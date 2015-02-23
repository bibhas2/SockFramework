#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#include "event-pump.h"

#define DIE(value, message) if (value < 0) {perror(message); abort();}

#define _info printf("INFO: "); printf

char req[1024];

static void onReadable(EventPump *pump, SocketRec *rec) {
	char buff[256];

	int len = read(rec->socket, buff, sizeof(buff));

	if (len == 0) {
		//Orderly disconnect
		pumpStop(pump);

		return;
	}
	printf("%.*s", len, buff);
}

static void onWritable(EventPump *pump, SocketRec *rec) {
	//Write the request
	puts(req);
	int status = write(rec->socket, req, strlen(req));

	assert(status > 0);

	rec->onWritable = NULL;
}

static void onConnect(EventPump *pump, SocketRec *rec, int status) {
	assert(status == 1);

	rec->onWritable = onWritable;
}

static void onTimeout(EventPump *pump, SocketRec *rec) {
	close(rec->socket);
	pumpStop(pump);
}

int make_connection(const char *host, const char *port) {
	_info("Connecting to %s:%s", host, port);

	struct addrinfo hints, *res;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	_info("Resolving name...");
	int status = getaddrinfo(host, port, &hints, &res);
	DIE(status, "Failed to resolve address.");
	if (res == NULL) {
		_info("Failed to resolve address: %s", host);
		abort();
	}

	int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	DIE(sock, "Failed to open socket.");

	status = fcntl(sock, F_SETFL, O_NONBLOCK);
	DIE(status, "Failed to set non blocking mode for socket.");

	status = connect(sock, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);

	_info("Asynchronous connection initiated.");
	if (status < 0 && errno != EINPROGRESS) {
		perror("Failed to connect to port.");
		close(sock);

		return -1;
	}

	return sock;
}

int main(int argc, char **argv) {
	if (argc < 4) {
		puts("Usage: test_client host port path");
		return 1;
	}

	const char *host = argv[1];
	const char *port = argv[2];
	const char *path = argv[3];

        char *fmt = "GET %s HTTP/1.1\r\n"
                "Host: %s:%s\r\n"
                "Accept: */*\r\n"
                "\r\n";
	snprintf(req, sizeof(req), fmt, path, host, port);

	EventPump *pump = newEventPump();
	int sock = make_connection(host, port);
	SocketRec *rec = pumpRegisterSocket(pump,
		sock, req);
	rec->onConnect = onConnect;
	rec->onReadable = onReadable;
	rec->onWritable = NULL;
	rec->onTimeout = onTimeout;

	pumpStart(pump);

	deleteEventPump(pump);

	return 0;
}
