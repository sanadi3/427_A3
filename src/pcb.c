#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcb.h"
#include "shellmemory.h"

static int pid_counter = 0;

PCB* make_pcb(const char *script_name, int job_time, int num_pages, int *page_table) {
    /*
     * 1.2.1: PCB Construction
     * A PCB no longer remembers where a script starts in one flat code array.
     * Instead it has a logical PC and a private page-table copy, which is what
     * lets multiple processes share frames while still tracking their own view
     * of which pages are in.
    */
    PCB *new_pcb = malloc(sizeof(PCB));
    if (new_pcb == NULL) {
        fprintf(stderr, "Memory allocation failed for PCB\n");
        return NULL;
    }

    // A3 1.2.1: PCB now stores logical paging state instead of start/end indices.
    new_pcb->pid = ++pid_counter;
    snprintf(new_pcb->script_name, sizeof(new_pcb->script_name), "%s", script_name);
    new_pcb->backing_path[0] = '\0';
    new_pcb->PC = 0;
    new_pcb->num_pages = num_pages;
    new_pcb->page_table = page_table;
    new_pcb->job_time = job_time;
    new_pcb->job_length_score = job_time;
    new_pcb->next = NULL;

    if (mem_register_pcb(new_pcb) != 0) {
        free(new_pcb);
        fprintf(stderr, "Memory allocation failed for PCB registry\n");
        return NULL;
    }

    return new_pcb;
}

void free_pcb(PCB *pcb) {
    /*
     * 1.2.1 / 1.2.2 — PCB Teardown
     * The page table belongs to the PCB, so it is freed here. The frames named
     * by that page table are not freed on normal process exit, because other
     * processes may still share them and eviction is handled globally by the
     * paging system instead.
    */
    if (pcb == NULL) {
        return;
    }

    mem_unregister_pcb(pcb);

    // A3 1.2.1: freeing a process releases its private page-table copy only.
    free(pcb->page_table);
    free(pcb);
}
