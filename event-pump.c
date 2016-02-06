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

static int write_pending_data(SocketRec *rec) {
	assert(rec->write_buffer != NULL);
	assert(rec->write_length > rec->write_completed);

	char *buffer_start = rec->write_buffer + rec->write_completed;
	int bytesWritten = write(rec->socket,
		buffer_start,
		rec->write_length - rec->write_completed);

	_info("Written %d of %zu bytes\n", bytesWritten, rec->write_length);

	if (bytesWritten < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			return -1;
		}
		//Write will block. Not an error.
		_info("Write block detected.");

		return 0;
	}

	if (bytesWritten == 0) {
		//Client has disconnected. We convert that to an error.
		return -1;
	}

	rec->write_completed += bytesWritten;

	if (rec->write_completed == rec->write_length) {
		pumpCancelWrite(rec);

		if (rec->onWriteCompleted != NULL) {
			rec->onWriteCompleted(rec);
		}
	}

	return 0;
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

			if (rec->onWritable != NULL || rec->onConnect != NULL || rec->write_buffer != NULL) {
				FD_SET(rec->socket, &writeFdSet);
			}

			highest_socket = rec->socket > highest_socket ?
				rec->socket : highest_socket;
		}

    timeout.tv_sec = pump->timeout;
    timeout.tv_usec = 0;

		_info("Selecting for events in %zu sockets.\n", pump->sockets->size);
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

			//Process writable state
			if (FD_ISSET(rec->socket, &writeFdSet)) {
				_info("Socket writable: %d\n", rec->socket);
				if (rec->onConnect != NULL) {
					rec->onConnect(rec,
						check_connect_status(rec->socket));
					rec->onConnect = NULL;
				} else {
					if (rec->onWritable != NULL) {
						rec->onWritable(rec);
					}
					if (rec->write_buffer != NULL) {
						write_pending_data(rec);
					}
				}
			}

			//Is socket removed?
			if (rec->socket < 0) {
				continue;
			}

			//Process readable state
			if (FD_ISSET(rec->socket, &readFdSet)) {
				_info("Socket readable: %d\n", rec->socket);
				if (rec->onAccept != NULL) {
					int sock = accept(rec->socket, NULL, NULL);
					DIE(sock, "accept() failed.");
					int status = fcntl(sock, F_SETFL, O_NONBLOCK);
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
	SocketRec *rec = calloc(1, sizeof(SocketRec));
	assert(rec != NULL);

	rec->socket = -1;
	rec->data = NULL;
	rec->write_buffer = NULL;
	rec->write_length = rec->write_completed = 0;
	rec->onReadable = NULL;
	rec->onAccept = NULL;
	rec->onWritable = NULL;
	rec->onTimeout = NULL;
	rec->onConnect = NULL;
	rec->onWriteCompleted = NULL;

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
			data = rec->data;
			//Removed from list managed sockets
			listRemoveNode(pump->sockets, n);
			//Destroy the record
			deleteSocketRec(rec);

			return data;
		}
	}

	_info("pumpRemoveSocket received invalid socket.");
	abort();

	return data;
}

SocketRec * pumpRegisterClient(EventPump *pump, const char *host, const char *port, void *data) {
	_info("Connecting to %s:%s\n", host, port);

	struct addrinfo hints, *res;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	_info("Resolving name...");
	int status = getaddrinfo(host, port, &hints, &res);
	DIE(status, "Failed to resolve address.");
	if (res == NULL) {
		_info("Failed to resolve address: %s\n", host);
		abort();
	}

	int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	DIE(sock, "Failed to open socket.");

	status = fcntl(sock, F_SETFL, O_NONBLOCK);
	DIE(status, "Failed to set non blocking mode for socket.");

	status = connect(sock, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);

	_info("Asynchronous connection initiated.\n");
	if (status < 0 && errno != EINPROGRESS) {
		perror("Failed to connect to port.");
		close(sock);

		return NULL;
	}

	return pumpRegisterSocket(pump, sock, data);
}

SocketRec * pumpRegisterServer(EventPump *pump, int port, void *data) {
	int status;

	int sock = socket(PF_INET, SOCK_STREAM, 0);
	DIE(sock, "Failed to open socket.");

	status = fcntl(sock, F_SETFL, O_NONBLOCK);
	DIE(status, "Failed to set non blocking mode for server listener socket.");

  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	status = bind(sock, (struct sockaddr*) &addr, sizeof(addr));
	DIE(status, "Failed to bind to port.");

	_info("Calling listen.\n");
	status = listen(sock, 10);
	_info("listen returned.\n");
	DIE(status, "Failed to listen.\n");

	return pumpRegisterSocket(pump, sock, data);
}

int pumpScheduleWrite(SocketRec *rec, char *buffer, size_t length) {
	if (rec->write_buffer != NULL) {
		_info("A write is already in progress.\n");

		return -1;
	}

	rec->write_buffer = buffer;
	rec->write_length = length;
	rec->write_completed = 0;

	return 0;
}

int pumpCancelWrite(SocketRec *rec) {
	rec->write_buffer = NULL;
	rec->write_length = rec->write_completed = 0;

	return 0;
}
