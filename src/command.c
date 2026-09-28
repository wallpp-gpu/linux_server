#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "app.h"
#include "command.h"


int command_queue_init(CommandQueue *queue)
{
    if(queue == NULL){
        return -1;
    }

    memset(queue, 0, sizeof(CommandQueue));

    if(pthread_mutex_init(&queue->mutex, NULL) < 0){
        perror("pthread_mutex_init");
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }
    if(pthread_cond_init(&queue->cond, NULL) < 0){
        perror("pthread_cond_init");
        pthread_cond_destroy(&queue->cond);
        return -1;
    }

    return 0;
}

int command_queue_push(CommandQueue *queue, Command command)
{
    pthread_mutex_lock(&queue->mutex);

    if(queue->count >=COMMAND_QUEUE_SIZE){
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    queue->commands[queue->tail] = command;
    queue->tail = (queue->tail + 1) % COMMAND_QUEUE_SIZE;
    queue->count++;
    pthread_cond_signal(&queue->cond); //通知有新命令到来
    pthread_mutex_unlock(&queue->mutex);   //解锁互斥锁
    return 0;
}

int command_queue_pop(CommandQueue *queue, Command *command)
{
    pthread_mutex_lock(&queue->mutex);

    while(queue->count == 0 && g_running){
        pthread_cond_wait(&queue->cond, &queue->mutex); //等待新命令到来，自动解锁互斥锁并阻塞线程，直到收到通知

    }

    if(!g_running){
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    *command = queue->commands[queue->head];  //获取队列头部的命令
    queue->head = (queue->head + 1) % COMMAND_QUEUE_SIZE;
    queue->count--;
    pthread_mutex_unlock(&queue->mutex);   //解锁互斥锁
    return 0;
}

void *control_thread(void *arg)                      //设备线程，负责改device参数，线程函数
{
    ControlThreadArgs *args = (ControlThreadArgs *)arg;
    CommandQueue *queue = args->queue;

    while(g_running){
        Command command;
        if(command_queue_pop(queue, &command) < 0){
            break;
        }

        switch (command.type)
        {
        case CMD_START:
            if(args->device->ops != NULL && args->device->ops->start != NULL){
                args->device->ops->start(args->device);
                log_queue_push(args->log_queue, "device started");
            }
            break;
        
        case CMD_STOP:
            if(args->device->ops != NULL && args->device->ops->stop!= NULL){
                args->device->ops->stop(args->device);
                log_queue_push(args->log_queue, "device stoped");
            }
            break;

        case CMD_SET_SPEED:
            if(args->device->ops != NULL && args->device->ops->set_speed !=NULL){
                args->device->ops->set_speed(args->device, command.value);
                
                char log_message[128];

                snprintf(log_message, sizeof(log_message), "speed changed to %d", command.value);

                log_queue_push(args->log_queue, log_message);
            }
            break;
        }

    }
    
    return  NULL;
}

void command_queue_destroy(CommandQueue *queue)
{
    if(queue == NULL){
        return;
    }
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}