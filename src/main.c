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

volatile sig_atomic_t g_running = 1;

void handle_signal(int sig){               //收到SIGINT或SIGTERM，g_running = 0
    (void) sig;

    g_running = 0;
}

int main(void)
{
    signal(SIGINT, handle_signal);              //提前声明ctrl+c，执行handle_signal,不按默认方式结束
    signal(SIGTERM, handle_signal);

    int server_fd;

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

    pthread_t control_tid;
    int ret = pthread_create(&control_tid, NULL, control_thread, &control_args);
    if(ret != 0){
        fprintf(stderr, "pthread_create control: %s\n", strerror(ret));
    }

    pthread_t logger_tid;
    ret = pthread_create(&logger_tid, NULL, logger_thread, &logger_agrs);
    if(ret != 0){
        fprintf(stderr, "pthread_create logger: %s\n", strerror(ret));
    }

    pthread_t status_tid;
    ret = pthread_create(&status_tid, NULL, status_thread, &device);
    if(ret != 0){
        fprintf(stderr, "pthread_create status: %s\n", strerror(ret));
    }

    // pthread_t uart_rx_tid;
    // ret = pthread_create(&uart_rx_tid, NULL, uart_rx_thread, &uart_data);
    // if(ret != 0){
    //     fprintf(stderr, "%s\n", strerror(ret));
    //     return 1;
    // }

    if(network_run( server_fd, &queue, &device) < 0){     //创建epoll实例
        fprintf(stderr, "network run failed\n");
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
    close(server_fd);

    device_destroy(&device);
    command_queue_destroy(&queue);
    log_queue_destroy(&log_queue);
    // uart_destroy(&uart_data);

    printf("[MAIN] shutdown complete\n");

    return 0;
}