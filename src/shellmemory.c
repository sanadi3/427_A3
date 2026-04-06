#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "shellmemory.h"

typedef struct memory_struct {
    char *var;
    char *value;
} MemoryEntry;

typedef struct loaded_script_struct {
    int in_use;
    char script_name[256];
    int num_pages;
    int line_count;
    int *page_table;
} LoadedScriptEntry;

static MemoryEntry variable_store[VAR_STORE_SIZE];
// A3 1.2.1: frame store replaces the old flat script-memory array.
static char *frame_store[FRAME_STORE_SIZE];
// A3 1.2.1: track which script/page occupies each frame.
static FrameEntry frame_table[MAX_FRAMES];
// A3 1.2.1: remember loaded scripts so duplicate exec shares frames.
static LoadedScriptEntry loaded_scripts[MAX_FRAMES];

// A3 1.2.2: global clock for LRU (Least Recently Used) page replacement
static unsigned long clock_value = 0;

static int clone_page_table(const int *src, int num_pages, int **out_copy) {
    // A3 1.2.1: duplicate a shared script mapping so each PCB gets its own table object.
    if (num_pages <= 0) {
        *out_copy = NULL;
        return 0;
    }

    int *copy = malloc((size_t)num_pages * sizeof(int));
    if (copy == NULL) {
        return 1;
    }

    memcpy(copy, src, (size_t)num_pages * sizeof(int));
    *out_copy = copy;
    return 0;
}

