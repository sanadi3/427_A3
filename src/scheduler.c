#include <stdlib.h>
#include <stdio.h>

#include "scheduler.h"
#include "shellmemory.h"
#include "ready_queue.h"
#include "shell.h"

static int g_scheduler_active = 0;
static int run_process_slice(PCB *current, int max_instructions, int last_error);
static char* get_current_instruction(PCB *current);

static int scheduler_run_fcfs(void) {
    // 1.2.1 base scheduler behavior. 1.2.2 exec FCFS also lands here.
    // A3 1.2.1: process teardown now frees only PCB-owned state.
    // A3 1.2.2: handle page faults by re-enqueueing process
    int last_error = 0;
    PCB *current = NULL;

    while ((current = ready_queue_pop_head()) != NULL) {
        last_error = run_process_slice(current, -1, last_error);
        
        // A3 1.2.2: if process hit a page fault and still has work, re-enqueue it
        if (current->PC < current->job_time) {
            ready_queue_add_to_tail(current);
        } else {
            free_pcb(current);
        }
    }

    return last_error;
}
// 1.2.3 helper. also reused by 1.2.4 aging loop
static int run_process_slice(PCB *current, int max_instructions, int last_error) {
    /* 
     1.2.2 — Demand Paging: Fault Detection During Execution
     * The scheduler does not pre-check all needed pages. It tries to fetch the
     * current instruction, and if translation fails because the page-table
     * entry is still -1, this loop triggers demand loading. A real load still
     * ends the slice so the process retries later, but a shared-script hit does
     * not count as a page fault: we sync the mapping, refetch the same line,
     * and keep using the remaining instructions in the current slice.
    */
    int executed = 0;

    // A3 1.2.1: stop based on logical line count, not physical memory bounds.
    while (current->PC < current->job_time
           && (max_instructions < 0 || executed < max_instructions)) {
        char *line = get_current_instruction(current);
        
        // NULL here means "cannot fetch this logical line right now", which is the page-fault signal.
        if (line == NULL && current->backing_path[0] != '\0') {
            int page = current->PC / PAGE_SIZE;
            if (page >= 0 && page < current->num_pages && current->page_table[page] < 0) {
                // Page fault - try to load the page on demand
                int load_result = mem_demand_load_page(current->page_table, page, current->backing_path,
                                                        current->script_name, current->job_time);
                if (load_result == 2) {
                    // Another process already loaded the page, so refetch and keep going from the same PC.
                    line = get_current_instruction(current);
                } else if (load_result == 0) {
                    // A real demand load interrupts the slice so the scheduler can requeue this process.
                    break;
                }
                // load_result == 1 is an error, so line stays NULL and the existing error path continues.
            }
        }
        
        // A3 1.2.1: padding is stored as "", and parseInput safely treats that as a no-op.
        if (line != NULL) {
            last_error = parseInput(line);
        }
        current->PC++;
        executed++;
    }

    return last_error;
}

static char* get_current_instruction(PCB *current) {
    /*
     * 1.2.1 / 1.2.2 — Logical-to-Physical Translation
     * PC counts logical script lines, not physical memory slots. We translate
     * PC into (page, offset), look up the frame in the page table, and then ask
     * shellmemory for that physical line. If the page-table entry is -1, we
     * return NULL instead of forcing a load here so the scheduler can treat the
     * miss as a page fault and preserve the current instruction.
    */
    int page;
    int offset;
    int frame;

    // A3 1.2.1: translate logical PC through the page table into the frame store.
    if (current == NULL || current->PC < 0 || current->PC >= current->job_time) {
        return NULL;
    }

    /* PC / PAGE_SIZE gives the logical page number.
       PC % PAGE_SIZE gives the line offset inside that page. */
    page = current->PC / PAGE_SIZE;
    offset = current->PC % PAGE_SIZE;
    if (page < 0 || page >= current->num_pages) {
        return NULL;
    }

    frame = current->page_table[page];
    if (frame < 0) {
        return NULL;
    }

    return mem_get_frame_line(frame, offset);
}



// 1.2.3: SJF scheduler
static int scheduler_run_sjf(void) {
    // A3 1.2.1: SJF ordering stays the same; only instruction fetch/teardown changed.
    int last_error = 0;
    PCB *current = NULL;

    while ((current = ready_queue_pop_shortest()) != NULL) {
        last_error = run_process_slice(current, -1, last_error);

        if (current->PC < current->job_time) {
            ready_queue_add_to_tail(current);
        } else {
            free_pcb(current);
        }
    }

    return last_error;
}

// 1.2.3 + 1.2.5: RR core with configurable quanta (2 for RR, 30 for RR30)
static int scheduler_run_rr_quantum(int quantum) {
    int last_error = 0;
    PCB *current = NULL;

    while ((current = ready_queue_pop_head()) != NULL) {
        last_error = run_process_slice(current, quantum, last_error);

        // A3 1.2.1: process exit no longer frees shared frames.
        if (current->PC >= current->job_time) {
            free_pcb(current);
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

    while ((current = ready_queue_pop_head()) != NULL) {
        last_error = run_process_slice(current, aging_quantum, last_error);

        // A3 1.2.1: process exit no longer frees shared frames.
        if (current->PC >= current->job_time) {
            free_pcb(current);
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

int scheduler_run(SchedulePolicy policy) {
    int rc = 1;

    if (g_scheduler_active) {
        return 1;
    }
    g_scheduler_active = 1;

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
