#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>

#include "socket-framework.h"

#define DIE(value, message) if (value < 0) {perror(message); exit(value);}

static int trace_on = 0;

void
_trace(const char* fmt, ...) {
    if (trace_on == 0) {
        return;
    }
    
    va_list ap;
    
    va_start(ap, fmt);
    
    printf("INFO: ");
    vprintf(fmt, ap);
    printf("\n");
}

void
enableTrace(int flag) {
    trace_on = flag;
}

static void reset_client(Client *cstate) {
    _trace("Resetting client: %d", cstate->fd);
    
    cstate->fd = -1;
    cstate->data = NULL;
    cstate->read_write_flag = RW_STATE_NONE;
    cstate->read_buffer = NULL;
    cstate->read_length = 0;
    cstate->read_completed = 0;
    cstate->write_buffer = NULL;
    cstate->write_length = 0;
    cstate->write_completed = 0;
}

void populate_fd_set(EventLoop *loop, fd_set *pReadFdSet, fd_set *pWriteFdSet) {
    FD_ZERO(pReadFdSet);
    FD_ZERO(pWriteFdSet);
    
    for (int j = 0; j < MAX_SERVERS; ++j) {
        Server *state = loop->server_state[j];
        
        if (state != NULL) {
            //Set the server socket
            FD_SET(state->server_socket, pReadFdSet);
            
            //Set the clients
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                int fd = state->client_state[i].fd;
                
                if (fd < 0) {
                    continue;
                }
                
                if (state->client_state[i].read_write_flag & RW_STATE_READ) {
                    FD_SET(fd, pReadFdSet);
                }
                if (state->client_state[i].read_write_flag & RW_STATE_WRITE) {
                    FD_SET(fd, pWriteFdSet);
                }
            }
        }
    }
}

void
disconnect_clients(Server *state) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        int fd = state->client_state[i].fd;
        
        if (fd >= 0) {
            close(fd);
            reset_client(state->client_state + i);
        }
    }
}

int
add_client_fd(Server *state, int fd) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (state->client_state[i].fd < 0) {
            //We have a free slot
            Client *cstate = state->client_state + i;
            
            cstate->fd = fd;
            cstate->data = NULL;
            cstate->read_write_flag = RW_STATE_NONE;
            cstate->read_buffer = NULL;
            cstate->read_length = 0;
            cstate->read_completed = 0;
            cstate->write_buffer = NULL;
            cstate->write_length = 0;
            cstate->write_completed = 0;
            
            return i;
        }
    }
    
    return -1;
}

int
remove_client_fd(Server *state, int fd) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (state->client_state[i].fd == fd) {
            //We found it!
            
            Client *cstate = state->client_state + i;
            
            reset_client(cstate);
            
            return i;
        }
    }
    
    return -1;
}

int
handle_client_write(Server* state, Client *cli_state) {
    if (!(cli_state->read_write_flag & RW_STATE_READ)) {
        _trace("Socket is not trying to read.");
        
        return -1;
    }
    if (cli_state->read_buffer == NULL) {
        _trace("Read buffer not setup.");
        
        return -1;
    }
    if (cli_state->read_length == cli_state->read_completed) {
        _trace("Read was already completed.");
        
        return -1;
    }
    
    char *buffer_start = cli_state->read_buffer + cli_state->read_completed;
    int bytesRead = read(cli_state->fd,
                         buffer_start,
                         cli_state->read_length - cli_state->read_completed);
    
    _trace("Read %d of %d bytes", bytesRead, cli_state->read_length);
    
    if (bytesRead < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
        //Read will block. Not an error.
        _trace("Read block detected.");
        return 0;
    }
    if (bytesRead == 0) {
        //Client has disconnected. We convert that to an error.
        return -1;
    }
    
    cli_state->read_completed += bytesRead;
    
    if (state->on_read) {
        state->on_read(state, cli_state, buffer_start, bytesRead);
    }
    if (cli_state->read_completed == cli_state->read_length) {
        cli_state->read_write_flag = cli_state->read_write_flag & (~RW_STATE_READ);
        
        if (state->on_read_completed) {
            state->on_read_completed(state, cli_state);
        }
    }
    
    return bytesRead;
}

