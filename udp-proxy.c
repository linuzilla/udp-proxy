
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>
#include <strings.h>
#include <arpa/inet.h>
#include <time.h>
#include <ev.h>
#include "access-list.h"
#include "trigger.h"

#define SERVER_IP	"140.115.18.75"
#define SERVER_PORT	5093
#define EVENT_PORT  5092
#define BUF_SIZE	1500
#define TIMEOUT		30

struct args_t {
	char	*server;
	int		port;
};

struct client_info_t {
	ev_io			watcher;
	int			fd;
	struct sockaddr_in	addr;
	uint32_t		ip;
	int			port;
	time_t			lastuse;
	struct client_info_t	*next;
};

static struct client_info_t	*client_list = NULL;
static struct client_info_t	*free_list = NULL;
static int			timeout_second = TIMEOUT;
static int			sockfd;
static struct ev_loop		*loop;
static struct sockaddr_in	server_addr;

static void back_callback (EV_P_ ev_io *w, int revents) {
	struct sockaddr_in	addr;
	socklen_t	addr_len;
	char buffer[BUF_SIZE];

	struct client_info_t	*ptr = (struct client_info_t *) w;

	addr_len = sizeof addr;

	socklen_t bytes = recvfrom (ptr->fd,
			buffer, sizeof(buffer) - 1, 0,
			(struct sockaddr*) &addr, (socklen_t *) &addr_len);

	char * remoteIp = inet_ntoa (addr.sin_addr);
	int rmport = ntohs (addr.sin_port);

	time_t	now; time (&now); fprintf (stderr, "%s", ctime (&now));
	fprintf (stderr, "[->] Connect from: %s:%d, size=%d\n",
			remoteIp, rmport, bytes);

	sendto (sockfd, buffer, bytes, 0,
			(struct sockaddr*) &ptr->addr, sizeof ptr->addr);
}



static struct client_info_t *	allocate_entry (void) {
	struct client_info_t	*ptr;

	if (free_list != NULL) {
		ptr = free_list;
		free_list = free_list->next;
	} else {
		ptr = malloc (sizeof *ptr);
	}

	return ptr;
}

static void free_entry (struct client_info_t *entry) {
	fprintf (stderr, "Free Entry, fd=%d\n", entry->fd);
	ev_io_stop (loop, &entry->watcher);
	close (entry->fd);

	entry->next = free_list;
	free_list = entry;
}

static struct client_info_t *	new_entry (struct sockaddr_in *client) {
	struct client_info_t	*ptr;
	struct sockaddr_in	addr;

	if ((ptr = allocate_entry ()) == NULL) {
		perror ("malloc");
		return NULL;
	}

	if ((ptr->fd = socket (PF_INET, SOCK_DGRAM, 0)) < 0) {
		perror ("socket");
		return NULL;
	}

	memcpy (&ptr->addr, client, sizeof ptr->addr);
	ptr->ip = client->sin_addr.s_addr;
	ptr->port = client->sin_port;

	memset (&addr, 0, sizeof addr);

	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind (ptr->fd, (struct sockaddr*) &addr, sizeof addr) != 0) {
		perror ("bind");
		close (ptr->fd);
		return NULL;
	} else {
		socklen_t	len = sizeof ptr->addr;

		if (getsockname (ptr->fd, (struct sockaddr *) &addr, &len) == -1) {
			perror("getsockname");
			close (ptr->fd);
			return NULL;
		} else {
			fprintf(stderr, "add port %d\n", ntohs (addr.sin_port));
		}
	}

	return ptr;
}

static struct client_info_t * find_entry (struct sockaddr_in *addr) {
	struct client_info_t	*ptr = client_list;
	uint32_t		ip = addr->sin_addr.s_addr;
	int			port = addr->sin_port;

	while (ptr != NULL) {
		if (ptr->ip == ip && ptr->port == port) {
			return ptr;
		}
		ptr = ptr->next;
	}

	return ptr;
}

static struct client_info_t *	lookup (struct sockaddr_in *addr) {
	struct client_info_t *ptr = find_entry (addr);

	if (ptr == NULL) {
		if ((ptr = new_entry (addr)) != NULL) {
			ptr->next = client_list;
			client_list = ptr;

			ev_io_init (&ptr->watcher, back_callback, ptr->fd, EV_READ);
			ev_io_start (loop, &ptr->watcher);
		}
	}

