#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "shellmemory.h"

struct memory_struct {
    char *var;
    char *value;
};

struct memory_struct shellmemory[MEM_SIZE];

// For script storage
struct code_struct{
    char *line;
};
struct code_struct shell_code[MEM_SIZE];

int code_idx = 0;

int mem_load_script_line(char *line) {  
    if (code_idx < MEM_SIZE) {
        shell_code[code_idx].line = strdup(line);
        return code_idx++; // Return current index and increment
    }
    return -1; // Out of memory
}

char *mem_get_line(int index) {
    if (index >= 0 && index < 1000) return shell_code[index].line;
    return NULL;
}

void mem_cleanup_script(int start, int end) {  // Free memory used by a script from start to end
    for (int i = start; i <= end && i < MEM_SIZE; i++) {
        if (shell_code[i].line != NULL) {
            free(shell_code[i].line);
            shell_code[i].line = NULL;
        }
    }
    while (code_idx > 0 && shell_code[code_idx - 1].line == NULL) {
        code_idx--;
    }
}

// Helper functions
int match(char *model, char *var) {
    int i, len = strlen(var), matchCount = 0;
    for (i = 0; i < len; i++) {
        if (model[i] == var[i])
            matchCount++;
    }
    if (matchCount == len) {
        return 1;
    } else
        return 0;
}

// Shell memory functions

void mem_init(void) {
    int i;
    for (i = 0; i < MEM_SIZE; i++) {
        shellmemory[i].var = "none";
        shellmemory[i].value = "none";
        shell_code[i].line = NULL;
    }
    code_idx = 0;
}

// Set key value pair
void mem_set_value(char *var_in, char *value_in) {
    int i;

    for (i = 0; i < MEM_SIZE; i++) {
        if (strcmp(shellmemory[i].var, var_in) == 0) {
            shellmemory[i].value = strdup(value_in);
            return;
        }
    }

    //Value does not exist, need to find a free spot.
    for (i = 0; i < MEM_SIZE; i++) {
        if (strcmp(shellmemory[i].var, "none") == 0) {
            shellmemory[i].var = strdup(var_in);
            shellmemory[i].value = strdup(value_in);
            return;
        }
    }

    return;
}

//get value based on input key
char *mem_get_value(char *var_in) {
    int i;

    for (i = 0; i < MEM_SIZE; i++) {
        if (strcmp(shellmemory[i].var, var_in) == 0) {
            return strdup(shellmemory[i].value);
        }
    }
    return NULL;
}
