#include <stdio.h>
#include <string.h>

#include "socket-framework.h"

char read_buff[128], write_buff[128];

void
on_write_completed(Client *cstate) {
	clientScheduleRead(cstate, read_buff, sizeof(read_buff) - 1);
}

void
on_read_completed(Client *cstate) {
	clientScheduleRead(cstate, read_buff, sizeof(read_buff) - 1);
}

void
on_read(Client *cstate, char *buff, int length) {
	buff[length] = '\0';

	printf("%s", buff);
}

int
main(int argc, char **argv) {
	if (argc < 2) {
		puts("Usage: test_client host");
		return 1;
	}
	Client *cstate = newClient(argv[1], 80);
	if (cstate == NULL) {
		return -1;
	}

	cstate->on_write_completed = on_write_completed;
	cstate->on_read_completed = on_read_completed;
	cstate->on_read = on_read;

        char *req = "GET / HTTP/1.1\r\n"
                "Host: *\r\n"
                "Accept: */*\r\n"
                "\r\n";

	clientScheduleWrite(cstate, req, strlen(req));

	clientLoop(cstate);

	deleteClient(cstate);

	return 0;
}
