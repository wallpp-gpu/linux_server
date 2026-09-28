#ifndef UART_H
#define UART_H

#include <pthread.h>
#include <stddef.h>
#include "device.h"

#define UART_RX_BUFFER_SIZE 1024

typedef struct{
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    int received;

    char message[128];
}UartAck;

typedef struct{
    char buffer[1024];
    size_t length;

}UartRxBuffer;

typedef struct{
    int fd;

    const char *device_path;

    UartRxBuffer rx_buffer;

    UartAck ack;

}UartDeviceData;

int uart_write_all(int fd, const char *buffer, size_t length);

int uart_init(UartDeviceData *uart, const char *device_path);

void uart_destroy(UartDeviceData *uart);

void *uart_rx_thread(void *arg);

extern DeviceOps uart_device_ops;

#endif