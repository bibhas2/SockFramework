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

#if DEBUG
#define _info printf("INFO: "); printf
#else
#define _info //
#endif

static int check_connect_status(int fd) {
	int valopt;
	socklen_t lon = sizeof(int);
	int result = getsockopt(fd, SOL_SOCKET, SO_ERROR, 
		(void*)(&valopt), &lon); 
	DIE(result, "Error in getsockopt()");
	//Check the value of valopt
	if (valopt) {
		_info("Error connecting to server: %s.\n", strerror(valopt));
		return 0;
	}

	return 1;
}

static void pump_loop(EventPump *pump) {
        fd_set readFdSet, writeFdSet;
        struct timeval timeout;

	pump->status = PUMP_STATUS_RUNNING;

	while (pump->status == PUMP_STATUS_RUNNING) {
		FD_ZERO(&readFdSet);
		FD_ZERO(&writeFdSet);

		//Setup the set
		int highest_socket = -1;

		for (int i = 0; i < PUMP_MAX_SOCKET; ++i) {
			SocketRec *rec = pump->sockets + i;

			if (rec->socket < 0) {
				continue;
			}

			FD_SET(rec->socket, &readFdSet);
			if (rec->onWritable != NULL || rec->onConnect != NULL) {
				FD_SET(rec->socket, &writeFdSet);
			}

			highest_socket = rec->socket;
		}

		//For now
		assert(highest_socket >= 0);

                timeout.tv_sec = pump->timeout;
                timeout.tv_usec = 0;

                int numEvents = select(highest_socket + 1, &readFdSet, &writeFdSet, NULL, &timeout);
                DIE(numEvents, "select() failed.");
		if (numEvents == 0) {
			_info("select() timed out.\n");

			for (int i = 0; i < PUMP_MAX_SOCKET; ++i) {
				SocketRec *rec = pump->sockets + i;

				if (rec->socket < 0) {
					continue;
				}
				if (rec->onTimeout != NULL) {
					rec->onTimeout(rec);
				}
			}
                }

		//Dispatch
		for (int i = 0; i < PUMP_MAX_SOCKET; ++i) {
			SocketRec *rec = pump->sockets + i;

			if (rec->socket < 0) {
				continue;
			}
			if (FD_ISSET(rec->socket, &writeFdSet)) {
				_info("Socket writable: %d\n", rec->socket);
				if (rec->onConnect != NULL) {
					rec->onConnect(rec, 
						check_connect_status(rec->socket));
					rec->onConnect = NULL;
				} else if (rec->onWritable != NULL) {
					rec->onWritable(rec);
				}
			}
			//Is socket removed?
			if (rec->socket < 0) {
				continue;
			}
			if (FD_ISSET(rec->socket, &readFdSet)) {
				_info("Socket readable: %d\n", rec->socket);
				if (rec->onAccept != NULL) {
					int sock = accept(rec->socket, 
						NULL, NULL);
					DIE(sock, "accept() failed.");
					int status = fcntl(sock, 
						F_SETFL, O_NONBLOCK);
					DIE(status, "Failed to set non blocking mode for client socket.");
					rec->onAccept(rec, sock);
				} else if (rec->onReadable != NULL) {
					rec->onReadable(rec);
				}
			}
		}
	}

	pump->status = PUMP_STATUS_STOPPED;
}

static void clear_socket(SocketRec *rec) {
	rec->socket = -1;
	rec->data = NULL;
	rec->onReadable = NULL;
	rec->onAccept = NULL;
	rec->onWritable = NULL;
	rec->onTimeout = NULL;
	rec->onConnect = NULL;
}

static void reset_sockets(EventPump *pump) {
	for (int i = 0; i < PUMP_MAX_SOCKET; ++i) {
		SocketRec *rec = pump->sockets + i;

		clear_socket(rec);
		rec->pump = pump;
	}
}

EventPump *newEventPump() {
	EventPump *pump = calloc(1, sizeof(EventPump));
	assert(pump != NULL);

	reset_sockets(pump);

	pump->timeout = 10; //Seconds

	return pump;
}

int pumpStart(EventPump *pump) {
	assert(pump->status == PUMP_STATUS_STOPPED);

	pump_loop(pump);

	return 1;
}

int pumpStop(EventPump *pump) {
	assert(pump->status == PUMP_STATUS_RUNNING);

	pump->status = PUMP_STATUS_STOP_REQUESTED;
	reset_sockets(pump);

	return 1;
}

void deleteEventPump(EventPump *pump) {
	reset_sockets(pump);
	free(pump);
}

SocketRec *pumpRegisterSocket(EventPump *pump, int socket, void *data) {
	assert(socket >= 0 && socket < PUMP_MAX_SOCKET);

	SocketRec *rec = pump->sockets + socket;
	assert(rec->socket < 0); //Make sure its free

	rec->socket = socket;
	rec->data = data;

	return rec;
}

void *pumpRemoveSocket(EventPump *pump, int socket) {
	assert(socket >= 0 && socket < PUMP_MAX_SOCKET);

	SocketRec *rec = pump->sockets + socket;
	assert(rec->socket >= 0); //Make sure its in use

	void *data = rec->data;

	clear_socket(rec);

	return data;
}