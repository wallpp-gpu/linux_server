#ifndef COMMAND_H
#define COMMAND_H

#include <pthread.h>
#include "device.h"
#include "logger.h"

#define COMMAND_QUEUE_SIZE 64

typedef enum{                     //命令类型
    CMD_START,
    CMD_STOP,
    CMD_SET_SPEED,
}CommandType;

typedef struct{                   //命令结构
    CommandType type;
    int value;
}Command;

typedef struct{                             //命令队列结构
    Command commands[COMMAND_QUEUE_SIZE];
    int head;
    int tail;
    int count;

    pthread_mutex_t mutex;                  //互斥锁(同一时刻只能有一个线程访问命令队列)
    pthread_cond_t cond;                    //条件变量（用于线程间的同步，通知有新命令到来）

}CommandQueue;

typedef struct{                //控制线程参数结构
    CommandQueue *queue;
    Device *device;
    LogQueue *log_queue;

}ControlThreadArgs;

int command_queue_init(CommandQueue *queue);
int command_queue_push(CommandQueue *queue, Command command);
int command_queue_pop(CommandQueue *queue, Command *command);
void *control_thread(void *arg);     //设备线程，负责改device参数，线程函数
void command_queue_destroy(CommandQueue *queue);

#endif