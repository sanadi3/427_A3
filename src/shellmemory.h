#ifndef SHELLMEMORY_H
#define SHELLMEMORY_H

#include <stddef.h>

// Forward declaration to avoid circular includes
typedef struct PCB PCB;

#ifndef FRAME_STORE_SIZE
#define FRAME_STORE_SIZE 18
#endif

#ifndef VAR_STORE_SIZE
#define VAR_STORE_SIZE 10
#endif

#define PAGE_SIZE 3
#define MAX_FRAMES (FRAME_STORE_SIZE / PAGE_SIZE)
#define MAX_PROCESSES 10  // Max processes that can share a frame

typedef struct FrameEntry {
    int occupied;
    char script_name[256];
    int page_num;
    unsigned long last_used;
    // A3 1.2.2: track which processes reference this frame
    int using_pcb_pids[MAX_PROCESSES];
    int num_using;
} FrameEntry;

void mem_init(void);
char *mem_get_value(char *var);
void mem_set_value(char *var, char *value);

void mem_ensure_backing_store(void);
int mem_copy_script_to_backing_store(const char *source_path,
                                     char *script_name_out, size_t script_name_size,
                                     char *backing_path_out, size_t backing_path_size);
int mem_count_script_lines(const char *script_path);
int mem_count_free_frames(void);
int mem_is_script_loaded(const char *script_name);
int mem_clone_script_page_table(const char *script_name,
                                int **page_table_out, int *num_pages_out, int *line_count_out);
int mem_load_initial_pages(const char *backing_path, const char *script_name,
                           int total_lines, int **page_table_out, int *num_pages_out);
int mem_load_script_from_backing_store(const char *backing_path, const char *script_name,
                                       int total_lines, int **page_table_out, int *num_pages_out);
int mem_demand_load_page(int *page_table, int page_num, const char *backing_path,
                         const char *script_name, int total_lines);
int mem_register_script(const char *script_name, const int *page_table, int num_pages, int line_count);
void mem_unregister_script(const char *script_name);
int mem_register_pcb(PCB *pcb);
void mem_unregister_pcb(PCB *pcb);
void mem_release_frames(const int *page_table, int num_pages);
char *mem_get_frame_line(int frame, int offset);

#endif