static int find_loaded_script_index(const char *script_name) {
    // A3 1.2.1: duplicate exec/source should reuse an existing loaded script.
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (loaded_scripts[i].in_use && strcmp(loaded_scripts[i].script_name, script_name) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_free_frame(void) {
    // A3 1.2.1: Task 1 still allocates a whole free frame per page.
    for (int frame = 0; frame < MAX_FRAMES; frame++) {
        if (!frame_table[frame].occupied) {
            return frame;
        }
    }
    return -1;
}

static int basename_from_path(const char *path, char *out_name, size_t out_size) {
    // A3 1.2.1: strip directories so backing_store uses just the script filename.
    const char *base = strrchr(path, '/');
    base = (base == NULL) ? path : base + 1;

    if (*base == '\0' || snprintf(out_name, out_size, "%s", base) >= (int)out_size) {
        return 1;
    }
    return 0;
}

static void clear_frame(int frame) {
    int base = frame * PAGE_SIZE;

    // A3 1.2.1: helper for rolling back a partially loaded script.
    for (int i = 0; i < PAGE_SIZE; i++) {
        free(frame_store[base + i]);
        frame_store[base + i] = NULL;
    }

    frame_table[frame].occupied = 0;
    frame_table[frame].script_name[0] = '\0';
    frame_table[frame].page_num = -1;
    frame_table[frame].last_used = 0;
    frame_table[frame].num_using = 0;
}

static int mem_register_pcb_uses_frame(int frame, int pcb_pid) {
    // A3 1.2.2: register that a PCB uses this frame
    if (frame < 0 || frame >= MAX_FRAMES) {
        return 1;
    }

    // Check if already registered
    for (int i = 0; i < frame_table[frame].num_using; i++) {
        if (frame_table[frame].using_pcb_pids[i] == pcb_pid) {
            return 0;  // Already registered
        }
    }

    // Add to list if space available
    if (frame_table[frame].num_using >= MAX_PROCESSES) {
        return 1;  // No space
    }

    frame_table[frame].using_pcb_pids[frame_table[frame].num_using] = pcb_pid;
    frame_table[frame].num_using++;
    return 0;
}

static int mem_unregister_pcb_from_frame(int frame, int pcb_pid) {
    // A3 1.2.2: unregister that a PCB uses this frame
    if (frame < 0 || frame >= MAX_FRAMES) {
        return 1;
    }

    for (int i = 0; i < frame_table[frame].num_using; i++) {
        if (frame_table[frame].using_pcb_pids[i] == pcb_pid) {
            // Remove by shifting
            for (int j = i; j < frame_table[frame].num_using - 1; j++) {
                frame_table[frame].using_pcb_pids[j] = frame_table[frame].using_pcb_pids[j + 1];
            }
            frame_table[frame].num_using--;
            return 0;
        }
    }

    return 0;  // Not found, but not an error
}

static int mem_update_page_table_for_evicted_frame(int victim_frame) {
    // A3 1.2.2: update all page tables that referenced the evicted frame
    char victim_script[256];
    int victim_page;

    strcpy(victim_script, frame_table[victim_frame].script_name);
    victim_page = frame_table[victim_frame].page_num;

    // Find all loaded scripts that use this script name
    for (int script_idx = 0; script_idx < MAX_FRAMES; script_idx++) {
        if (!loaded_scripts[script_idx].in_use) {
            continue;
        }

        if (strcmp(loaded_scripts[script_idx].script_name, victim_script) != 0) {
            continue;
        }

        // Found a loaded script with this name, mark the page as not loaded
        if (victim_page >= 0 && victim_page < loaded_scripts[script_idx].num_pages) {
            loaded_scripts[script_idx].page_table[victim_page] = -1;
        }
    }

    return 0;
}

static int mem_evict_random_frame(void) {
    // A3 1.2.2: evict the least recently used (LRU) occupied frame
    int victim = -1;
    unsigned long min_last_used = (unsigned long)-1;
    
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (frame_table[i].occupied && frame_table[i].last_used < min_last_used) {
            victim = i;
            min_last_used = frame_table[i].last_used;
        }
    }

    if (victim < 0) {
        return -1;  // No frame to evict
    }

    // Print victim page contents header and contents (frame store was full)
    printf("Page fault! Victim page contents:\n\n");
    int base = victim * PAGE_SIZE;
    for (int i = 0; i < PAGE_SIZE; i++) {
        if (frame_store[base + i] != NULL) {
            printf("%s", frame_store[base + i]);
        }
    }
    printf("\nEnd of victim page contents.\n");

    // Clear the victim frame
    clear_frame(victim);

    return victim;
}

void mem_init(void) {
    // A3 1.2.1: initialize the split memory model used by paging.
    for (int i = 0; i < VAR_STORE_SIZE; i++) {
        variable_store[i].var = "none";
        variable_store[i].value = "none";
    }

    // A3 1.2.1: reset physical frame contents at startup.
    for (int i = 0; i < FRAME_STORE_SIZE; i++) {
        frame_store[i] = NULL;
    }

    // A3 1.2.1: clear frame metadata and loaded-script sharing state.
    for (int i = 0; i < MAX_FRAMES; i++) {
        frame_table[i].occupied = 0;
        frame_table[i].script_name[0] = '\0';
        frame_table[i].page_num = -1;
        frame_table[i].last_used = 0;
        // A3 1.2.2: initialize PCB tracking
        frame_table[i].num_using = 0;
        for (int j = 0; j < MAX_PROCESSES; j++) {
            frame_table[i].using_pcb_pids[j] = -1;
        }

        loaded_scripts[i].in_use = 0;
        loaded_scripts[i].script_name[0] = '\0';
        loaded_scripts[i].num_pages = 0;
        loaded_scripts[i].line_count = 0;
        loaded_scripts[i].page_table = NULL;
    }
}

void mem_set_value(char *var_in, char *value_in) {
    for (int i = 0; i < VAR_STORE_SIZE; i++) {
        if (strcmp(variable_store[i].var, var_in) == 0) {
            variable_store[i].value = strdup(value_in);
            return;
        }
    }

    for (int i = 0; i < VAR_STORE_SIZE; i++) {
        if (strcmp(variable_store[i].var, "none") == 0) {
            variable_store[i].var = strdup(var_in);
            variable_store[i].value = strdup(value_in);
            return;
        }
    }
}

char *mem_get_value(char *var_in) {
    for (int i = 0; i < VAR_STORE_SIZE; i++) {
        if (strcmp(variable_store[i].var, var_in) == 0) {
            return strdup(variable_store[i].value);
        }
    }
    return NULL;
}

void mem_ensure_backing_store(void) {
    // A3 1.2.1: wipe stale backing-store contents from prior runs, then recreate the directory.
    system("rm -rf backing_store");
    system("mkdir backing_store");
}

int mem_copy_script_to_backing_store(const char *source_path,
                                     char *script_name_out, size_t script_name_size,
                                     char *backing_path_out, size_t backing_path_size) {
    FILE *src = NULL;
    FILE *dst = NULL;
    char line[1000];

    // A3 1.2.1: copy one source script into the already prepared backing_store.
    if (basename_from_path(source_path, script_name_out, script_name_size) != 0) {
        return 1;
    }

    if (snprintf(backing_path_out, backing_path_size, "backing_store/%s", script_name_out)
        >= (int)backing_path_size) {
        return 1;
    }

    src = fopen(source_path, "rt");
    if (src == NULL) {
        return 1;
    }

    dst = fopen(backing_path_out, "wt");
    if (dst == NULL) {
        fclose(src);
        return 1;
    }

    while (fgets(line, sizeof(line), src) != NULL) {
        if (fputs(line, dst) == EOF) {
            fclose(src);
            fclose(dst);
            return 1;
        }
    }

    fclose(src);
    fclose(dst);
    return 0;
}

int mem_count_script_lines(const char *script_path) {
    // A3 1.2.1: count logical lines so we can size the page table and job length.
    FILE *script = fopen(script_path, "rt");
    char line[1000];
    int line_count = 0;

    if (script == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), script) != NULL) {
        line_count++;
    }

    fclose(script);
    return line_count;
}

