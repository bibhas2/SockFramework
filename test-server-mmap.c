#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>

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
	int file;
	off_t file_size;
	void *file_map;
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
	httpState->file = -1;
	httpState->file_map = NULL;

	cli_state->data = httpState;

	//Start reading request
	int status = schedule_read(cli_state, httpState->io_buffer, 
		sizeof(httpState->io_buffer));
	assert(status == 0);
}

void on_disconnect(ServerState *state, ClientState *cli_state) {
	_info("Client disconnected %d", cli_state->fd);

	HTTPState *httpState = (HTTPState*) cli_state->data;

	if (httpState->file_map != NULL) {
		_info("Unmapping file.");
		int status = munmap(httpState->file_map, httpState->file_size);
		assert(status == 0);
		httpState->file_map = NULL;
	}
	if (httpState->file > 0) {
		_info("Closing file.");
		close(httpState->file);
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
	httpState->file_size = st.st_size;
	httpState->parse_state = WRITING_RESPONSE_HEADER;
	schedule_write(cli_state, str, strlen(str));
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
			cancel_read_write(cli_state);
			start_response(cli_state);
		}
	}
}

void on_write_completed(ServerState *state, ClientState *cli_state) {
	HTTPState *httpState = (HTTPState*) cli_state->data;

	if (httpState->parse_state == WRITING_RESPONSE_HEADER) {
		httpState->file = open("image.png", O_RDONLY);
		assert(httpState->file > 0);
		httpState->file_map = mmap(
			NULL, httpState->file_size, 
			PROT_READ, MAP_SHARED,
			httpState->file, 0);
		assert(httpState->file_map != MAP_FAILED);
		httpState->parse_state = WRITING_RESPONSE_BODY;
		_info("Dumping mmap buffer.");
		schedule_write(cli_state, httpState->file_map, 
			httpState->file_size);
	} else if (httpState->parse_state == WRITING_RESPONSE_BODY) {
		_info("Done writing response. Disconnecting...");
		httpState->parse_state = RESPONSE_COMPLETED;
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
