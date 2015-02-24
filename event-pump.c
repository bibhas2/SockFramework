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

#define DEBUG 1

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

		for (ListNode *n = pump->sockets->first; n != NULL; 
			n = n->next) {
			SocketRec *rec = n->data;
			assert(rec->socket >= 0);

			FD_SET(rec->socket, &readFdSet);

			if (rec->onWritable != NULL || rec->onConnect != NULL) {
				FD_SET(rec->socket, &writeFdSet);
			}

			highest_socket = rec->socket > highest_socket ?
				rec->socket : highest_socket;
		}

                timeout.tv_sec = pump->timeout;
                timeout.tv_usec = 0;

                int numEvents = select(highest_socket + 1, &readFdSet, &writeFdSet, NULL, &timeout);
                DIE(numEvents, "select() failed.");
		if (numEvents == 0) {
			_info("select() timed out.\n");

			for (ListNode *n = pump->sockets->first; n != NULL;) {
				SocketRec *rec = n->data;
				n = n->next;

				if (rec->onTimeout != NULL) {
					rec->onTimeout(rec);
				}
			}

			continue; //Timeout
                }

		//Dispatch
		for (ListNode *n = pump->sockets->first; n != NULL;) {
			SocketRec *rec = n->data;
			n = n->next;

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

static SocketRec *newSocketRec() {
	SocketRec *rec = malloc(sizeof(SocketRec));
	assert(rec != NULL);

	rec->socket = -1;
	rec->data = NULL;
	rec->onReadable = NULL;
	rec->onAccept = NULL;
	rec->onWritable = NULL;
	rec->onTimeout = NULL;
	rec->onConnect = NULL;

	return rec;
}

static void deleteSocketRec(SocketRec *rec) {
	rec->socket = -1;
	rec->data = NULL;

	free(rec);
}

static void clear_sockets(EventPump *pump) {
	while (pump->sockets->first != NULL) {
		SocketRec *rec = pump->sockets->first->data;

		deleteSocketRec(rec);
		listRemoveNode(pump->sockets, pump->sockets->first);
	}
}

EventPump *newEventPump() {
	EventPump *pump = calloc(1, sizeof(EventPump));
	assert(pump != NULL);

	pump->sockets = newList();

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
	clear_sockets(pump);

	return 1;
}

void deleteEventPump(EventPump *pump) {
	clear_sockets(pump);
	deleteList(pump->sockets);
	free(pump);
}

SocketRec *pumpRegisterSocket(EventPump *pump, int socket, void *data) {
	SocketRec *rec = newSocketRec();

	rec->socket = socket;
	rec->data = data;
	rec->pump = pump;

	listAddLast(pump->sockets, rec);

	return rec;
}

void *pumpRemoveSocket(EventPump *pump, int socket) {
	void *data = NULL;

	for (ListNode *n = pump->sockets->first; n != NULL; n = n->next) {
		SocketRec *rec = n->data;

		if (rec->socket == socket) {
			listRemoveNode(pump->sockets, n);
			deleteSocketRec(rec);
			data = rec->data;

			break;
		}
	}
	_info("pumpRemoveSocket received invalid socket.");
	abort();

	return data;
}
