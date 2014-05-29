#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "socket-framework.h"

typedef enum {
	STATE_NONE,
	STATE_READING_PROTOCOL_LINE,
	STATE_PROTOCOL_DONE,
	STATE_READING_HEADER,
	STATE_HEADER_DONE,
	STATE_READING_BODY,
	STATE_DONE
} ParseState;

typedef struct _HTTPState {
	ParseState parse_state;
	char buffer[1024];
	int position;
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

	cli_state->data = httpState;
}

void on_disconnect(ServerState *state, ClientState *cli_state) {
	_info("Client disconnected %d", cli_state->fd);

	HTTPState *httpState = (HTTPState*) cli_state->data;

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
				state->parse_state = STATE_DONE;
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

void on_client_write(ServerState *state, ClientState *cli_state, char *buff, int length) {
	HTTPState *httpState = (HTTPState*) cli_state->data;

	for (int i = 0; i < length; ++i) {
		stuff_request_byte(httpState, buff[i]);

		if (httpState->parse_state == STATE_PROTOCOL_DONE) {
			char *str = "HTTP/1.1 200 OK\r\nContext-Type: text/plain\r\n\r\n";
			write(cli_state->fd, str, strlen(str));
			httpState->parse_state = STATE_READING_HEADER;
		} else if (httpState->parse_state == STATE_HEADER_DONE) {
			write(cli_state->fd, httpState->buffer, strlen(httpState->buffer));
			httpState->parse_state = STATE_READING_HEADER;
		} else if (httpState->parse_state == STATE_DONE) {
			//Send response
			disconnect_client(state, cli_state);
		}
	}
}

int main() {
	ServerState *state = new_server_state(9090);

	state->on_loop_start = init_server;
	state->on_client_connect = on_connect;
	state->on_client_disconnect = on_disconnect;
	state->on_client_write = on_client_write;

	start_server(state);

	delete_server_state(state);
}
