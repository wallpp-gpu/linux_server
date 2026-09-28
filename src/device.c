#include <stdio.h>
#include <string.h>

#include "device.h"

int device_init(Device *device, DeviceOps *ops, void *private_data)
{
    if(device == NULL || ops == NULL){
        return -1;
    }

    memset(device, 0, sizeof(Device));

    device->status = DEVICE_IDLE;
    device->speed = 0;

    device->ops = ops;

    device->private_data = private_data;

    if(pthread_mutex_init( &device->mutex, NULL) != 0){
        return -1;
    }

    return 0;
}

void device_destroy(Device *device)
{
    if(device == NULL){
        return;
    }

    pthread_mutex_destroy(&device->mutex);
}

static int virtual_device_start(Device *device)
{
    pthread_mutex_lock(&device->mutex);

    device->status = DEVICE_RUNNING;

    pthread_mutex_unlock(&device->mutex);
    
    return 0;
}

static int virtual_device_stop(Device *device)
{
    pthread_mutex_lock(&device->mutex);

    device->status = DEVICE_IDLE;
    device->speed = 0;

    pthread_mutex_unlock(&device->mutex);
    
    return 0;
}

static int virtual_device_set_speed(Device *device, int speed)
{
    if(speed < 0 || speed > 100){
        return -1;
    }

    pthread_mutex_lock(&device->mutex);

    device->status = DEVICE_RUNNING;
    device->speed = speed;

    pthread_mutex_unlock(&device->mutex);
    
    return 0;
}

DeviceOps virtual_device_ops =
{
    .start = virtual_device_start,

    .stop = virtual_device_stop,

    .set_speed = virtual_device_set_speed
};