int mem_count_free_frames(void) {
    int free_frames = 0;

    // A3 1.2.1: Task 1 rejects loads that cannot fit all pages eagerly.
    for (int frame = 0; frame < MAX_FRAMES; frame++) {
        if (!frame_table[frame].occupied) {
            free_frames++;
        }
    }

    return free_frames;
}

int mem_is_script_loaded(const char *script_name) {
    // A3 1.2.1: tells exec/source whether frames can be shared for this script.
    return find_loaded_script_index(script_name) >= 0;
}

int mem_clone_script_page_table(const char *script_name,
                                int **page_table_out, int *num_pages_out, int *line_count_out) {
    int index = find_loaded_script_index(script_name);

    // A3 1.2.1: each PCB gets its own page-table copy even when frames are shared.
    if (index < 0) {
        return 1;
    }

    if (clone_page_table(loaded_scripts[index].page_table,
                         loaded_scripts[index].num_pages,
                         page_table_out) != 0) {
        return 1;
    }

    *num_pages_out = loaded_scripts[index].num_pages;
    *line_count_out = loaded_scripts[index].line_count;
    return 0;
}

int mem_load_script_from_backing_store(const char *backing_path, const char *script_name,
                                       int total_lines, int **page_table_out, int *num_pages_out) {
    // A3 1.2.1: eager-load every page from the backing-store file into frames.
    FILE *script = NULL;
    int *page_table = NULL;
    // A3 1.2.1: round up total lines to whole pages of 3 lines each.
    int num_pages = (total_lines + PAGE_SIZE - 1) / PAGE_SIZE;
    char line[1000];

    *page_table_out = NULL;
    *num_pages_out = num_pages;

    if (num_pages == 0) {
        return 0;
    }

    script = fopen(backing_path, "rt");
    if (script == NULL) {
        return 1;
    }

    page_table = malloc((size_t)num_pages * sizeof(int));
    if (page_table == NULL) {
        fclose(script);
        return 1;
    }

    // A3 1.2.1: Task 1 loads every page eagerly into the frame store.
    for (int page = 0; page < num_pages; page++) {
        int frame = find_free_frame();
        if (frame < 0) {
            fclose(script);
            mem_release_frames(page_table, page);
            free(page_table);
            return 1;
        }

        // A3 1.2.1: page_table[page] stores the physical frame number.
        // A3 1.2.1: page_table[page] stores the frame index used for address translation later.
        page_table[page] = frame;
        frame_table[frame].occupied = 1;
        frame_table[frame].page_num = page;
        frame_table[frame].last_used = clock_value++;
        snprintf(frame_table[frame].script_name, sizeof(frame_table[frame].script_name), "%s", script_name);

        // A3 1.2.1: each frame always holds exactly three line slots.
        for (int offset = 0; offset < PAGE_SIZE; offset++) {
            int physical_index = frame * PAGE_SIZE + offset;
            char *stored_line = NULL;

            if (fgets(line, sizeof(line), script) != NULL) {
                stored_line = strdup(line);
            } else {
                stored_line = strdup("");
            }

            if (stored_line == NULL) {
                fclose(script);
                mem_release_frames(page_table, page + 1);
                free(page_table);
                return 1;
            }

            frame_store[physical_index] = stored_line;
        }
    }

    fclose(script);
    *page_table_out = page_table;
    return 0;
}