	return ptr;
}

// void (*cb)(struct ev_loop *loop, struct ev_periodic *w, int revents)
// struct ev_loop *loop, ev_timer *w, int revents

static void tick_callback (EV_P_ ev_periodic *w, int revents) { // {{{
	struct client_info_t	*ptr = client_list;
	struct client_info_t	*prev = NULL;
	time_t			now;
	int			seconds;

	time (&now);

	while (ptr != NULL) {
		seconds = (int) difftime (now, ptr->lastuse);

		if (seconds > timeout_second) {
			if (ptr == client_list) {
				client_list = ptr->next;

				free_entry (ptr);
				ptr = client_list;
				continue;
			} else {
				prev->next = ptr->next;
				free_entry (ptr);
				ptr = prev;
			}
		}
		prev = ptr;
		ptr = ptr->next;
	}
	//fprintf (stderr, "\n\ntick\n\n");
	    // this causes the innermost ev_run to stop iterating
	// ev_break (EV_A_ EVBREAK_ONE);
} // }}}

static void main_callback (EV_P_ ev_io *w, int revents) {
	char buffer[BUF_SIZE];
	struct sockaddr_in	client_addr;
	socklen_t	addr_len = sizeof client_addr;

	socklen_t bytes = recvfrom (sockfd,
			buffer, sizeof(buffer) - 1, 0,
			(struct sockaddr*) &client_addr, (socklen_t *) &addr_len);

	struct client_info_t	*ptr = lookup (&client_addr);

	if (ptr != NULL) {
		char * remoteIp = inet_ntoa (client_addr.sin_addr);
		int rmport = ntohs (client_addr.sin_port);

		time (&ptr->lastuse);

		time_t	now; time (&now); fprintf (stderr, "%s", ctime (&now));
		fprintf (stderr, "[<-] Connect from: %s:%d, size=%d ... ",
				remoteIp, rmport, bytes);

		if (is_allow (ntohl (client_addr.sin_addr.s_addr))) {
			sendto (ptr->fd, buffer, bytes, 0,
					(struct sockaddr*) &server_addr, sizeof server_addr);
			fprintf (stderr, "ok.\n");
		} else {
			fprintf (stderr, "access deny\n");
		}
	}
}

static void *do_proxy(void *args) {
	struct args_t	*arg = args;
	struct sockaddr_in	addr;

	if ((sockfd = socket (PF_INET, SOCK_DGRAM, 0)) < 0) {
		perror ("socket");
		return NULL;
	}

	bzero (&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons (arg->port);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind (sockfd, (struct sockaddr*) &addr, sizeof(addr)) != 0) {
		perror ("bind");
		close (sockfd);
		return NULL;
	} else {
		struct hostent          *hp;

		memset (&server_addr, 0, sizeof server_addr);
		                     
		if (inet_aton (arg->server, &server_addr.sin_addr) != 0) {
			server_addr.sin_family = AF_INET;
		} else if ((hp = gethostbyname (arg->server)) != NULL) {
			socklen_t len = hp->h_length;
			server_addr.sin_family = hp->h_addrtype;

			if (len > sizeof server_addr.sin_addr) {
				len = sizeof server_addr.sin_addr;
			}

			memcpy (&server_addr.sin_addr, hp->h_addr_list[0], len);
		} else {
			perror (arg->server);
			return NULL;
		}
		server_addr.sin_port = htons (arg->port);
	}

	loop = ev_default_loop (0);

	if (loop != NULL) {
		ev_periodic	tick;
		ev_io		udp_watcher;

		ev_periodic_init (&tick, tick_callback, 0., 60.0, 0);
		tick.cb = tick_callback;
		ev_periodic_start (loop, &tick);

		ev_io_init (&udp_watcher, main_callback, sockfd, EV_READ);
		ev_io_start (loop, &udp_watcher);

		fprintf (stderr, "udp-proxy, proxy-to: %s:%d\n"
					"libev version: %d.%d\n"
					"Supported backends: select=%s, poll=%s, epoll=%s, kqueue=%s\n",
					arg->server, arg->port,
					ev_version_major (), ev_version_minor (),
					(ev_supported_backends () & EVBACKEND_SELECT) ? "Yes" : "No",
					(ev_supported_backends () & EVBACKEND_POLL) ? "Yes" : "No",
					(ev_supported_backends () & EVBACKEND_EPOLL) ? "Yes" : "No",
					(ev_supported_backends () & EVBACKEND_KQUEUE) ? "Yes" : "No"
		);
		ev_loop (loop, 0);
	}

	fprintf (stderr, "done.\n");

	// This point is never reached.
	close (sockfd);

	return NULL;
}

