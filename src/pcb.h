#ifndef PCB_H
#define PCB_H

typedef struct PCB {
    int pid;
    // A3 1.2.1: logical execution state for paging.
    char script_name[256];
    // A3 1.2.2: backing store path for demand paging.
    char backing_path[512];
    int PC;
    int num_pages;
    int *page_table;
    int job_time; // 1.2.3 estimated length (line count)
    int job_length_score; // 1.2.4 AGING score
    struct PCB *next; // Pointer to the next PCB in the queue
} PCB;

PCB* make_pcb(const char *script_name, int job_time, int num_pages, int *page_table);
void free_pcb(PCB *pcb);

#endif
