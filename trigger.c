#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>


int trigger_by_udp(volatile bool *terminate, const char *server, const int port, void (*func)(const char *,const int, const char *, const int)) {
	int sockfd;
	struct sockaddr_in	server_addr;
	struct sockaddr_in	addr;
	socklen_t	addr_len;
	char buffer[1500];
	struct hostent          *hp;

	if ((sockfd = socket (PF_INET, SOCK_DGRAM, 0)) < 0) {
		perror ("socket");
		return 1;
	}

	memset (&server_addr, 0, sizeof server_addr);
		                     
	if (inet_aton (server, &server_addr.sin_addr) != 0) {
		server_addr.sin_family = AF_INET;
	} else if ((hp = gethostbyname (server)) != NULL) {
		socklen_t len = hp->h_length;
		server_addr.sin_family = hp->h_addrtype;

		if (len > sizeof server_addr.sin_addr) {
			len = sizeof server_addr.sin_addr;
		}

		memcpy (&server_addr.sin_addr, hp->h_addr_list[0], len);
	} else {
		perror (server);
		return -3;
	}
	server_addr.sin_port = htons (port);

	if (bind (sockfd, (struct sockaddr*) &server_addr, sizeof(server_addr)) != 0) {
		perror ("bind");
		close (sockfd);
		return -2;
	}


	addr_len = sizeof addr;

	while (! *terminate) {
		int bytes = recvfrom (sockfd,
				buffer, sizeof(buffer) - 1, 0,
				(struct sockaddr*) &addr, (socklen_t *) &addr_len);

		char * remoteIp = inet_ntoa (addr.sin_addr);
		int rmport = ntohs (addr.sin_port);

		func(remoteIp, rmport, buffer, bytes);
	}

	return 0;
}

#if 0
#include <time.h>

void callback (const char *remoteIp, const int rmport, const char *buf, const int bytes) {
		time_t	now; time (&now); fprintf (stderr, "%s", ctime (&now));
		fprintf (stderr, "[->] Connect from: %s:%d, size=%d\n", remoteIp, rmport, bytes);
		fwrite(buf, 1, bytes, stderr);
}

int main (int argc, char *argv[]) {
	volatile bool term = false;
	trigger_by_udp(&term, "0.0.0.0", 8848, callback);
	return 0;
}
#endif
/* vim settings {{{
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 * }}} */
