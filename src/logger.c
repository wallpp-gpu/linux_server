#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "logger.h"

int log_queue_init(LogQueue *queue)      //初始化日志队列
{
    if(queue == NULL){
        return -1;
    }
    memset(queue, 0, sizeof(LogQueue));

    int ret = pthread_mutex_init( &queue->mutex, NULL);


    if(ret < 0){
        fprintf(stderr, "pthread_mutex_init: %s\n", strerror(ret));
        return -1;
    }

    ret = pthread_cond_init(&queue->cond, NULL);

    if(ret < 0){
        fprintf(stderr, "pthread_cond_init: %s\n", strerror(ret));
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }

    return 0;
}

int log_queue_push(LogQueue *queue, const char *message)
{
    if(queue == NULL || message == NULL){
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);
    if(queue->count >= LOG_QUEUE_SIZE){
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    snprintf(queue->logs[queue->tail].message, LOG_MESSAGE_SIZE, "%s", message);

    queue->tail = (queue->tail + 1) % LOG_QUEUE_SIZE;
    queue->count ++;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

int log_queue_pop(LogQueue *queue, LogMessage *log)
{
    if(queue == NULL || log == NULL){
        return -1;
    }

    pthread_mutex_lock(&queue->mutex);
    while(queue->count == 0 && g_running){
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }

    if(queue->count == 0 && !g_running){
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    *log = queue->logs[queue->head];

    queue->head = (queue->head + 1) % LOG_QUEUE_SIZE;

    queue->count--;

    pthread_mutex_unlock(&queue->mutex);

    return 0;

}

static int write_all(int fd, const char *buffer, size_t length)         //完整写入函数
{
    size_t total = 0;

    while(total < length){
        ssize_t n = write(fd, buffer + total, length - total);

        if(n < 0){
            if(errno == EINTR){
                continue;
            }

            perror("write");
            return -1;

        }

        total +=(size_t)n;
    }

    return 0;
}

void log_queue_destroy(LogQueue *queue)
{
    if(queue == NULL){
        return;
    }

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

void *logger_thread(void *arg)
{
    LoggerThreadArgs *log_queue = (LoggerThreadArgs *)arg;
    
    int log_fd = open(log_queue->log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if(log_fd < 0){
        perror("open log file");
        return NULL;
    }

    while(g_running){
        LogMessage log;

        if(log_queue_pop(log_queue->queue, &log) < 0){
            break;
        }

        time_t now;
        struct tm tm_info;
        char time_buffer[64];
        char line[512];

        time(&now);

        localtime_r(&now, &tm_info);

        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &tm_info);

        int len = snprintf(line,sizeof(line), "[%s] %s\n", time_buffer, log.message);

        if(len < 0){
            continue;
        }

        if((size_t)len >= sizeof(line)){
            len = sizeof(line) - 1;
        }

        if(write_all(log_fd, line, (size_t)len) < 0){
            break;
        }

    }

    close(log_fd);

    return NULL;

}