int
handle_client_read(Server* state, Client *cli_state) {
    if (!(cli_state->read_write_flag & RW_STATE_WRITE)) {
        _trace("Socket is not trying to write.");
        
        return -1;
    }
    if (cli_state->write_buffer == NULL) {
        _trace("Write buffer not setup.");
        
        return -1;
    }
    if (cli_state->write_length == cli_state->write_completed) {
        _trace("Write was already completed.");
        
        return -1;
    }
    
    char *buffer_start = cli_state->write_buffer + cli_state->write_completed;
    int bytesWritten = write(cli_state->fd,
                             buffer_start,
                             cli_state->write_length - cli_state->write_completed);
    
    _trace("Written %d of %d bytes", bytesWritten, cli_state->write_length);
    
    if (bytesWritten < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
        //Write will block. Not an error.
        _trace("Write block detected.");
        
        return 0;
    }
    if (bytesWritten == 0) {
        //Client has disconnected. We convert that to an error.
        return -1;
    }
    
    cli_state->write_completed += bytesWritten;
    
    if (state->on_write) {
        state->on_write(state, cli_state, buffer_start, bytesWritten);
    }
    
    if (cli_state->write_completed == cli_state->write_length) {
        cli_state->read_write_flag &= ~RW_STATE_WRITE;
        
        if (state->on_write_completed) {
            state->on_write_completed(state, cli_state);
        }
    }
    
    return bytesWritten;
}

void
serverDisconnect(Server *state, Client *cli_state) {
    if (state->on_client_disconnect) {
        state->on_client_disconnect(state, cli_state);
    }
    close(cli_state->fd);
    remove_client_fd(state, cli_state->fd);
}

