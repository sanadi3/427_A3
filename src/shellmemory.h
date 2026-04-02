#ifndef SHELLMEMORY_H
#define SHELLMEMORY_H

#define MEM_SIZE 1000

void mem_init(void);
char *mem_get_value(char *var);
void mem_set_value(char *var, char *value);

int mem_load_script_line(char *line);
char *mem_get_line(int index);
void mem_cleanup_script(int start, int end);

#endif
