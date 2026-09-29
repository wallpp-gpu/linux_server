#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include "network.h"

static int handle_command(const char *buffer, CommandQueue *queue, Device *device, int client_fd)
{
    ssize_t sent;
    
    printf("received:%s\n", buffer);

    if(strcmp(buffer, "start") == 0){

        Command command;
        command.type = CMD_START;
        command.value = 0;
        if(command_queue_push(queue, command) < 0){
            const char *reply = "command queue full\n";
            sent = send(client_fd, reply, strlen(reply), 0);
        }
        else{
            const char *reply = "start queue\n";
            sent = send(client_fd, reply, strlen(reply), 0);
        }

    }
    else if(strcmp(buffer, "stop") == 0){

        Command command;
        command.type = CMD_STOP;
        command.value = 0;
        if(command_queue_push(queue, command) < 0){
            const char *reply = "command queue full\n";
            sent = send(client_fd, reply, strlen(reply), 0);
        }
        else{
            const char *reply = "stop queue\n";
            sent = send(client_fd, reply, strlen(reply), 0);
        }

    }
    else if(strcmp(buffer, "status") == 0){
        char reply[128];
        const char *status_str;

        Device_status status;
        int speed;

        pthread_mutex_lock(&device->mutex);            //临界区越短越好

        status = device->status;
        speed = device->speed;

        pthread_mutex_unlock(&device->mutex);
        
        if(status == DEVICE_RUNNING){
            status_str = "running";
        }
        else if(status == DEVICE_ERROR){
            status_str = "error";
        }
        else{
            status_str = "idle";
        }
        snprintf(reply, sizeof(reply), "status=%s speed=%d\n", status_str, speed);
        sent = send(client_fd, reply, strlen(reply), 0);        //回复reply，不能用device->mutex去做
    }

    else{
        int new_speed;

        if(sscanf(buffer, "set_speed = %d", &new_speed) == 1){
            if(new_speed < 0 || new_speed > 100){
                const char *reply = "invalid speed\n";
                sent = send(client_fd, reply, strlen(reply), 0);        //回复reply
            }
            else{
                Command command;
                command.type = CMD_SET_SPEED;
                command.value = new_speed;

                if(command_queue_push(queue, command) < 0){
                    const char *reply = "command queue full\n";
                    sent = send(client_fd, reply, strlen(reply), 0);
                }
                else{
                    const char *reply = "set speed queue\n";
                    sent = send(client_fd, reply, strlen(reply), 0);
                }
            }
        }
        else{
            const char *reply = "unknown command\n";
            sent = send(client_fd, reply, strlen(reply), 0);        //回复reply
        }
    }
    if(sent < 0){
        perror("send");
        return -1;
    }
    return 0;
}

static int process_client_data(ClientBuffer *client_buf, const char *data, size_t data_len, CommandQueue *queue, Device *device, int client_fd)
{
    if(client_buf->length + data_len >= sizeof(client_buf->buffer) ){
        printf("buffer overflow,closing connection.\n");
        return -1;
    }
    memcpy(client_buf->buffer + client_buf->length, data, data_len);       //将新数据拷贝到缓冲区末尾
    client_buf->length += data_len;                      //更新缓冲区长度

    size_t start = 0;
    
    for(size_t i = 0; i < client_buf->length; i++){
        if(client_buf->buffer[i] == '\n'){
            client_buf->buffer[i] = '\0';

            char *commend = client_buf->buffer + start;   //获取命令字符串的起始位置
            handle_command(commend, queue, device, client_fd);
            start = i + 1;                           //更新start位置，指向下一个命令的起始位置
        }
    }
    if(start > 0){                               //保存剩余数据
            size_t remaining = client_buf->length - start;        //计算剩余数据长度
            memmove(client_buf->buffer, client_buf->buffer + start, remaining);
            client_buf->length = remaining;
    }
    
    return 0;
}

int set_nonblocking(int fd)
{
    int flags;
    flags = fcntl(fd, F_GETFL, 0);                  //拿到原来标志位
    if(flags < 0){
        perror("fcntl F_GETFL");
        return -1;
    }
    if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0){  //设置为非阻塞模式
        perror("fcntl F_SETFL");
        return -1;
    }

    return 0;
}

int network_create_server(int port)
{
    int server_fd;

    struct sockaddr_in server_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if(server_fd < 0){
        perror("socket");
        return -1;
    }

    if(set_nonblocking(server_fd) < 0){
        close(server_fd);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;               //IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;       //本地任意IP
    server_addr.sin_port = htons(port);
    if(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        perror("bind");
        close(server_fd);
        return -1;
    }

    if(listen(server_fd, 5) < 0){
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

int network_run(int server_fd, CommandQueue *command_queue, Device *device)
{
    int epfd;

    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];  //用于存储就绪事件的数组

    ClientBuffer client_buf[MAX_CLIENTS];

    memset(client_buf, 0, sizeof(client_buf));

    epfd = epoll_create1(0);               //创建epoll实例

    if(epfd < 0){
        perror("epfd_create1");
        return -1;
    }

    ev.events = EPOLLIN | EPOLLET;         //设置监听事件类型为可读
    ev.data.fd = server_fd;                //设置监听的文件描述符为server_fd

    if(epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) < 0){    //将server_fd添加到epoll实例中
        perror("epoll_ctl");
        close(epfd);
        return -1;
    }

    while(g_running){
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if(n < 0){
            if(errno == EINTR){            //睡眠被打断，返回-1，信号与阻塞系统调用之间的关系
                continue;
            }
            perror("epoll_wait");
            break;
        }
        for(int i = 0; i < n; i++){
            int fd = events[i].data.fd;

            if(fd == server_fd){
                while (1){
                    struct sockaddr_in client_addr;

                    socklen_t client_len = sizeof(client_addr);

                    int client_fd = accept(server_fd, (struct sockaddr  *)&client_addr, &client_len);
                    if(client_fd < 0){
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            break;
                        }
                        perror("accept");
                        break;
                    }
                    if(client_fd >= MAX_CLIENTS){
                        printf("client_fd too large\n");
                        close(client_fd);
                        continue;
                    }
                    if(set_nonblocking(client_fd) < 0){
                        close(client_fd);
                        continue;
                    }
                    client_buf[client_fd].length = 0;
                    printf("client_fd = %d\n", client_fd);
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = client_fd;

                    if(epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) < 0){
                        perror("epoll_ctl client_fd");
                        close(client_fd);
                        continue;
                    }

                }
                
            }
            else{
                while(1){
                    char buffer[1024];
                    ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);
                    if(n < 0){
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            //非阻塞模式下，没有数据可读
                            break;
                        }
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                        if(fd < MAX_CLIENTS){
                            client_buf[fd].length = 0;
                        }
                        break;
                    }
                    else if(n == 0){
                        printf("client %d disconnect.\n", fd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);

                        if(fd < MAX_CLIENTS){
                            client_buf[fd].length = 0; // Reset the buffer length for this client
                        }

                        break;
                    }
                    else{
                        if(fd >= MAX_CLIENTS){
                            printf("fd too large,fd = %d.\n", fd);
                            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                            close(fd);
                            break;
                        }

                        if(process_client_data(&client_buf[fd], buffer, n, command_queue, device, fd) < 0){
                            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                            close(fd);
                            client_buf[fd].length = 0; // Reset the buffer length for this client
                            break;
                        }
                    }
                }
            }
        }
    }

    close(epfd);
    return 0;
}