void dispatch_event(Server *state, fd_set *readFdSet, fd_set *writeFdSet) {
    //Make sense out of the event
    if (FD_ISSET(state->server_socket, readFdSet)) {
        _trace("Client is connecting...");
        int clientFd = accept(state->server_socket, NULL, NULL);
        
        DIE(clientFd, "accept() failed.");
        
        int position = add_client_fd(state, clientFd);
        
        if (position < 0) {
            _trace("Too many clients. Disconnecting...");
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
            
            if (state->client_state[i].fd < 0) {
                //This slot is not in use
                continue;
            }
            
            if (FD_ISSET(state->client_state[i].fd, readFdSet)) {
                Client *cli_state = state->client_state + i;
                int status = handle_client_write(state, cli_state);
                if (status < 1) {
                    _trace("Client is finished. Status: %d", status);
                    if (state->on_client_disconnect) {
                        state->on_client_disconnect(state, cli_state);
                    }
                    close(cli_state->fd);
                    remove_client_fd(state, cli_state->fd);
                }
            }
            
            if (state->client_state[i].fd < 0) {
                //Client write event caused application to disconnect.
                continue;
            }
            
            if (FD_ISSET(state->client_state[i].fd, writeFdSet)) {
                Client *cli_state = state->client_state + i;
                int status = handle_client_read(state, cli_state);
                if (status < 1) {
                    _trace("Client is finished. Status: %d", status);
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

void
loopStart(EventLoop *loop) {
    loop->continue_loop = 1;
    
    for (int i = 0; i < MAX_SERVERS; ++i) {
        Server *s = loop->server_state[i];
        
        if (s != NULL && s->on_loop_start != NULL) {
            s->on_loop_start(s);
        }
    }
    
    fd_set readFdSet, writeFdSet;
    struct timeval timeout;
    
    while (loop->continue_loop == 1) {
        populate_fd_set(loop, &readFdSet, &writeFdSet);
                
        timeout.tv_sec = loop->idle_timeout;
        timeout.tv_usec = 0;
        
        int numEvents = select(
                               FD_SETSIZE,
                               &readFdSet,
                               &writeFdSet,
                               NULL,
                               loop->idle_timeout > 0 ? &timeout : NULL);
        
        DIE(numEvents, "select() failed.");
        
        if (numEvents == 0) {
            _trace("select() timed out.");
            for (int i = 0; i < MAX_SERVERS; ++i) {
                Server *s = loop->server_state[i];
                
                if (s != NULL && s->on_timeout != NULL) {
                    s->on_timeout(s);
                }
            }
            
            continue;
        }
        
        for (int i = 0; i < MAX_SERVERS; ++i) {
            Server *s = loop->server_state[i];
            
            if (s != NULL) {
                dispatch_event(s, &readFdSet, &writeFdSet);
            }
        }
    }
}

void
serverStart(Server *state) {
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
    
    _trace("Calling listen.");
    status = listen(sock, 10);
    _trace("listen returned.");
    
    DIE(status, "Failed to listen.");
    
    state->server_socket = sock;
}

Server* newServer(int port) {
    Server *state = (Server*) calloc(1, sizeof(Server));
    
    state->port = port;
    
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        Client *cstate = state->client_state + i;
        
        reset_client(cstate);
    }
    
    return state;
}

void deleteServer(Server *state) {
    disconnect_clients(state);
    
    if (state->server_socket >= 0) {
        close(state->server_socket);
    }
    
    free(state);
}

int clientScheduleRead(Client *cstate, char *buffer, size_t length) {
    assert(cstate->fd >= 0); //Bad socket?
    assert((cstate->read_write_flag & RW_STATE_READ) == 0); //Already reading?
    
    cstate->read_buffer = buffer;
    cstate->read_length = length;
    cstate->read_completed = 0;
    cstate->read_write_flag |= RW_STATE_READ;
    
    _trace("Scheduling read for socket: %d", cstate->fd);
    return 0;
}

int clientScheduleWrite(Client *cstate, char *buffer, size_t length) {
    assert(cstate->fd >= 0); //Bad socket?
    assert((cstate->read_write_flag & RW_STATE_WRITE) == 0); //Already writing?
    
    cstate->write_buffer = buffer;
    cstate->write_length = length;
    cstate->write_completed = 0;
    cstate->read_write_flag |= RW_STATE_WRITE;
    
    _trace("Scheduling write for socket: %d", cstate->fd);
    return 0;
}

void clientCancelRead(Client *cstate) {
    cstate->read_buffer = NULL;
    cstate->read_length = 0;
    cstate->read_completed = 0;
    cstate->read_write_flag &= ~RW_STATE_READ;
    _trace("Cancel read for socket: %d", cstate->fd);
}
void clientCancelWrite(Client *cstate) {
    cstate->write_buffer = NULL;
    cstate->write_length = 0;
    cstate->write_completed = 0;
    cstate->read_write_flag &= ~RW_STATE_WRITE;
    _trace("Cancel write for socket: %d", cstate->fd);
}

void loopInit(EventLoop *loop) {
    for (int i = 0; i < MAX_SERVERS; ++i) {
        loop->server_state[i] = NULL;
    }
    
    loop->continue_loop = 0;
    loop->idle_timeout = 0;
}

int loopAddServer(EventLoop *loop, Server *state) {
    assert(state->server_socket >= 0);
    
    for (int i = 0; i < MAX_SERVERS; ++i) {
        if (loop->server_state[i] == NULL) {
            loop->server_state[i] = state;
            
            return 0;
        }
    }
    
    return -1;
}

int loopRemoveServer(EventLoop *loop, Server *state) {
    for (int i = 0; i < MAX_SERVERS; ++i) {
        if (loop->server_state[i] == state) {
            loop->server_state[i] = NULL;
            
            return 0;
        }
    }
    
    return -1;
}

void loopEnd(EventLoop *loop) {
    loop->continue_loop = 0;
}
