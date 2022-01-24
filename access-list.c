#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "configs.h"
#include "access-list.h"
#include "db_mysql.h"

#define MAX_IP_LIST	1024

struct accesslist_t {
	bool		inuse;
	uint32_t	ip;
	time_t		expire;
	time_t		lastuse;
};

struct classtime_t {
	int			wday;
	int			from;
	int			to;
	struct classtime_t	*next;
};

static struct db_mysql_t *	mydb;
static struct classtime_t *	classtime = NULL;
static pthread_t		update_thread;
static pthread_t		timer_thread;
static pthread_mutex_t		mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t		condi = PTHREAD_COND_INITIALIZER;
static volatile bool		update_event = false;
static volatile bool		terminate;
static struct accesslist_t	iplist[MAX_IP_LIST];
static int		iplist_index;

static struct db_mysql_t       *mydb = NULL;
static int      connected = 0;
// static int      last_insert_id = 0;

static bool connect_mysql() { // {{{
	if (! connected) {
		if (! mydb->connect (mydb, DATABASE_HOST, DATABASE_USER, DATABASE_PASSWORD, DATABASE_NAME)) {
			mydb->perror (mydb, "mysql");
			return false;
		}
		connected = 1;
	}
	return true;
} // }}}

static void disconnect_mysql() { // {{{
	if (connected) {
		mydb->disconnect(mydb);
		connected = 0;
	}
} // }}} 



static void create_db (void) {
	char	*create_query[] = {
				DATABASE_CREATE_CLASS,
				DATABASE_CREATE_REQUEST
	};

	int len = sizeof create_query / sizeof (char *);
	int	i;

	connect_mysql ();
	for (i = 0; i < len; i++) {
		mydb->query (mydb, create_query[i]);
		mydb->free_result (mydb);
	}
	disconnect_mysql ();
}

bool is_allow (const uint32_t ip) {
	int	i;
	time_t	now;
	bool	retval = false;
	bool	not_allow = false;
	struct tm	tm;
	struct classtime_t	*ptr;
	struct in_addr in;

	in.s_addr = htonl (ip);

	pthread_mutex_lock (&mutex);

	time (&now);

	if (classtime != NULL) {
		localtime_r (&now, &tm);

		for (ptr = classtime; ptr != NULL; ptr = ptr->next) {
			if (ptr->wday == tm.tm_wday) {
				if (tm.tm_hour >= ptr->from && tm.tm_hour <= ptr->to) {
					not_allow = true;
					break;
				} else if (tm.tm_hour == ptr->from - 1 && tm.tm_min > 40) {
					not_allow = true;
					break;
				}
			}
		}
	}

	// tm.tm_hour; tm.tm_wday;

	if (! not_allow) {
		bool	found = false;
		for (i = 0; i < iplist_index; i++) {
			if (iplist[i].inuse) {
				if (ip == iplist[i].ip) {
					if (now > iplist[i].expire) {
						fprintf (stderr, "%s(%d): %s expired\n", __FILE__, __LINE__,
								inet_ntoa (in));
						iplist[i].inuse = false;
						found = true;
					} else {
						fprintf (stderr, "%s(%d): %s access ok\n", __FILE__, __LINE__,
								inet_ntoa (in));
						iplist[i].lastuse = now;
						retval = true;
						found = true;
					}
					break;
				} else {
					fprintf (stderr, "%s(%d): %u vs %u\n", __FILE__, __LINE__, ip, iplist[i].ip);
				}
			} else {
				fprintf (stderr, "%s(%d): %u not inuse\n", __FILE__, __LINE__, ip);
			}
		}
		if (! found) {
			fprintf (stderr, "%s(%d): %s not found\n", __FILE__, __LINE__,
						inet_ntoa (in));
		}
	}

	pthread_mutex_unlock (&mutex);

	return retval;
}

