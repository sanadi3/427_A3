#include <stdlib.h>
#include <stdio.h>
#include "pcb.h"

int pid_counter = 0; // global pid counter

PCB* make_pcb(int start, int end) {
    // 1.2.1: one PCB per loaded script
    PCB* new_pcb = (PCB*)malloc(sizeof(PCB));
    if (new_pcb == NULL) {
        fprintf(stderr, "Memory allocation failed for PCB\n");
        return NULL;
    }
    new_pcb->pid = ++pid_counter; //pre increment to start from 1
    new_pcb->start = start;
    new_pcb->end = end;
    new_pcb->pc = start; // 1.2.1 program counter
    new_pcb->job_time = (end - start+1); // 1.2.3 SJF uses line count as job length
    new_pcb->job_length_score = new_pcb->job_time; // (NOT in the video) 1.2.4 AGING score starts = job length
    new_pcb->next = NULL; // Initialize next pointer to NULL
    return new_pcb;
}
