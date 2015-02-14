#define MAX_CLIENTS 5

#define RW_STATE_NONE 0
#define RW_STATE_READ 2
#define RW_STATE_WRITE 4

typedef struct _Client {
	int fd;

	char *read_buffer;
	size_t read_length;
	size_t read_completed;
	char *write_buffer;
	size_t write_length;
	size_t write_completed;

	char host[128];
	int port;

	int read_write_flag;
	void *data;
	int is_connected;

        void (*on_server_connect)(struct _Client* client_state);
        void (*on_server_disconnect)(struct _Client *client_state);
        void (*on_read)(struct _Client *client_state, char* buffer, size_t length);
        void (*on_write)(struct _Client *client_state, char* buffer, size_t length);
        void (*on_read_completed)(struct _Client *client_state);
        void (*on_write_completed)(struct _Client *client_state);
} Client;

typedef struct _Server {
	Client client_state[MAX_CLIENTS];
	int port;
	int server_socket;

	void (*on_loop_start)(struct _Server* state);
	void (*on_loop_end)(struct _Server* state);
	void (*on_client_connect)(struct _Server* state, Client* client_state);
	void (*on_client_disconnect)(struct _Server* state, Client *client_state);
	void (*on_read)(struct _Server* state, Client *client_state, char* buffer, size_t length);
	void (*on_write)(struct _Server* state, Client *client_state, char* buffer, size_t length);
	void (*on_read_completed)(struct _Server* state, Client *client_state);
	void (*on_write_completed)(struct _Server* state, Client *client_state);
} Server;

void _info(const char* fmt, ...);

Server *newServer(int port);
void serverStart(Server* state);
void deleteServer(Server *state);
void serverDisconnect(Server *state, Client *cli_state);
int clientScheduleRead(Client *cli_state, char *buffer, size_t length);
int clientScheduleWrite(Client *cli_state, char *buffer, size_t length);
void clientCancelRead(Client *cstate);
void clientCancelWrite(Client *cstate);
void clientLoop(Client *cstate);
Client* newClient(const char *host, int port);
int clientMakeConnection(Client *cstate);
void deleteClient(Client *cstate);
