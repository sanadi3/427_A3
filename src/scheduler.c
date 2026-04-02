#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include "scheduler.h"
#include "shellmemory.h"
#include "ready_queue.h"
#include "shell.h"

static int g_scheduler_active = 0;
static SchedulePolicy g_current_policy = POLICY_FCFS;
static int g_force_first_pid_once = -1;

// Multithreaded scheduler globals
static int mt_enabled = 0;
static pthread_t worker_threads[2];
static pthread_mutex_t rq_mutex = PTHREAD_MUTEX_INITIALIZER;  // Queue mutex
static pthread_cond_t rq_cond = PTHREAD_COND_INITIALIZER;  // Condition variable
static int scheduler_quit = 0;
static int active_jobs = 0;  // Count of jobs currently being executed
// Note: for the fcfs function in the video, please see line 41 onwards

// Background globals
static int background_jobs_active = 0;
static pthread_mutex_t bg_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * 1.2.5 background-mode fix (for T_background):
 * Before this, SJF could pick a shorter user program before the batch-script process.
 * Assignment says batch script must run first once.
 * So we keep one "forced first PID" and pop that process first exactly one time.
 * After that, scheduling goes back to normal FCFS/SJF/RR/RR30/AGING behavior.
 */
static PCB* scheduler_pop_forced_first_if_any(void);
// Forward declaration for 1.2.6
static void* scheduler_worker_thread(void* arg);
static int run_process_slice(PCB *current, int max_instructions, int last_error);

static int scheduler_run_fcfs(void) {
    // 1.2.1 base scheduler behavior. 1.2.2 exec FCFS also lands here
    int last_error = 0;
    PCB *current = NULL;

    while ((current = scheduler_pop_forced_first_if_any()) != NULL
           || (current = ready_queue_pop_head()) != NULL) {
        last_error = run_process_slice(current, -1, last_error);

        mem_cleanup_script(current->start, current->end);
        free(current);
    }

    return last_error;
}

// Pop forced-first process if any
static PCB* scheduler_pop_forced_first_if_any(void) {
    PCB *forced = NULL;
    if (g_force_first_pid_once < 0) {
        return NULL;
    }
    forced = ready_queue_pop_pid(g_force_first_pid_once);
    g_force_first_pid_once = -1;
    return forced;
}

// 1.2.3 helper. also reused by 1.2.4 aging loop
static int run_process_slice(PCB *current, int max_instructions, int last_error) {
    int executed = 0;

    while (current->pc <= current->end
           && (max_instructions < 0 || executed < max_instructions)) {
        char *line = mem_get_line(current->pc);
        if (line != NULL) {
            last_error = parseInput(line);
        }
        current->pc++;
        executed++;
    }

    return last_error;
}



// 1.2.3: SJF scheduler
static int scheduler_run_sjf(void) {
    int last_error = 0;
    PCB *current = NULL;

    // Get next process - try forced first, then shortest job
    while ((current = scheduler_pop_forced_first_if_any()) != NULL
           || (current = ready_queue_pop_shortest()) != NULL) {
        last_error = run_process_slice(current, -1, last_error);

        mem_cleanup_script(current->start, current->end);
        free(current);
    }

    return last_error;
}

// 1.2.3 + 1.2.5: RR core with configurable quantum (2 for RR, 30 for RR30)
static int scheduler_run_rr_quantum(int quantum) {
    int last_error = 0;
    PCB *current = NULL;

    // Get next process - try forced first, then head of queue
    while ((current = scheduler_pop_forced_first_if_any()) != NULL
           || (current = ready_queue_pop_head()) != NULL) {
        last_error = run_process_slice(current, quantum, last_error);

        if (current->pc > current->end) {
            mem_cleanup_script(current->start, current->end);
            free(current);
        } else {
            ready_queue_add_to_tail(current);
        }
    }

    return last_error;
}

// 1.2.4: AGING policy
static int scheduler_run_aging(void) {
    int last_error = 0;
    PCB *current = NULL;
    const int aging_quantum = 1;

    while ((current = scheduler_pop_forced_first_if_any()) != NULL
           || (current = ready_queue_pop_head()) != NULL) {
        last_error = run_process_slice(current, aging_quantum, last_error);

        if (current->pc > current->end) {
            mem_cleanup_script(current->start, current->end);
            free(current);
            continue;
        }

        // 1.2.4 exact rule: age waiting jobs, not the one that just ran
        ready_queue_age_all();

        // if current still lowest/tied-lowest, keep running. else reinsert sorted and promote
        PCB *next = ready_queue_peek_head();
        if (next == NULL || next->job_length_score >= current->job_length_score) {
            ready_queue_add_to_head(current);
        } else {
            ready_queue_insert_sorted(current);
        }
    }

    return last_error;
}

static int scheduler_run_mt_rr(int time_slice) {
    int slice_arg0 = time_slice;
    int slice_arg1 = time_slice;
    
    scheduler_quit = 0;  // Reset quit flag
    active_jobs = 0;  // Reset active job count
    
    pthread_create(&worker_threads[0], NULL, scheduler_worker_thread, &slice_arg0);
    pthread_create(&worker_threads[1], NULL, scheduler_worker_thread, &slice_arg1);
    
    // Wait for all jobs to complete
    while (1) {
        pthread_mutex_lock(&rq_mutex);
        int queue_empty = ready_queue_is_empty();
        int jobs_active = active_jobs;
        int should_quit = scheduler_quit;
        pthread_mutex_unlock(&rq_mutex);
        
        // If queue is empty and no jobs are being executed, we're done
        if (queue_empty && jobs_active == 0) {
            break;
        }
        usleep(1000);
    }
    
    // Tell threads to quit
    pthread_mutex_lock(&rq_mutex);
    scheduler_quit = 1;
    pthread_cond_broadcast(&rq_cond);  // Wake any waiting threads
    pthread_mutex_unlock(&rq_mutex);
    
    // Wait for threads to finish
    pthread_join(worker_threads[0], NULL); 
    pthread_join(worker_threads[1], NULL);
    
    return 0;
}