int mem_load_initial_pages(const char *backing_path, const char *script_name,
                           int total_lines, int **page_table_out, int *num_pages_out) {
    // A3 1.2.2: demand paging - load only first 1-2 pages initially.
    FILE *script = NULL;
    int *page_table = NULL;
    // Total number of pages (including future pages not yet loaded)
    int num_pages = (total_lines + PAGE_SIZE - 1) / PAGE_SIZE;
    // Pages to load initially: 2 if enough lines, else 1
    int pages_to_load = (total_lines >= PAGE_SIZE) ? 2 : 1;
    char line[1000];

    *page_table_out = NULL;
    *num_pages_out = num_pages;

    if (num_pages == 0) {
        return 0;
    }

    script = fopen(backing_path, "rt");
    if (script == NULL) {
        return 1;
    }

    page_table = malloc((size_t)num_pages * sizeof(int));
    if (page_table == NULL) {
        fclose(script);
        return 1;
    }

    // Initialize all pages to -1 (not loaded)
    for (int i = 0; i < num_pages; i++) {
        page_table[i] = -1;
    }

    // A3 1.2.2: load only the first pages_to_load pages.
    for (int page = 0; page < pages_to_load && page < num_pages; page++) {
        int frame = find_free_frame();
        if (frame < 0) {
            fclose(script);
            mem_release_frames(page_table, page);
            free(page_table);
            return 1;
        }

        page_table[page] = frame;
        frame_table[frame].occupied = 1;
        frame_table[frame].page_num = page;
        frame_table[frame].last_used = clock_value++;
        snprintf(frame_table[frame].script_name, sizeof(frame_table[frame].script_name), "%s", script_name);

        // Load PAGE_SIZE lines into the frame
        for (int offset = 0; offset < PAGE_SIZE; offset++) {
            int physical_index = frame * PAGE_SIZE + offset;
            char *stored_line = NULL;

            if (fgets(line, sizeof(line), script) != NULL) {
                stored_line = strdup(line);
            } else {
                stored_line = strdup("");
            }

            if (stored_line == NULL) {
                fclose(script);
                mem_release_frames(page_table, page + 1);
                free(page_table);
                return 1;
            }

            frame_store[physical_index] = stored_line;
        }
    }

    // If we loaded fewer than all pages, seek to end of initial loaded content
    // This prepares for demand loading the rest later
    for (int remaining = pages_to_load * PAGE_SIZE; remaining < total_lines; remaining++) {
        if (fgets(line, sizeof(line), script) == NULL) {
            break;
        }
    }

    fclose(script);
    *page_table_out = page_table;
    return 0;
}

