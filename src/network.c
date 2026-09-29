#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include "network.h"


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
            }
        }
    }

    close(epfd);
    return 0;
}