#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

#include "socket-framework.h"

#define _info printf
#define HTTP_PORT 9090

typedef enum {
	STATE_NONE,
	STATE_READ_HEADER,
	WRITE_RESPONSE_HEADER,
	WRITE_RESPONSE_BODY
} ParseState;

typedef struct _HTTPState {
	ParseState parse_state;
	char verb[128];
	char file_name[1024];
	char write_buffer[1024];
	char read_buffer[1024];
	FILE *file;
} HTTPState;

void
init_server(Server* state) {
	_info("Server listening on %d.\n", HTTP_PORT);
}

void on_connect(Server *state, Client *cli_state) {
	_info("Client connected %d\n", cli_state->fd);
	HTTPState *httpState = (HTTPState*) malloc(sizeof(HTTPState));

	httpState->file = NULL;

	cli_state->data = httpState;

	//Start reading protocol line
	httpState->parse_state = STATE_READ_HEADER;
	memset(httpState->read_buffer, '\0', sizeof(httpState->read_buffer));
	int status = clientScheduleRead(cli_state, httpState->read_buffer, 
		sizeof(httpState->read_buffer));

	assert(status == 0);
}

void on_disconnect(Server *state, Client *cli_state) {
	_info("Client disconnected %d\n", cli_state->fd);

	HTTPState *httpState = (HTTPState*) cli_state->data;

	if (httpState->file != NULL) {
		fclose(httpState->file);

		httpState->file = NULL;
	}

	free(httpState);
}

void
write_to_client(HTTPState *http, Client *cli_state, char* line) {
	char *end = stpncpy(http->write_buffer, line, sizeof(http->write_buffer));

	clientScheduleWrite(cli_state, http->write_buffer, end - http->write_buffer);
}

void 
transfer_file_data(Server *state, Client *cli_state) {
	HTTPState *httpState = (HTTPState*) cli_state->data;

	assert(httpState->file != NULL);
	assert(httpState->parse_state == WRITE_RESPONSE_BODY);

	int length = fread(httpState->write_buffer, 1, 
		sizeof(httpState->write_buffer), httpState->file);

	if (length > 0) {
		clientScheduleWrite(cli_state, httpState->write_buffer, length);
	} else if (feof(httpState->file)) {
		//We are done writing
		fclose(httpState->file);
		httpState->file = NULL;

		//Allow the client to send another request.
		httpState->parse_state = STATE_READ_HEADER;
	} else {
		perror("File read failed.");
		serverDisconnect(state, cli_state);
	}
}

void on_read(Server *state, Client *cli_state, char *buff, size_t length) {
	HTTPState *httpState = (HTTPState*) cli_state->data;

	if (httpState->parse_state == STATE_READ_HEADER) {
		//See if we got the verb and file path.
		//Protocol line will have two spaces: GET /index.php HTTP/1.1
		int space_count = 0;

		for (size_t i = 0; i < cli_state->read_completed; ++i) {
			if (httpState->read_buffer[i] == ' ') {
				space_count += 1;
			}

			if (space_count == 2) {
				char tmp[1024];
				
				sscanf(httpState->read_buffer, "%s %s", httpState->verb, tmp);
				sprintf(httpState->file_name, ".%s", tmp);

				clientCancelRead(cli_state);

				_info("Request verb: %s path: %s\n", httpState->verb, httpState->file_name);

				httpState->file = fopen(httpState->file_name, "r");
				httpState->parse_state = WRITE_RESPONSE_HEADER;

				if (httpState->file == NULL) {
					write_to_client(httpState, cli_state, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
				} else {
					struct stat st;

					int status = stat(httpState->file_name, &st);

					if (status == 0) {
						sprintf(tmp, "HTTP/1.1 200 OK\r\nContent-Length: %lld\r\n\r\n", st.st_size);
					} else {
						perror("Can not stat file.");

						strcpy(tmp, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");

						fclose(httpState->file);
						httpState->file = NULL;
					}

					write_to_client(httpState, cli_state, tmp);
				}

				break;
			}
		}
	}
}

void on_write_completed(Server *state, Client *cli_state) {
	HTTPState *httpState = (HTTPState*) cli_state->data;

	if (httpState->parse_state == WRITE_RESPONSE_HEADER) {
		if (httpState->file != NULL) {
			//Initiate file transfer
			httpState->parse_state = WRITE_RESPONSE_BODY;

			transfer_file_data(state, cli_state);
		}
	} else if (httpState->parse_state == WRITE_RESPONSE_BODY) {
		if (httpState->file != NULL) {
			//Continue file transfer
			transfer_file_data(state, cli_state);
		}
	}
}

void on_read_completed(Server *state, Client *cli_state) {
	HTTPState *httpState = (HTTPState*) cli_state->data;

	//If we are still parsing request then request is too long
	if (httpState->parse_state == STATE_READ_HEADER) {
		_info("Request larger than %ld\n", sizeof(httpState->read_buffer));

		serverDisconnect(state, cli_state);
	}
}

int main() {
	Server *state = newServer(HTTP_PORT);

	state->on_loop_start = init_server;
	state->on_client_connect = on_connect;
	state->on_client_disconnect = on_disconnect;
	state->on_read = on_read;
	state->on_read_completed = on_read_completed;
	//state->on_write = on_write;
	state->on_write_completed = on_write_completed;

	serverStart(state);
    
    EventLoop loop;
    
    loopInit(&loop);
    
    loopAddServer(&loop, state);
    
    loopStart(&loop);

	deleteServer(state);
}
