#define MAX_CLIENTS 5

#define RW_STATE_NONE 0
#define RW_STATE_READ 2
#define RW_STATE_WRITE 4

typedef struct _ClientState {
	int fd;

	char *read_buffer;
	int read_length;
	int read_completed;
	char *write_buffer;
	int write_length;
	int write_completed;

	int read_write_flag;
	void *data;

        void (*on_server_connect)(struct _ClientState* client_state);
        void (*on_server_disconnect)(struct _ClientState *client_state);
        void (*on_read)(struct _ClientState *client_state, char* buffer, int length);
        void (*on_write)(struct _ClientState *client_state, char* buffer, int length);
        void (*on_read_completed)(struct _ClientState *client_state);
        void (*on_write_completed)(struct _ClientState *client_state);
} ClientState;

typedef struct _ServerState {
	ClientState client_state[MAX_CLIENTS];
	int port;
	int server_socket;

	void (*on_loop_start)(struct _ServerState* state);
	void (*on_loop_end)(struct _ServerState* state);
	void (*on_client_connect)(struct _ServerState* state, ClientState* client_state);
	void (*on_client_disconnect)(struct _ServerState* state, ClientState *client_state);
	void (*on_read)(struct _ServerState* state, ClientState *client_state, char* buffer, int length);
	void (*on_write)(struct _ServerState* state, ClientState *client_state, char* buffer, int length);
	void (*on_read_completed)(struct _ServerState* state, ClientState *client_state);
	void (*on_write_completed)(struct _ServerState* state, ClientState *client_state);
} ServerState;

ServerState *new_server_state(int port);
void start_server(ServerState* state);
void delete_server_state(ServerState *state);
void _info(const char* fmt, ...);
void disconnect_client(ServerState *state, ClientState *cli_state);
int schedule_read(ClientState *cli_state, char *buffer, int length);
int schedule_write(ClientState *cli_state, char *buffer, int length);
void cancel_read(ClientState *cstate);
void cancel_write(ClientState *cstate);
void client_loop(ClientState *cstate);
ClientState* new_client_state(const char *host, int port);
void delete_client_state(ClientState *cstate);