static void update_callback (const char *remoteIp, const int rmport, const char *buf, const int bytes) {
		/*
		time_t	now; time (&now); fprintf (stderr, "%s", ctime (&now));
		fprintf (stderr, "[->] Connect from: %s:%d, size=%d\n", remoteIp, rmport, bytes);
		fwrite(buf, 1, bytes, stderr);
		*/
	update_accesslist (SIGHUP);
}

int main (int argc, char *argv[]) {
	int	port = SERVER_PORT;
	char	*server = SERVER_IP;
	int		c;
	int		daemon_flag = 0;
	char	*logfile = NULL;
	char	*pidfile = "/var/log/spss/spss-proxy.pid";
	char	*runas = "spss";
	pthread_t	thr;
	bool	killflag = false;
	char	*listen_interface = "0.0.0.0";

	while ((c = getopt (argc, argv, "Df:h:p:Kl:")) != EOF) {
		switch (c) {
		case 'f':
			logfile = optarg;
			break;
		case 'D':
			daemon_flag = 1;
			break;
		case 'h':
			server = optarg;
			break;
		case 'K':
			killflag = true;
			break;
		case 'p':
			port = atoi (optarg);
			break;
		case 'l':
			listen_interface = optarg;
			break;
		case '?':
		default:
			break;
		}
	}

	if (killflag) {
		if (pidfile != NULL) {
			FILE	*f = fopen (pidfile, "r");
			int		pid;
			if (f != NULL) {
				if (fscanf (f, "%d", &pid) == 1) {
					fprintf (stderr, "Send signal to %d\n", pid);
					kill (pid, SIGHUP);
				}
				fclose (f);
			}
		} else {
			fprintf (stderr, "please define pidfile\n");
		}
		exit (0);
	}

	if (runas != NULL) {
		struct passwd	*pwd;
		if ((pwd = getpwnam (runas)) != NULL) {
			setregid (pwd->pw_gid, pwd->pw_gid);
			if (setreuid (pwd->pw_uid, pwd->pw_uid) != 0) {
				fprintf (stderr, "failed to set uid as user \'%s\'!\n", runas);
				exit (-1);
			}
			setregid (pwd->pw_gid, pwd->pw_gid);
		} else {
			fprintf (stderr, "user \'%s\' not found\n", runas);
		}
	}

	if (logfile != NULL) {
		int fd;

		if ((fd = open (logfile, O_WRONLY|O_CREAT|O_APPEND, 0644)) >= 0) {
			close (STDERR_FILENO);
			dup2 (fd, STDERR_FILENO);
			close (fd);
		}
	}

	if (daemon_flag) {
		if (fork() > 0) exit (0);
		if (chdir ("/") != 0) {
			fprintf (stderr, "@@\n");
		}

		if (pidfile != NULL) {
			FILE	*f = fopen (pidfile, "w+");
			if (f != NULL) {
				fprintf (f, "%d\n", getpid ());
				fclose (f);
			}
		}


		setsid ();
		signal (SIGHUP, SIG_IGN);
		close (STDIN_FILENO);
		close (STDOUT_FILENO);
	}

	if (init_ip_allow_list () == 0) {
		volatile bool term = false;
		struct args_t	args = { .server = server, .port = port };

		signal (SIGCHLD, SIG_IGN);
		pthread_create (&thr, NULL, do_proxy, &args);
		signal (SIGHUP, update_accesslist);
		trigger_by_udp(&term, listen_interface, EVENT_PORT, update_callback);
		pthread_join (thr, NULL);

		close_ip_allow_list ();
	}

	return EXIT_SUCCESS;
}

/* vim settings {{{
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 * }}} */