// Non-blocking MT scheduler for background mode
static int scheduler_run_mt_rr_nonblocking(int time_slice) {
    scheduler_quit = 0;  // Reset quit flag
    active_jobs = 0;  // Reset active job count
    // dont set g_scheduler_active = 1 here because its background
    pthread_create(&worker_threads[0], NULL, scheduler_worker_thread, (void*)(intptr_t)time_slice);
    pthread_create(&worker_threads[1], NULL, scheduler_worker_thread, (void*)(intptr_t)time_slice);
    
    return 0;
}

int scheduler_run(SchedulePolicy policy) {
    int rc = 1;

    // Check for MT mode with RR/RR30 policies
    if (mt_enabled && (policy == POLICY_RR || policy == POLICY_RR30)) {
        int slice = (policy == POLICY_RR) ? 2 : 30;
        return scheduler_run_mt_rr(slice);
    }
    // 1.2.5: avoid nested scheduler loops
    if (g_scheduler_active) {
        return 1;
    }
    g_scheduler_active = 1;
    g_current_policy = policy;

    // 1.2.2 policy dispatch entrypoint used by exec/source path
    switch (policy) {
    case POLICY_FCFS:
        rc = scheduler_run_fcfs();
        break;
    case POLICY_SJF:
        rc = scheduler_run_sjf();
        break;
    case POLICY_RR:
        rc = scheduler_run_rr_quantum(2);
        break;
    case POLICY_RR30:
        rc = scheduler_run_rr_quantum(30);
        break;
    case POLICY_AGING:
        rc = scheduler_run_aging();
        break;
    default:
        rc = 1;
        break;
    }

    g_scheduler_active = 0;
    return rc;
}

// Background mode scheduler: for MT, starts threads without waiting. For non-MT, returns immediately.
int scheduler_run_background(SchedulePolicy policy) {
    if (mt_enabled && (policy == POLICY_RR || policy == POLICY_RR30)) {
        int slice = (policy == POLICY_RR) ? 2 : 30;
        // use local variables, not static array
        return scheduler_run_mt_rr_nonblocking(slice);
    }
    return 0;
}

int scheduler_is_active(void) {
    // In background mode, check if there are active jobs or non-empty queue
    if (mt_enabled) {
        pthread_mutex_lock(&rq_mutex);
        int has_work = (active_jobs > 0 || !ready_queue_is_empty());
        pthread_mutex_unlock(&rq_mutex);
        return has_work;
    }
    return g_scheduler_active;
}

SchedulePolicy scheduler_get_current_policy(void) {
    return g_current_policy;
}

void scheduler_set_first_process_pid(int pid) {
    g_force_first_pid_once = pid;
}

void scheduler_enable_multithreaded() {
    mt_enabled = 1;
}

void scheduler_disable_multithreaded() {
    mt_enabled = 0;
}

int scheduler_is_multithreaded() {
    return mt_enabled;
}

// Wait for worker threads to finish (called on quit)
void scheduler_join_workers() {
    if (!mt_enabled) return;
    scheduler_quit = 1;
    pthread_cond_broadcast(&rq_cond);
    pthread_join(worker_threads[0], NULL);
    pthread_join(worker_threads[1], NULL);
}

// 1.2.6 Worker thread function for MT RR/RR30
static void* scheduler_worker_thread(void* arg) {
    int time_slice = (int)(intptr_t)arg;
    
    while (1) {
        pthread_mutex_lock(&rq_mutex);
        
        // Wait for work - but check quit condition properly
        while (!scheduler_quit && ready_queue_is_empty()) {
            pthread_cond_wait(&rq_cond, &rq_mutex);
        }
        
        // Check if we should quit (queue might be empty)
        if (scheduler_quit && ready_queue_is_empty()) {
            pthread_mutex_unlock(&rq_mutex);
            break;
        }
        
        // Get a process to run
        PCB *current = ready_queue_pop_head();
        if (current) {
            active_jobs++;
        }
        pthread_mutex_unlock(&rq_mutex);
        
        if (!current) continue;
        
        // Run the process slice
        run_process_slice(current, time_slice, 0);
        
        // Check if process is complete
        int is_done = (current->pc > current->end);
        
        pthread_mutex_lock(&rq_mutex);
        if (is_done) {
            // Process finished - cleanup
            mem_cleanup_script(current->start, current->end);
            free(current);
            active_jobs--;
        } else {
            // Process not done - back to queue
            ready_queue_add_to_tail(current);
            active_jobs--;
        }
        
        // Signal that queue state has changed
        pthread_cond_signal(&rq_cond);
        pthread_mutex_unlock(&rq_mutex);
    }

    // When thread exits (all jobs done)
    pthread_mutex_lock(&bg_mutex);
    background_jobs_active--;
    pthread_mutex_unlock(&bg_mutex);
    
    return NULL;
}
