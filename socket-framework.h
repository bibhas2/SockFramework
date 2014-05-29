#define MAX_CLIENTS 5

typedef struct _ClientState {
	int fd;
	void *data;
} ClientState;

typedef struct _ServerState {
	ClientState client_state[MAX_CLIENTS];
	int port;
	int server_socket;

	void (*on_loop_start)(struct _ServerState* state);
	void (*on_loop_end)(struct _ServerState* state);
	void (*on_client_connect)(struct _ServerState* state, ClientState* client_state);
	void (*on_client_disconnect)(struct _ServerState* state, ClientState *client_state);
	void (*on_client_write)(struct _ServerState* state, ClientState *client_state, char *buffer, int length);
} ServerState;

ServerState *new_server_state(int port);
void start_server(ServerState* state);
void delete_server_state(ServerState *state);
void _info(const char* fmt, ...);
void disconnect_client(ServerState *state, ClientState *cli_state);
