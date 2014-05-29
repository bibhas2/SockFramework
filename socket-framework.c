#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>

#include "socket-framework.h"

#define DIE(value, message) if (value < 0) {perror(message); exit(value);}

void
_info(const char* fmt, ...) {
        va_list ap;

        va_start(ap, fmt);

        printf("INFO: ");
        vprintf(fmt, ap);
        printf("\n");
}

void
zero_fd_list(int *list) {
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		list[i] = 0;
	}
}


void
populate_fd_set(ServerState *state, fd_set *pFdSet) {
	FD_ZERO(pFdSet);
	
	//Set the server socket
	FD_SET(state->server_socket, pFdSet);

	//Set the clients
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		int fd = state->client_state[i].fd;

		if (fd != 0) {
			FD_SET(fd, pFdSet);
		}
	}
}

void
disconnect_clients(ServerState *state) {
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		int fd = state->client_state[i].fd;

		if (fd != 0) {
			close(fd);
			state->client_state[i].fd = 0;
			state->client_state[i].data = NULL;
		}
	}
}

int
add_client_fd(ServerState *state, int fd) {
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (state->client_state[i].fd == 0) {
			//We have a free slot
			state->client_state[i].fd = fd;
			state->client_state[i].data = NULL;

			return i;
		}
	}

	return -1;
}

int
remove_client_fd(ServerState *state, int fd) {
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (state->client_state[i].fd == fd) {
			//We found it!
			state->client_state[i].fd = 0;
			state->client_state[i].data = NULL;

			return i;
		}
	}

	return -1;
}

int
handle_client_write(ServerState* state, ClientState *cli_state) {
	char buff[256];

	int bytesRead = read(cli_state->fd, buff, sizeof(buff));

	if (bytesRead < 0) {
		return -1;
	}
	if (bytesRead == 0) {
		return 0;
	}
	//Dump the written data
	if (state->on_client_write) {
		state->on_client_write(state, cli_state, buff, bytesRead);
	}	
	return bytesRead;
}

void
disconnect_client(ServerState *state, ClientState *cli_state) {
	if (state->on_client_disconnect) {
		state->on_client_disconnect(state, cli_state);
	}
	close(cli_state->fd);
	remove_client_fd(state, cli_state->fd);
}

void
server_loop(ServerState *state) {
	if (state->on_loop_start) {
		state->on_loop_start(state);
	}

	fd_set fdSet;
	struct timeval timeout;


	while (1) {
		populate_fd_set(state, &fdSet);

		timeout.tv_sec = 10;
		timeout.tv_usec = 0;

		int numEvents = select(FD_SETSIZE, &fdSet, NULL, NULL, &timeout);
		DIE(numEvents, "select() failed.");

		if (numEvents == 0) {
			_info("select() timed out.");

			break;
		}
		
		//Make sense out of the event
		if (FD_ISSET(state->server_socket, &fdSet)) {
			_info("Client is connecting...");
			int clientFd = accept(state->server_socket, NULL, NULL);

			DIE(clientFd, "accept() failed.");

			int position = add_client_fd(state, clientFd);

			if (position < 0) {
				_info("Too many clients. Disconnecting...");
				close(clientFd);
				remove_client_fd(state, clientFd);
			}
			if (state->on_client_connect) {
				state->on_client_connect(state, state->client_state + position);
			}
		} else {
			//Client wrote something or disconnected
			for (int i = 0; i < MAX_CLIENTS; ++i) {
				if (FD_ISSET(state->client_state[i].fd, &fdSet)) {
					ClientState *cli_state = state->client_state + i;
					int status = handle_client_write(state, cli_state);
					if (status < 1) {
						_info("Client is finished. Status: %d", status);
						if (state->on_client_disconnect) {
							state->on_client_disconnect(state, cli_state);
						}
						close(cli_state->fd);
						remove_client_fd(state, cli_state->fd);
					}
				}
			}
		}
	}

	disconnect_clients(state);
}

void
start_server(ServerState *state) {
	int sock = socket(PF_INET, SOCK_STREAM, 0);

	DIE(sock, "Failed to open socket.");

        int reuse = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(state->port);

	int status = bind(sock, (struct sockaddr*) &addr, sizeof(addr));

	DIE(status, "Failed to bind to port.");

	_info("Calling listen.");
	status = listen(sock, 10);
	_info("listen returned.");

	DIE(status, "Failed to listen.");

	state->server_socket = sock;

	server_loop(state);

	close(sock);
}

ServerState* new_server_state(int port) {
	ServerState *state = (ServerState*) calloc(1, sizeof(ServerState));

	state->port = port;

	return state;
}

void
delete_server_state(ServerState *state) {
	free(state);
}
