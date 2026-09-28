#ifndef DEVICE_H
#define DEVICE_H

#include <pthread.h>

typedef enum{
    DEVICE_IDLE,
    DEVICE_RUNNING,
    DEVICE_ERROR,
} Device_status;

struct Device;

typedef struct{
    int (*start)(struct Device *device);
    int (*stop)(struct Device *device);
    int (*set_speed)(struct Device *device, int speed);
}DeviceOps;

typedef struct Device{
    Device_status status;
    int speed;

    pthread_mutex_t mutex;

    DeviceOps *ops;

    void *private_data;
}Device;

int device_init(Device *device, DeviceOps *ops, void *private_data);

void device_destroy(Device *device);

extern DeviceOps virtual_device_ops;

#endif