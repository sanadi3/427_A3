#include <stdio.h>
#include "ready_queue.h"

// Global pointers to head and tail 
PCB *head = NULL;
PCB *tail = NULL;

// 1.2.1/1.2.2 FCFS path uses tail enqueue
void ready_queue_add_to_tail(PCB *p) {
    if (!p) {
        return;
    }
    p->next = NULL;

    if (head == NULL) {  // Empty queue
        head = p;
        tail = p;
    } else {
        tail->next = p;
        tail = p;
    }
}

// 1.2.4 AGING can keep current process running by putting it back at head
void ready_queue_add_to_head(PCB *p) {
    if (!p) {
        return;
    }

    if (head == NULL) {  // Empty queue
        head = p;
        tail = p;
        p->next = NULL;
    } else {
        p->next = head;
        head = p;
    }
}

// shared dequeue for FCFS/RR/AGING
PCB* ready_queue_pop_head() {
    if (head == NULL) {  // Empty queue
        return NULL;
    }

    PCB *temp = head;
    head = head->next;
    
    // If queue becomes empty, reset tail
    if (head == NULL) {
        tail = NULL;
    }
    
    temp->next = NULL; // Isolate the popped PCB
    return temp;
}

void ready_queue_insert_sorted(PCB *p) {
    // 1.2.4: keep AGING queue ordered by score (low score first)
    if (!p) {
        return;
    }
    p->next = NULL;

    if (head == NULL) {  // Empty queue
        head = p;
        tail = p;
        return;
    }

    // 1.2.4 tie behavior: stable for equal scores
    if (p->job_length_score < head->job_length_score) {
        p->next = head;
        head = p;
        return;
    }

    // Find correct position in sorted list
    PCB *curr = head; 
    while (curr->next != NULL && curr->next->job_length_score <= p->job_length_score) {
        curr = curr->next;
    }

    // Insert after curr
    p->next = curr->next;
    curr->next = p;
    if (p->next == NULL) {
        tail = p;
    }
}

void ready_queue_age_all(void) {
    // 1.2.4 aging step: waiting jobs only, score-- floor at 0
    PCB *curr = head;
    while (curr != NULL) {
        if (curr->job_length_score > 0) {
            curr->job_length_score--;
        }
        curr = curr->next;
    }
}

// Peek at head of queue without removing (for AGING decision)
PCB* ready_queue_peek_head(void) {
    return head;
}

// 1.2.3 SJF: pick lowest job_time
PCB* ready_queue_pop_shortest() {
    if (head == NULL) {
        return NULL;
    }

    // Track min node and its predecessor for removal
    PCB *prev = NULL;
    PCB *curr = head;
    PCB *min_prev = NULL;
    PCB *min_node = head;

    // Find minimum job_time
    while (curr != NULL) {
        if (curr->job_time < min_node->job_time) {
            min_node = curr;
            min_prev = prev;
        }
        prev = curr;
        curr = curr->next;
    }

    // Remove min_node from queue
    if (min_prev == NULL) {
        head = min_node->next;
    } else {
        min_prev->next = min_node->next;
    }

    if (tail == min_node) {
        tail = min_prev;
    }

    min_node->next = NULL;
    return min_node;
}
int ready_queue_is_empty(void) {
    return head == NULL;
}

void ready_queue_print() {
    PCB *curr = head;
    printf("Ready Queue: ");
    while (curr != NULL) {
        printf("[PID:%d] -> ", curr->pid);
        curr = curr->next;
    }
    printf("NULL\n");
}
