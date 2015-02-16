#define PUMP_MAX_SOCKET 128
#define PUMP_STATUS_STOPPED 0
#define PUMP_STATUS_RUNNING 1
#define PUMP_STATUS_STOP_REQUESTED 2

struct _EventPump;

typedef struct _SocketRec {
	int socket;
	int needToWrite;
	void *data;
	void (*onReadable)
		(struct _EventPump *pump, struct _SocketRec *repeater);
	void (*onWritable)
		(struct _EventPump *pump, struct _SocketRec *repeater);
	void (*onTimeout)
		(struct _EventPump *pump, struct _SocketRec *repeater);
} SocketRec;

typedef struct _EventPump {
	int status;
	time_t timeout;
	int control_pipe[2];
	SocketRec sockets[PUMP_MAX_SOCKET];
} EventPump;

EventPump *newEventPump();
void deleteEventPump(EventPump *pump);
SocketRec *pumpRegisterSocket(EventPump *pump, int socket, void *data);
void *pumpRemoveSocket(EventPump *pump, int socket);
int pumpStart(EventPump *pump);
int pumpStop(EventPump *pump);
