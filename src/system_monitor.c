#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "system_monitor.h"


int get_system_uptime(double *uptime)
{
    int fd = open("/proc/uptime", O_RDONLY);

    if(fd < 0){
        perror("open /proc/uptime");
        return -1;
    }

    char buffer[128];

    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);

    close(fd);

    if(n < 0){
        perror("read /proc/uptime");
        return -1;
    }

    buffer[n] = '\0';

    if(sscanf(buffer, "%lf", uptime) != 1){
        return -1;
    }

    return 0;
    
}

int get_memory_info(long *total_kb, long *available_kb)
{
    FILE *fp = fopen("/proc/meminfo", "r");              //多行文本用fopen更方便

    if(fp == NULL){
        perror("fopen /proc/,meminfo");
        return -1;

    }

    char line[256];

    *total_kb = 0;
    *available_kb = 0;

    while (fgets(line, sizeof(line), fp) != NULL)                     //一行一行读取
    {
        if(sscanf(line, "MemTotal: %ld kB", total_kb) == 1){          //寻找想要的一行
            continue;
        }

        if(sscanf(line, "MemAvailable: %ld kB", available_kb) == 1){
            continue;
        }
    }

    fclose(fp);

    if(*total_kb == 0 || *available_kb == 0){
            return -1;
    }

    return 0;
}

int get_cpu_stat(CpuStat *stat)
{
    FILE *fp = fopen("/proc/stat","r");

    if(fp == NULL){
        perror("fopen /proc/stat");
        return -1;
    }

    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;

    int ret = fscanf(fp, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);

    fclose(fp);

    if(ret != 8){
        return -1;
    }

    stat->idle = idle + iowait;

    stat->total = user + nice + system + idle + iowait + irq + softirq + steal;

    return 0;
}

int get_cpu_temperature(double *temperature)
{
    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");

    if(fp == NULL){
        return -1;
    }
    long temp_milli;

    if(fscanf(fp, "%ld", &temp_milli) != 1){
        fclose(fp);
        return -1;
    }

    fclose(fp);

    *temperature = temp_milli / 1000.0;

    return 0;
}

double calculate_cpu_usage(CpuStat *prev, CpuStat *curr)
{
    unsigned long long total_diff = curr->total - prev->total;
    unsigned long long idle_diff  = curr->idle - prev->idle;

    if(total_diff == 0){
        return 0.0;
    }

    return (double)(total_diff - idle_diff) / total_diff * 100.0;
}