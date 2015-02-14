#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include "socket-framework.h"

#define DIE(value, message) if (value < 0) {perror(message); abort();}

void _info(const char* fmt, ...);

Client*
newClient(const char *host, int port) {
	Client *cstate = NULL;

	cstate = (Client*) calloc(1, sizeof(Client));
	cstate->read_write_flag = RW_STATE_NONE;
	strncpy(cstate->host, host, sizeof(cstate->host));
	cstate->port = port;
	cstate->fd = clientMakeConnection(cstate);;

	return cstate;
}

int clientMakeConnection(Client *cstate) {
	_info("Connecting to %s:%d", cstate->host, cstate->port);

	char port_str[128];

	snprintf(port_str, sizeof(port_str), "%d", cstate->port);

	struct addrinfo hints, *res;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	_info("Resolving name...");
	int status = getaddrinfo(cstate->host, port_str, &hints, &res);
	DIE(status, "Failed to resolve address.");
	if (res == NULL) {
		_info("Failed to resolve address: %s", cstate->host);
		exit(-1);
	}

	int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	DIE(sock, "Failed to open socket.");

	status = fcntl(sock, F_SETFL, O_NONBLOCK);
	DIE(status, "Failed to set non blocking mode for socket.");

	status = connect(sock, res->ai_addr, res->ai_addrlen);
	_info("Asynchronous connection initiated.");
	if (status < 0 && errno != EINPROGRESS) {
		perror("Failed to connect to port.");
		close(sock);

		return -1;
	}

	freeaddrinfo(res);

	return sock;
}

void
deleteClient(Client *cstate) {
	free(cstate);
}

int
handle_server_read(Client *cli_state) {
        if (!(cli_state->read_write_flag & RW_STATE_WRITE)) {
                _info("Socket is not trying to write.");
                return -1;
        }
        if (cli_state->write_buffer == NULL) {
                _info("Write buffer not setup.");
                return -1;
        }
        if (cli_state->write_length == cli_state->write_completed) {
                _info("Write was already completed.");
                return -1;
        }

        char *buffer_start = cli_state->write_buffer + cli_state->write_completed;
        int bytesWritten = write(cli_state->fd,
                buffer_start,
                cli_state->write_length - cli_state->write_completed);
        _info("Written %d of %d bytes", bytesWritten, cli_state->write_length);
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

        cli_state->write_completed += bytesWritten;

        if (cli_state->on_write) {
                cli_state->on_write(cli_state, buffer_start, bytesWritten);
        }
        if (cli_state->write_completed == cli_state->write_length) {
                cli_state->read_write_flag &= ~RW_STATE_WRITE;

                if (cli_state->on_write_completed) {
                        cli_state->on_write_completed(cli_state);
                }
        }

        return bytesWritten;
}

int
handle_server_write(Client *cli_state) {
        if (!(cli_state->read_write_flag & RW_STATE_READ)) {
                _info("Socket is not trying to read.");
                return -1;
        }
        if (cli_state->read_buffer == NULL) {
                _info("Read buffer not setup.");
                return -1;
        }
        if (cli_state->read_length == cli_state->read_completed) {
                _info("Read was already completed.");
                return -1;
        }

        char *buffer_start = cli_state->read_buffer + cli_state->read_completed;
        int bytesRead = read(cli_state->fd,
                buffer_start,
                cli_state->read_length - cli_state->read_completed);

        _info("Read %d of %d bytes", bytesRead, cli_state->read_length);

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

        cli_state->read_completed += bytesRead;

        if (cli_state->on_read) {
                cli_state->on_read(cli_state, buffer_start, bytesRead);
        }
        if (cli_state->read_completed == cli_state->read_length) {
                cli_state->read_write_flag = cli_state->read_write_flag & (~RW_STATE_READ);
                if (cli_state->on_read_completed) {
                        cli_state->on_read_completed(cli_state);
                }
	}

	return bytesRead;
}

void
clientLoop(Client *cstate) {
        fd_set readFdSet, writeFdSet;
        struct timeval timeout;

	while (1) {
		FD_ZERO(&readFdSet);
		FD_ZERO(&writeFdSet);

		if (cstate->read_write_flag & RW_STATE_READ) {
			FD_SET(cstate->fd, &readFdSet);
		}
		if ((cstate->read_write_flag & RW_STATE_WRITE) ||
			(cstate->is_connected == 0)) {
			FD_SET(cstate->fd, &writeFdSet);
		}

                timeout.tv_sec = 10;
                timeout.tv_usec = 0;

                int numEvents = select(FD_SETSIZE, &readFdSet, &writeFdSet, NULL, &timeout);
                DIE(numEvents, "select() failed.");
		if (numEvents == 0) {
			_info("select() timed out.");

                        break;
                }

		if (FD_ISSET(cstate->fd, &readFdSet)) {
			int status = handle_server_write(cstate);
			if (status < 1) {
				close(cstate->fd);
				cstate->fd = -1;
				_info("Server disconnected.");
				if (cstate->on_server_disconnect) {
					cstate->on_server_disconnect(cstate);
				}
				break;
			}
		}
		if (FD_ISSET(cstate->fd, &writeFdSet)) {
			if (cstate->is_connected == 0) {
				//Connection is complete. See if it worked
				int valopt; 
				socklen_t lon = sizeof(int); 
				if (getsockopt(cstate->fd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) { 
					perror("Error in getsockopt()");
					break;
				}
				//Check the value of valopt
				if (valopt) {
					printf("Error connecting to server: %s.\n", strerror(valopt));
					break;
				}
				cstate->is_connected = 1;
				_info("Asynchronous connection completed.");
			} else {
				int status = handle_server_read(cstate);
				if (status < 1) {
					_info("Server disconnected.");
					close(cstate->fd);
					cstate->fd = -1;
					if (cstate->on_server_disconnect) {
						cstate->on_server_disconnect(cstate);
					}
					break;
				}
			}
		}
	}

	if (cstate->fd >= 0) {
		close(cstate->fd);
	}
}
