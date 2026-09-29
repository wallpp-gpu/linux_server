#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>
#include "command.h"
#include "device.h"

#define MAX_EVENTS 64
#define MAX_CLIENTS 64
#define CLIENT_BUFFER_SIZE 1024

typedef struct{
    char buffer[CLIENT_BUFFER_SIZE];
    size_t length;
} ClientBuffer;

static int handle_command(const char *buffer, CommandQueue *queue, Device *device, int client_fd);
static int process_client_data(ClientBuffer *client_buf, const char *data, size_t data_len, CommandQueue *queue, Device *device, int client_fd);

int set_nonblocking(int fd);          //设置为非阻塞

int network_create_server(int port);  //创建tcp监听socket

int network_run(int server_fd, CommandQueue *command_queue, Device *device);


#endif