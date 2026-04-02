#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcb.h"

static int pid_counter = 0;

PCB* make_pcb(const char *script_name, int job_time, int num_pages, int *page_table) {
    // A3 1.2.1: each PCB now tracks logical paging state instead of start/end offsets.
    PCB *new_pcb = malloc(sizeof(PCB));
    if (new_pcb == NULL) {
        fprintf(stderr, "Memory allocation failed for PCB\n");
        return NULL;
    }

    // A3 1.2.1: PCB now stores logical paging state instead of start/end indices.
    new_pcb->pid = ++pid_counter;
    snprintf(new_pcb->script_name, sizeof(new_pcb->script_name), "%s", script_name);
    new_pcb->PC = 0;
    new_pcb->num_pages = num_pages;
    new_pcb->page_table = page_table;
    new_pcb->job_time = job_time;
    new_pcb->job_length_score = job_time;
    new_pcb->next = NULL;
    return new_pcb;
}

void free_pcb(PCB *pcb) {
    // A3 1.2.1: each process owns its page-table copy, so free it with the PCB.
    if (pcb == NULL) {
        return;
    }

    // A3 1.2.1: freeing a process releases its private page-table copy only.
    free(pcb->page_table);
    free(pcb);
}