static void update_classtime (void) {
	char		**result;
	struct classtime_t	*freelist = classtime;
	struct classtime_t	*newhead = NULL;
	struct classtime_t	*ptr = NULL;

	mydb->query (mydb, DATABASE_CLASS_QUERY);
	if (mydb->num_rows (mydb) > 0) {
		while ((result = mydb->fetch (mydb)) != NULL) {
			if (freelist != NULL) {
				ptr = freelist;
				freelist = freelist->next;
			} else {
				ptr = malloc (sizeof *ptr);
			}
			ptr->wday = atoi (result[0]);
			ptr->from = atoi (result[1]);
			ptr->to = atoi (result[2]);
			ptr->next = newhead;
			newhead = ptr;
			fprintf (stderr, "Deny on %d: from %d to %d\n", ptr->wday, ptr->from, ptr->to);
		}
	} else {
		fprintf (stderr, "No class entry\n");
	}
	mydb->free_result (mydb);

	classtime = newhead;

	while ((ptr = freelist) != NULL) {
		freelist = freelist->next;
		free (ptr);
	}
}

static void * update_iplist (void *args) {
	char		**result;
	int			i;

	while (! terminate) {
		pthread_mutex_lock (&mutex);

		while (! update_event) {
			pthread_cond_wait (&condi, &mutex);
		}
		update_event = false;

		fprintf (stderr, "update ip access-list\n");

		for (i = 0; i < MAX_IP_LIST; i++) {
			iplist[i].inuse = false;
		}
		iplist_index = 0;

		if (connect_mysql ()) {
			update_classtime ();
			// delete from tbl_single where date_add(editdate, INTERVAL 5 MINUTE) < NOW();
			mydb->query (mydb, DATABASE_REQUEST_QUERY);

			if (mydb->num_rows (mydb) > 0) {
				while ((result = mydb->fetch (mydb)) != NULL) {
					iplist[iplist_index].ip = strtoul(result[0], NULL, 10);
					iplist[iplist_index].expire = strtoul(result[1], NULL, 10);
					iplist[iplist_index].inuse = true;
					iplist[iplist_index].lastuse = 0;

					fprintf (stderr, "ip=%u\n", iplist[iplist_index].ip);

					if (++iplist_index >= MAX_IP_LIST) break;
				}
			}
			mydb->free_result (mydb);

			// -----------------------
			fprintf (stderr, "update access-list ... %d record(s)\n", iplist_index);
			disconnect_mysql ();
		}

		pthread_mutex_unlock (&mutex);
	}
	return NULL;
}

static void * minute_timer (void *args) {
	struct timeval	tv;
	// char		buffer[80];
	time_t		prev_min;
	time_t		current_min;

	gettimeofday (&tv, NULL);
	prev_min = tv.tv_sec / 60;

	while (! terminate) {
		gettimeofday (&tv, NULL);
		if ((current_min = tv.tv_sec / 60) == prev_min) {
			usleep ((59 - (tv.tv_sec % 60)) * 1000000 + 1000000 - tv.tv_usec);
		} else {
			prev_min = current_min;
			// fprintf (stderr, "%s", ctime_r (&tv.tv_sec, buffer));
		}
	}

	return NULL;
}

int init_ip_allow_list (void) {
	int	i;

	for (i = 0; i < MAX_IP_LIST; i++) {
		iplist[i].inuse = false;
	}

	iplist_index = 0;

	if ((mydb = new_dbmysql ()) == NULL) {
		perror ("new_dbmysql");
		return -1;
	} else {
		mydb->verbose (mydb, 4);
		create_db ();
	}

	pthread_create (&update_thread, NULL, update_iplist, NULL);
	pthread_create (&timer_thread, NULL, minute_timer, NULL);

	usleep (100);
	update_accesslist (SIGHUP);
	usleep (100);

	return 0;
}

void close_ip_allow_list (void) {
	terminate = true;

	pthread_mutex_lock (&mutex);
	update_event = true;
	pthread_cond_signal (&condi);
	pthread_mutex_unlock (&mutex);

	pthread_join (update_thread, NULL);
	pthread_join (timer_thread, NULL);
}

void update_accesslist (const int signo) {
	pthread_mutex_lock (&mutex);
	update_event = true;
	pthread_cond_signal (&condi);
	pthread_mutex_unlock (&mutex);
}

/*
void init_memcached (void) {
	memcached_st	*memc;
	memcached_server_st	*servers;

	void memcached_server_list_free (memcached_server_st *ptr);

	memc = memcached_create (NULL);
	servers = memcached_servers_parse ("localhost:11211");
	memcached_server_push (memc, servers);
    memcached_server_list_free (servers);

	memcached_free (memc);
}
*/
/* vim settings {{{
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 * }}} */
