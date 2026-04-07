#ifndef READY_QUEUE_H
#define READY_QUEUE_H

#include "pcb.h"

void ready_queue_add_to_tail(PCB *p);
void ready_queue_add_to_head(PCB *p); // Helper for some policies
PCB* ready_queue_pop_head();
PCB* ready_queue_pop_shortest(); // Helper for SJF policy
void ready_queue_insert_sorted(PCB *p); // 1.2.4 AGING: score-sorted enqueue
void ready_queue_age_all(void); // 1.2.4 AGING: age waiting jobs
PCB* ready_queue_peek_head(void); // 1.2.4 AGING: promotion/continue check
int ready_queue_is_empty(void);
void ready_queue_print(); // Helper for debugging

#endif
