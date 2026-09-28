#ifndef LOGGER_H
#define LOGGER_H

#include <pthread.h>
#include "app.h"
#include "errno.h"
#define LOG_QUEUE_SIZE 128                     //定义日志
#define LOG_MESSAGE_SIZE 256

typedef struct{
    char message[LOG_MESSAGE_SIZE];

}LogMessage;

typedef struct{                                //日志队列
    LogMessage logs[LOG_QUEUE_SIZE];

    int head;
    int tail;
    int count;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

}LogQueue;

typedef struct
{
    LogQueue *queue;
    const char *log_path;

} LoggerThreadArgs;

int log_queue_init(LogQueue *queue);     //初始化日志队列
void log_queue_destroy(LogQueue *queue);
int log_queue_push(LogQueue *queue, const char *message);
int log_queue_pop(LogQueue *queue, LogMessage *log);
void *logger_thread(void *arg);


#endif