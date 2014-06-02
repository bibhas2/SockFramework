#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

#include "socket-framework.h"

typedef enum {
	STATE_NONE,
	STATE_READING_PROTOCOL_LINE,
	STATE_PROTOCOL_DONE,
	STATE_READING_HEADER,
	STATE_HEADER_DONE,
	STATE_READING_BODY,
	HEADER_READ_COMPLETED,
	WRITING_RESPONSE_HEADER,
	WRITING_RESPONSE_BODY,
	RESPONSE_COMPLETED
} ParseState;

typedef struct _HTTPState {
	ParseState parse_state;
	char buffer[1024];
	char io_buffer[1024];
	int position;
	FILE *file;
} HTTPState;

void
init_server(ServerState* state) {
	_info("Server loop is starting");
}

void on_connect(ServerState *state, ClientState *cli_state) {
	_info("Client connected %d", cli_state->fd);
	HTTPState *httpState = (HTTPState*) malloc(sizeof(HTTPState));
	httpState->parse_state = STATE_READING_PROTOCOL_LINE;
	httpState->position = 0;
	httpState->file = NULL;

	cli_state->data = httpState;

	//Start reading request
	int status = schedule_read(cli_state, httpState->io_buffer, 
		sizeof(httpState->io_buffer));
	assert(status == 0);
}

void on_disconnect(ServerState *state, ClientState *cli_state) {
	_info("Client disconnected %d", cli_state->fd);

	HTTPState *httpState = (HTTPState*) cli_state->data;

	if (httpState->file != NULL) {
		fclose(httpState->file);
		httpState->file = NULL;
	}

	free(httpState);
}

void
stuff_request_byte(HTTPState *state, char c) {
	if (state->parse_state == STATE_READING_PROTOCOL_LINE) {
		if (c == '\r') {
		} else if (c == '\n') {
			state->buffer[state->position] = '\0';
			_info("Protocol line [%s]", state->buffer);
			state->parse_state = STATE_PROTOCOL_DONE;
			state->position = 0;
		} else {
			state->buffer[state->position] = c;
			state->position += 1;
		}	
	} else if (state->parse_state == STATE_READING_HEADER) {
		if (c == '\r') {
		} else if (c == '\n') {
			state->buffer[state->position] = '\0';
			_info("Header line [%s]", state->buffer);
			if (state->position == 0) {
				state->parse_state = HEADER_READ_COMPLETED;
			} else {
				state->parse_state = STATE_HEADER_DONE;
			}
			state->position = 0;
		} else {
			state->buffer[state->position] = c;
			state->position += 1;
		}	
	}
}


void
start_response(ClientState *cli_state) {
	struct stat st;

	int status = stat("image.png", &st);
	assert(status == 0);

	char str[1024];
	sprintf(str, "HTTP/1.1 200 OK\r\nContent-Length: %lld\r\n\r\n",
		st.st_size);
	
	HTTPState *httpState = (HTTPState*) cli_state->data;
	httpState->parse_state = WRITING_RESPONSE_HEADER;
	schedule_write(cli_state, str, strlen(str));
}

void 
transfer_file_data(ClientState *cli_state) {
	HTTPState *httpState = (HTTPState*) cli_state->data;
	int length = fread(httpState->io_buffer, 1, 
		sizeof(httpState->io_buffer), httpState->file);
	if (length > 0) {
		httpState->parse_state = WRITING_RESPONSE_BODY;
		schedule_write(cli_state, httpState->io_buffer, length);
	}

	if (length < sizeof(httpState->io_buffer)) {
		//We are done reading
		httpState->parse_state = RESPONSE_COMPLETED;
	}
}

void on_read(ServerState *state, ClientState *cli_state, char *buff, int length) {
	HTTPState *httpState = (HTTPState*) cli_state->data;

	for (int i = 0; i < length; ++i) {
		stuff_request_byte(httpState, buff[i]);

		if (httpState->parse_state == STATE_PROTOCOL_DONE) {
			httpState->parse_state = STATE_READING_HEADER;
		} else if (httpState->parse_state == STATE_HEADER_DONE) {
			httpState->parse_state = STATE_READING_HEADER;
		} else if (httpState->parse_state == HEADER_READ_COMPLETED) {
			//Send response
			cancel_read(cli_state);
			start_response(cli_state);
		}
	}
}

void on_write_completed(ServerState *state, ClientState *cli_state) {
	HTTPState *httpState = (HTTPState*) cli_state->data;

	if (httpState->parse_state == WRITING_RESPONSE_HEADER) {
		httpState->file = fopen("image.png", "r");
		assert(httpState->file != NULL);
		transfer_file_data(cli_state);
	} else if (httpState->parse_state == WRITING_RESPONSE_BODY) {
		transfer_file_data(cli_state);
	} else if (httpState->parse_state == RESPONSE_COMPLETED) {
		_info("Done writing response. Disconnecting...");
		//We are done. Disconnect.
		disconnect_client(state, cli_state);
	}
}

void on_read_completed(ServerState *state, ClientState *cli_state) {
	//If we are still parsing request, keep reading
	HTTPState *httpState = (HTTPState*) cli_state->data;
	if (httpState->parse_state != HEADER_READ_COMPLETED) {
		int status = schedule_read(cli_state, httpState->io_buffer, 
			sizeof(httpState->io_buffer));
		assert(status == 0);
	}
}

int main() {
	ServerState *state = new_server_state(9090);

	state->on_loop_start = init_server;
	state->on_client_connect = on_connect;
	state->on_client_disconnect = on_disconnect;
	state->on_read = on_read;
	state->on_read_completed = on_read_completed;
	//state->on_write = on_write;
	state->on_write_completed = on_write_completed;

	start_server(state);

	delete_server_state(state);
}
