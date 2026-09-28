#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

typedef struct{
    unsigned long long total;
    unsigned long long idle;
}CpuStat;

int get_system_uptime(double *uptime);
int get_memory_info(long *total_kb, long *available_kb);
int get_cpu_stat(CpuStat *stat);
int get_cpu_temperature(double *temperature);
double calculate_cpu_usage(CpuStat *prev, CpuStat *curr);


#endif