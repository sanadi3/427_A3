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
int scheduler_run_background(SchedulePolicy policy);
int scheduler_is_active(void);

// Enable/disable multithreaded mode
void scheduler_enable_multithreaded();
void scheduler_disable_multithreaded();
// Join worker threads on quit
void scheduler_join_workers();
// Check if multithreaded mode is enabled
int scheduler_is_multithreaded();

#endif
