#ifndef PCB_H
#define PCB_H

typedef struct PCB {
    int pid;
    int start;
    int end;
    int pc; // 1.2.1 Program Counter
    int job_time; // 1.2.3 estimated length (line count)
    int job_length_score; // 1.2.4 AGING score
    struct PCB *next; // Pointer to the next PCB in the queue
} PCB;

PCB* make_pcb(int start, int end);

#endif
