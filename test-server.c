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

static void onWriteCompleted(SocketRec *client) {
		_info("Write completed: %d\n", client->socket);
		//Disconnect
		close(client->socket);
		//Unregister
		pumpRemoveSocket(client->pump, client->socket);
}

static void onWritable(SocketRec *client) {
	//Write the request
	char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<h1>It works</h1>";
	int status = pumpScheduleWrite(client, response, strlen(response));

	assert(status >= 0);
}

static void onReadable(SocketRec *client) {
	char buff[256];

	int len = read(client->socket, buff, sizeof(buff));

	if (len == 0) {
		//Client has disconnected
		//Disconnect
		close(client->socket);
		//Unregister
		pumpRemoveSocket(client->pump, client->socket);

		return;
	}
	printf("%.*s", len, buff);

	client->onWritable = onWritable;
}


static void onAccept(SocketRec *server, int sock) {
	printf("Accepted socket: %d\n", sock);
	assert(sock >= 0);

	SocketRec *client = pumpRegisterSocket(server->pump, sock, server);
	client->onReadable = onReadable;
	client->onWriteCompleted = onWriteCompleted;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		puts("Usage: test_server port");
		return 1;
	}

	int port = 0;

	if (sscanf(argv[1], "%d", &port) < 1) {
		printf("Invalid port: %s\n", argv[1]);

		return 1;
	}

	EventPump *pump = newEventPump();
	SocketRec *rec = pumpRegisterServer(pump, port, NULL);
	rec->onAccept = onAccept;

	pumpStart(pump);

	deleteEventPump(pump);

	return 0;
}
