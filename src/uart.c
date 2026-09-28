#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <time.h>

#include "app.h"
#include "uart.h"

int uart_write_all(int fd, const char *buffer, size_t length)         //完整写入函数
{
    size_t total = 0;

    while(total < length){
        ssize_t n = write(fd, buffer + total, length - total);

        if(n < 0){
            if(errno == EINTR){
                continue;
            }

            perror("uart write");
            return -1;
        }

        if(n == 0){
            return -1;
        }

        total +=(size_t)n;
    }

    return 0;
}

 static int uart_configure(int fd)
{
    struct termios tty;

    if(tcgetattr(fd, &tty) != 0){
        perror("tcgetattr");
        return -1;
    }

    cfmakeraw(&tty);

    cfsetispeed(&tty, B115200);        //设置波特率
    cfsetospeed(&tty, B115200);

    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;                 //设置8位

    tty.c_cflag &= ~PARENB;             //设置校验位（无）

    tty.c_cflag &= ~CSTOPB;             //设置一位停止位

    tty.c_cflag &= ~CRTSCTS;            //关闭硬件流控

    tty.c_cflag |= CREAD | CLOCAL;      //允许收发数据

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;               //等待1s

    if(tcsetattr(fd, TCSANOW, &tty) != 0){
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

static int uart_ack_init(UartAck *ack)
{
    if(ack == NULL){
        return -1;
    }

    if(pthread_mutex_init(&ack->mutex, NULL) < 0){
        pthread_mutex_destroy(&ack->mutex);
        return -1;
    }

    if(pthread_cond_init(&ack->cond, NULL) < 0){
        pthread_cond_destroy(&ack->cond);
        return -1;
    }

    ack->received = 0;
    ack->message[0] = '\0';
    return 0;
}

static int uart_wait_ack(UartDeviceData *uart, const char *expected, int timeout_ms)
{
    UartAck *ack = &uart->ack;

    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);

    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;

    if(ts.tv_nsec >= 1000000000L){
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }


    pthread_mutex_lock(&ack->mutex);

    while (!ack->received && g_running){
        int ret = pthread_cond_timedwait(&ack->cond, &ack->mutex, &ts);            //wait会释放互斥锁
        if(ret == ETIMEDOUT){
            pthread_mutex_unlock(&ack->mutex);
            return -1;
        }
        if(ret != 0){
            fprintf(stderr, "pthread_cond_timedwait: %s\n", strerror(ret));
            pthread_mutex_unlock(&ack->mutex);
            return -1;
        }
    }

    if(!g_running){
        pthread_mutex_unlock(&ack->mutex);
        return -1;
    }

    int success = strcmp(ack->message, expected) == 0;

    ack->received =0;

    pthread_mutex_unlock(&ack->mutex);

    return success ? 0: -1 ;
    
}


static int uart_device_start(Device *device)
{
    UartDeviceData *uart = (UartDeviceData *)device->private_data;

    const char *command = "START\n";

    pthread_mutex_lock(&uart->ack.mutex);

    uart->ack.received = 0;
    uart->ack.message[0] = '\0';

    pthread_mutex_unlock(&uart->ack.mutex);

    if(uart_write_all(uart->fd, command, strlen(command)) < 0){   //完整写完
        return -1;
    }

    if(uart_wait_ack(uart, "OK START", 1000) < 0){                      //等待回复
        fprintf(stderr, "[UART] START failed\n");
        pthread_mutex_lock(&device->mutex);
        device->status = DEVICE_ERROR;
        pthread_mutex_unlock(&device->mutex);

        return -1;
    }

    pthread_mutex_lock(&device->mutex);

    device->status = DEVICE_RUNNING;

    pthread_mutex_unlock(&device->mutex);

    return 0;
}

static int uart_device_stop(Device *device)
{
    UartDeviceData *uart = (UartDeviceData *)device->private_data;

    const char *command = "STOP\n";

    pthread_mutex_lock(&uart->ack.mutex);

    uart->ack.received = 0;
    uart->ack.message[0] = '\0';

    pthread_mutex_unlock(&uart->ack.mutex);

    if(uart_write_all(uart->fd, command, strlen(command)) < 0){
        return -1;
    }

    if(uart_wait_ack(uart, "OK STOP", 1000) < 0){                      //等待回复
        fprintf(stderr, "[UART] STOP failed\n");
        pthread_mutex_lock(&device->mutex);
        device->status = DEVICE_ERROR;
        pthread_mutex_unlock(&device->mutex);

        return -1;
    }

    pthread_mutex_lock(&device->mutex);

    device->status = DEVICE_IDLE;
    device->speed = 0;

    pthread_mutex_unlock(&device->mutex);

    return 0;
}

static int uart_device_set_speed(Device *device, int speed)
{
    if(speed < 0 || speed > 100){
        return -1;
    }

    UartDeviceData *uart = (UartDeviceData *)device->private_data;

    char command[64];

    int len = snprintf(command, sizeof(command), "SPEED %d\n", speed);
    
    if(len < 0){
        return -1;
    }

    if((size_t)len >= sizeof(command)){
        return -1;
    }

    pthread_mutex_lock(&uart->ack.mutex);

    uart->ack.received = 0;
    uart->ack.message[0] = '\0';

    pthread_mutex_unlock(&uart->ack.mutex);

    if(uart_write_all(uart->fd, command, strlen(command)) < 0){
        return -1;
    }
    char expected[64];

    int expected_len = snprintf(expected, sizeof(expected), "OK SPEED %d", speed);

    if(expected_len < 0 || (size_t)expected_len >= sizeof(expected)){
        return -1;
    }

    if(uart_wait_ack(uart, expected, 1000) < 0){                      //等待回复
        fprintf(stderr, "[UART] SET SPEED failed\n");
        pthread_mutex_lock(&device->mutex);
        device->status = DEVICE_ERROR;
        pthread_mutex_unlock(&device->mutex);

        return -1;
    }

    pthread_mutex_lock(&device->mutex);

    device->status = DEVICE_RUNNING;
    device->speed  = speed;

    pthread_mutex_unlock(&device->mutex);

    return 0;
}

DeviceOps uart_device_ops = {
    .start = uart_device_start,
    .stop = uart_device_stop,
    .set_speed = uart_device_set_speed
};

static void handle_uart_frame(UartDeviceData *uart, const char *frame)
{
    printf("[UART FRAME] %s\n", frame);

    if(strncmp(frame, "OK ", 3) == 0){
        UartAck *ack = &uart->ack;
        pthread_mutex_lock(&ack->mutex);

        snprintf(ack->message, sizeof(ack->message), "%s", frame);

        ack->received = 1;

        pthread_cond_signal(&ack->cond);

        pthread_mutex_unlock(&ack->mutex);
    }
}

static int process_uart_data(UartDeviceData *uart, const char *data, size_t data_len)
{
    UartRxBuffer *rx_buf = &uart->rx_buffer;

    if(rx_buf->length + data_len >= sizeof(rx_buf->buffer) ){
        fprintf(stderr, "buffer overflow,closing connection.\n");
        rx_buf->length = 0;
        return -1;
    }
    memcpy(rx_buf->buffer + rx_buf->length, data, data_len);       //将新数据拷贝到缓冲区末尾
    rx_buf->length += data_len;                      //更新缓冲区长度

    size_t start = 0;
    
    for(size_t i = 0; i < rx_buf->length; i++){
        if(rx_buf->buffer[i] == '\n'){
            rx_buf->buffer[i] = '\0';

            char *frame = rx_buf->buffer + start;   //获取命令字符串的起始位置

            handle_uart_frame(uart, frame);
            start = i + 1;                           //更新start位置，指向下一个命令的起始位置
        }
    }
    if(start > 0){                               //保存剩余数据
            size_t remaining = rx_buf->length - start;        //计算剩余数据长度
            memmove(rx_buf->buffer, rx_buf->buffer + start, remaining);
            rx_buf->length = remaining;
    }
    
    return 0;
}

void *uart_rx_thread(void *arg)
{
    UartDeviceData *uart = (UartDeviceData *)arg;
    char buffer[256];

    while (g_running)
    {
        ssize_t n = read(uart->fd, buffer, sizeof(buffer) - 1);

        if(n < 0){
            if(errno == EINTR){
                continue;
            }
            perror("uart read");
            break;
        }
        if(n == 0){
            continue;
        }

        if(process_uart_data(uart, buffer, (size_t)n) < 0){
            fprintf(stderr, "[UART] process data failed\n");
        }
    }

    return NULL; 
}

int uart_init(UartDeviceData *uart, const char *device_path)
{
    if(uart == NULL || device_path ==NULL){
        return -1;
    }
    memset(uart, 0, sizeof(UartDeviceData));

    uart->device_path = device_path;

    uart->fd = open(device_path, O_RDWR | O_NOCTTY);

    if(uart->fd < 0){
        perror("open uart");
        return -1;
    }
    if(uart_configure(uart->fd) < 0){
        close(uart->fd);
        uart->fd = -1;
        return -1;
    }
    if(uart_ack_init(&uart->ack)< 0){
        close(uart->fd);
        uart->fd = -1;
        return -1;
    }
    uart->rx_buffer.length = 0;

    return 0;
}

void uart_destroy(UartDeviceData *uart)
{
    if(uart == NULL){
        return;
    }
    if(uart->fd >= 0){
        close(uart->fd);
        uart->fd = -1;
    }

    pthread_mutex_destroy(&uart->ack.mutex);
    pthread_cond_destroy(&uart->ack.cond);
}