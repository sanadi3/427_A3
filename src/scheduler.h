#ifndef SCHEDULER_H
#define SCHEDULER_H

typedef enum {
    POLICY_FCFS = 0,
    POLICY_SJF,
    POLICY_RR,
    POLICY_AGING,
    POLICY_RR30
} SchedulePolicy;

int scheduler_run(SchedulePolicy policy);

#endif
