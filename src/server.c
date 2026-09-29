#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <termios.h>

#include <signal.h>
#include <time.h>

#include "system_monitor.h"
#include "app.h"
#include "device.h"
#include "uart.h"
#include "command.h"
#include "logger.h"
#include "network.h"

#define PORT 8888
#define MAX_EVENTS 64

volatile sig_atomic_t g_running = 1;

void handle_signal(int sig){               //收到SIGINT或SIGTERM，g_running = 0
    (void) sig;

    g_running = 0;
}

int handle_command(const char *buffer, CommandQueue *queue, Device *device, int client_fd)
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

int process_client_data(ClientBuffer *client_buf, const char *data, size_t data_len, CommandQueue *queue, Device *device, int client_fd)
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

int main(void)
{
    signal(SIGINT, handle_signal);              //提前声明ctrl+c，执行handle_signal,不按默认方式结束
    signal(SIGTERM, handle_signal);

    // UartDeviceData uart_data;
    // if(uart_init(&uart_data, "/dev/tty0") < 0){
    //     fprintf(stderr, "uart init failed\n");
    //     return 1;
    // }

    Device device;
    if(device_init(&device, &virtual_device_ops,NULL) < 0){
        fprintf(stderr, "device init failed\n");
        //uart_destroy(&uart_data);
        return 1;
    }

    int server_fd;                          //创建文件描述符
    int client_fd;
    int epfd;
                                                                                                         
    struct sockaddr_in client_addr;

    ClientBuffer client_buf[MAX_CLIENTS];  //为每个客户端维护一个缓冲区
    memset(client_buf, 0, sizeof(client_buf)); //初始化缓冲区

    socklen_t client_addr_len;

    CommandQueue queue;
    command_queue_init(&queue);  //初始化命令队列

    LogQueue log_queue;
    log_queue_init(&log_queue);  //初始化日志队列

    LoggerThreadArgs logger_agrs;
    logger_agrs.queue = &log_queue;
    logger_agrs.log_path = "logs/server.log";

    ControlThreadArgs control_args;          //线程参数
    control_args.queue = &queue;
    control_args.device = &device;
    control_args.log_queue = &log_queue;

    server_fd = network_create_server(PORT);         //创建server
    if(server_fd < 0){
        fprintf(stderr, "network server init failed\n");
        return 1;
    }

    epfd =epoll_create1(0);                        //创建epoll实例
    if(epfd < 0){
        perror("epoll_create1");
        close(server_fd);
        return 1;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;                           //设置监听事件类型为可读
    ev.data.fd = server_fd;                        //设置监听的文件描述符为server_fd

    struct epoll_event events[MAX_EVENTS];          //用于存储就绪事件的数组

    if( epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) < 0 ){         //将server_fd添加到epoll实例中
        perror("epoll_ctl");
        close(server_fd);
        close(epfd);
        return 1;
    }


    pthread_t control_tid;
    if(pthread_create(&control_tid, NULL, control_thread, &control_args) != 0){
        perror("pthread_create");
        close(epfd);
        close(server_fd);
        return 1;
    }

    pthread_t logger_tid;
    if(pthread_create(&logger_tid, NULL, logger_thread, &logger_agrs) != 0){
        perror("pthread_create logger");
        return 1;
    }

    pthread_t status_tid;
    int ret = pthread_create(&status_tid, NULL, status_thread, &device);
    if(ret != 0){
        fprintf(stderr, "%s\n", strerror(ret));
    }

    // pthread_t uart_rx_tid;
    // ret = pthread_create(&uart_rx_tid, NULL, uart_rx_thread, &uart_data);
    // if(ret != 0){
    //     fprintf(stderr, "%s\n", strerror(ret));
    //     return 1;
    // }

    while(g_running){
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if(n < 0){
            if(errno == EINTR){                    //睡眠被打断，返回-1，信号与阻塞系统调用之间的关系
                continue;
            }
            perror("epoll_wait");
            break;
        }

        for(int i = 0; i < n; i++){
            int fd = events[i].data.fd;

            if(fd == server_fd){
                printf("wait client.\n");

                while (1)
                {
                    client_addr_len = sizeof(client_addr);
                    //用accept创建client_fd,负责通信，server收到连接
                    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

                    if(client_fd < 0){
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            //非阻塞模式下，没有连接请求
                            break;
                        }
                        perror("accept");
                        break;
                    }

                    if(set_nonblocking(client_fd) < 0){
                        close(client_fd);
                        continue;
                    }

                    if(client_fd < MAX_CLIENTS){
                        client_buf[client_fd].length = 0;
                    }
                    else{
                        printf("client_fd too large,client_fd = %d.\n", client_fd);
                        close(client_fd);
                        continue;
                    }

                    printf("client connect, client_fd = %d.\n", client_fd);
                    ev.events = EPOLLIN | EPOLLET;                           //设置监听事件类型为可读
                    ev.data.fd = client_fd;                        //设置监听的文件描述符为client_fd

                    if( epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) < 0 ){
                        perror("epoll_ctl");
                        close(client_fd);
                        continue;
                    }
                }
            }

            else{
                while (1)
                {
                    char buffer[1024];
                    ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);

                    if(n < 0){
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            //非阻塞模式下，没有数据可读
                            break;
                        }
                        perror("recv");
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);

                        if(fd < MAX_CLIENTS){
                            client_buf[fd].length = 0; // Reset the buffer length for this client
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

                        if(process_client_data(&client_buf[fd], buffer, n, &queue, &device, fd) < 0){
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

    printf("\n[MAIN] shitting down...\n");

    pthread_mutex_lock(&queue.mutex);
    pthread_cond_broadcast(&queue.cond);          //唤醒
    pthread_mutex_unlock(&queue.mutex);

    pthread_mutex_lock(&log_queue.mutex);
    pthread_cond_broadcast(&log_queue.cond);      //释放cond
    pthread_mutex_unlock(&log_queue.mutex);

    pthread_join(control_tid, NULL);              //主线程在这里等待线程自己结束，pthread_join != 结束线程
    pthread_join(logger_tid, NULL);
    pthread_join(status_tid, NULL);
    // pthread_join(uart_rx_tid, NULL);

    // close(uart_data.fd);
    close(epfd);
    close(server_fd);

    device_destroy(&device);
    command_queue_destroy(&queue);
    log_queue_destroy(&log_queue);
    // uart_destroy(&uart_data);

    printf("[MAIN] shutdown complete\n");

    return 0;
}