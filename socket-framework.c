#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>

#include "socket-framework.h"

#define DIE(value, message) if (value < 0) {perror(message); exit(value);}

#define RW_STATE_NONE 0
#define RW_STATE_READ 1
#define RW_STATE_WRITE 2

void
_info(const char* fmt, ...) {
        va_list ap;

        va_start(ap, fmt);

        printf("INFO: ");
        vprintf(fmt, ap);
        printf("\n");
}

void
populate_fd_set(ServerState *state, fd_set *pReadFdSet, fd_set *pWriteFdSet) {
	FD_ZERO(pReadFdSet);
	FD_ZERO(pWriteFdSet);
	
	//Set the server socket
	FD_SET(state->server_socket, pReadFdSet);

	//Set the clients
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		int fd = state->client_state[i].fd;

		if (fd == 0) {
			continue;
		}

		if (state->client_state[i].read_write_flag == RW_STATE_READ) {
			FD_SET(fd, pReadFdSet);
		} else if (state->client_state[i].read_write_flag == RW_STATE_WRITE) {
			FD_SET(fd, pWriteFdSet);
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
			state->client_state[i].read_write_flag = RW_STATE_NONE;
			state->client_state[i].buffer = NULL;
			state->client_state[i].length = 0;
			state->client_state[i].completed = 0;

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
			state->client_state[i].read_write_flag = RW_STATE_NONE;
			state->client_state[i].buffer = NULL;
			state->client_state[i].length = 0;
			state->client_state[i].completed = 0;

			return i;
		}
	}

	return -1;
}

int
handle_client_write(ServerState* state, ClientState *cli_state) {
	if (cli_state->read_write_flag != RW_STATE_READ) {
		_info("Socket is not trying to read.");
		return -1;
	}
	if (cli_state->buffer == NULL) {
		_info("Read buffer not setup.");
		return -1;
	}
	if (cli_state->length == cli_state->completed) {
		_info("Read was already completed.");
		return -1;
	}

	char *buffer_start = cli_state->buffer + cli_state->completed;
	int bytesRead = read(cli_state->fd, 
		buffer_start, 
		cli_state->length - cli_state->completed);

	if (bytesRead < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			return -1;
		}
		//Read will block. Not an error.
		_info("Read block detected.");
		return 0;
	}
	if (bytesRead == 0) {
		//Client has disconnected. We convert that to an error.
		return -1;
	}
	
	cli_state->completed += bytesRead;

	if (state->on_read) {
		state->on_read(state, cli_state, buffer_start, bytesRead);
	}
	if (cli_state->completed == cli_state->length) {
		cli_state->read_write_flag = RW_STATE_NONE;

		if (state->on_read_completed) {
			state->on_read_completed(state, cli_state);
		}
	}

	return bytesRead;
}

int
handle_client_read(ServerState* state, ClientState *cli_state) {
	if (cli_state->read_write_flag != RW_STATE_WRITE) {
		_info("Socket is not trying to write.");
		return -1;
	}
	if (cli_state->buffer == NULL) {
		_info("Write buffer not setup.");
		return -1;
	}
	if (cli_state->length == cli_state->completed) {
		_info("Write was already completed.");
		return -1;
	}

	char *buffer_start = cli_state->buffer + cli_state->completed;
	int bytesWritten = write(cli_state->fd, 
		buffer_start, 
		cli_state->length - cli_state->completed);

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
	
	cli_state->completed += bytesWritten;

	if (state->on_write) {
		state->on_write(state, cli_state, buffer_start, bytesWritten);
	}
	if (cli_state->completed == cli_state->length) {
		cli_state->read_write_flag = RW_STATE_NONE;

		if (state->on_write_completed) {
			state->on_write_completed(state, cli_state);
		}
	}

	return bytesWritten;
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

	fd_set readFdSet, writeFdSet;
	struct timeval timeout;


	while (1) {
		populate_fd_set(state, &readFdSet, &writeFdSet);

		timeout.tv_sec = 10;
		timeout.tv_usec = 0;

		int numEvents = select(FD_SETSIZE, &readFdSet, &writeFdSet, NULL, &timeout);
		DIE(numEvents, "select() failed.");

		if (numEvents == 0) {
			_info("select() timed out.");

			break;
		}
		
		//Make sense out of the event
		if (FD_ISSET(state->server_socket, &readFdSet)) {
			_info("Client is connecting...");
			int clientFd = accept(state->server_socket, NULL, NULL);

			DIE(clientFd, "accept() failed.");

			int position = add_client_fd(state, clientFd);

			if (position < 0) {
				_info("Too many clients. Disconnecting...");
				close(clientFd);
				remove_client_fd(state, clientFd);
			}

			int status = fcntl(clientFd, F_SETFL, O_NONBLOCK);
			DIE(status, "Failed to set non blocking mode for client socket.");

			if (state->on_client_connect) {
				state->on_client_connect(state, state->client_state + position);
			}
		} else {
			//Client wrote something or disconnected
			for (int i = 0; i < MAX_CLIENTS; ++i) {
				if (FD_ISSET(state->client_state[i].fd, &readFdSet)) {
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
				if (FD_ISSET(state->client_state[i].fd, &writeFdSet)) {
					ClientState *cli_state = state->client_state + i;
					int status = handle_client_read(state, cli_state);
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
	addr.sin_port = htons(state->port);

	status = bind(sock, (struct sockaddr*) &addr, sizeof(addr));

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

int schedule_read(ClientState *cstate, char *buffer, int length) {
	if (cstate->fd <= 0) {
		return -1;
	}
	if (cstate->read_write_flag != RW_STATE_NONE) {
		return -2;
	}
	cstate->buffer = buffer;
	cstate->length = length;
	cstate->completed = 0;
	cstate->read_write_flag = RW_STATE_READ;

	return 0;
}

int schedule_write(ClientState *cstate, char *buffer, int length) {
	if (cstate->fd <= 0) {
		return -1;
	}
	if (cstate->read_write_flag != RW_STATE_NONE) {
		return -2;
	}
	cstate->buffer = buffer;
	cstate->length = length;
	cstate->completed = 0;
	cstate->read_write_flag = RW_STATE_WRITE;

	return 0;
}

void cancel_read_write(ClientState *cstate) {
	cstate->buffer = NULL;
	cstate->length = 0;
	cstate->completed = 0;
	cstate->read_write_flag = RW_STATE_NONE;
}