int mem_demand_load_page(int *page_table, int page_num, const char *backing_path,
                         const char *script_name, int total_lines) {
    // A3 1.2.2: load a specific page on demand when accessed.
    // Returns: 0 = success, 1 = page fault (had to evict), 2 = error
    FILE *script = NULL;
    int frame = -1;
    int num_pages = (total_lines + PAGE_SIZE - 1) / PAGE_SIZE;
    char line[1000];
    int had_to_evict = 0;

    // Sanity checks
    if (page_num < 0 || page_num >= num_pages) {
        return 2;
    }

    // Already loaded?
    if (page_table[page_num] >= 0) {
        return 0;
    }

    // Find a free frame
    frame = find_free_frame();
    if (frame < 0) {
        // A3 1.2.2: no free frame, evict a random one - prints "Page fault! Victim..." 
        frame = mem_evict_random_frame();
        if (frame < 0) {
            return 2;  // Error - eviction failed
        }
        
        // Update the page table of the evicted script to mark page as not loaded
        mem_update_page_table_for_evicted_frame(frame);
        had_to_evict = 1;
    } else {
        // A3 1.2.2: page fault without needing eviction (frame store not full)
        printf("Page fault!\n");
    }

    script = fopen(backing_path, "rt");
    if (script == NULL) {
        return 2;
    }

    // Seek to the start of the page
    int line_to_read = page_num * PAGE_SIZE;
    for (int i = 0; i < line_to_read; i++) {
        if (fgets(line, sizeof(line), script) == NULL) {
            fclose(script);
            return 2;
        }
    }

    // Load this page into the frame
    page_table[page_num] = frame;
    frame_table[frame].occupied = 1;
    frame_table[frame].page_num = page_num;
    frame_table[frame].last_used = clock_value++;
    snprintf(frame_table[frame].script_name, sizeof(frame_table[frame].script_name), "%s", script_name);

    for (int offset = 0; offset < PAGE_SIZE; offset++) {
        int physical_index = frame * PAGE_SIZE + offset;
        char *stored_line = NULL;

        if (fgets(line, sizeof(line), script) != NULL) {
            stored_line = strdup(line);
        } else {
            stored_line = strdup("");
        }

        if (stored_line == NULL) {
            fclose(script);
            clear_frame(frame);
            page_table[page_num] = -1;
            return 2;
        }

        frame_store[physical_index] = stored_line;
    }

    fclose(script);
    
    // Return 1 if we had to evict (process should be interrupted for page fault)
    return had_to_evict ? 1 : 0;
}

int mem_register_script(const char *script_name, const int *page_table, int num_pages, int line_count) {
    // A3 1.2.1: store one canonical mapping so later PCBs can share resident frames.
    int free_slot = -1;

    // A3 1.2.1: first load wins; later duplicates only clone its page table.
    if (find_loaded_script_index(script_name) >= 0) {
        return 0;
    }

    for (int i = 0; i < MAX_FRAMES; i++) {
        if (!loaded_scripts[i].in_use) {
            free_slot = i;
            break;
        }
    }

    if (free_slot < 0) {
        return 1;
    }

    if (clone_page_table(page_table, num_pages, &loaded_scripts[free_slot].page_table) != 0) {
        return 1;
    }

    loaded_scripts[free_slot].in_use = 1;
    loaded_scripts[free_slot].num_pages = num_pages;
    loaded_scripts[free_slot].line_count = line_count;
    snprintf(loaded_scripts[free_slot].script_name,
             sizeof(loaded_scripts[free_slot].script_name), "%s", script_name);
    return 0;
}

void mem_unregister_script(const char *script_name) {
    // A3 1.2.1: remove a canonical mapping if a load must be rolled back.
    int index = find_loaded_script_index(script_name);

    // A3 1.2.1: remove sharing metadata during rollback only.
    if (index < 0) {
        return;
    }

    free(loaded_scripts[index].page_table);
    loaded_scripts[index].page_table = NULL;
    loaded_scripts[index].in_use = 0;
    loaded_scripts[index].num_pages = 0;
    loaded_scripts[index].line_count = 0;
    loaded_scripts[index].script_name[0] = '\0';
}

void mem_release_frames(const int *page_table, int num_pages) {
    // A3 1.2.1: release only the frames named in a page table during rollback.
    if (page_table == NULL) {
        return;
    }

    // A3 1.2.1: only used to unwind failed loads; normal process exit keeps frames.
    for (int page = 0; page < num_pages; page++) {
        int frame = page_table[page];

        if (frame >= 0 && frame < MAX_FRAMES) {
            clear_frame(frame);
        }
    }
}

char *mem_get_frame_line(int frame, int offset) {
    // A3 1.2.1: convert frame + offset into one physical line in the frame store.
    int physical_index = frame * PAGE_SIZE + offset;

    // A3 1.2.1: scheduler fetches instructions through this physical frame lookup.
    if (frame < 0 || frame >= MAX_FRAMES || offset < 0 || offset >= PAGE_SIZE) {
        return NULL;
    }

    // A3 1.2.2: update LRU timestamp on frame access
    if (frame_table[frame].occupied) {
        frame_table[frame].last_used = clock_value++;
    }

    return frame_store[physical_index];
}
