//#define DEBUG 1

#ifdef DEBUG
#   define debug(...) fprintf(stderr, __VA_ARGS__)
#else
#   define debug(...)
// NDEBUG disables asserts
#   define NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>              // tolower, isdigit
#include <dirent.h>             // scandir
#include <unistd.h>             // chdir
#include <sys/stat.h>           // mkdir
// for run:
#include <sys/types.h>          // pid_t
#include <sys/wait.h>           // waitpid

#include "shellmemory.h"
#include "shell.h"
#include "pcb.h"
#include "ready_queue.h"
#include "scheduler.h"

int badcommand() {
    printf("Unknown Command\n");
    return 1;
}

// For source command only
int badcommandFileDoesNotExist() {
    printf("Bad command: File not found\n");
    return 3;
}

int badcommandMkdir() {
    printf("Bad command: my_mkdir\n");
    return 4;
}

int badcommandCd() {
    printf("Bad command: my_cd\n");
    return 5;
}

int help();
int quit();
int set(char *var, char *value);
int print(char *var);
int echo(char *tok);
int ls();
int my_mkdir(char *name);
int touch(char *path);
int cd(char *path);
int source(char *script);
int exec_cmd(char *args[], int arg_size);
int run(char *args[], int args_size);
int badcommandFileDoesNotExist();
int badcommandExec();
int badcommandExecPolicy();
int badcommandExecDuplicate();
int badcommandExecLoad();
int parse_policy(char *policy_text, SchedulePolicy *out_policy);
int load_and_schedule_programs(char *scripts[], int script_count, SchedulePolicy policy, int print_exec_load_error, int background_mode);

typedef struct ScriptPlan {
    char script_name[256];
    char backing_path[512];
    int needs_load;
    int line_count;
    int num_pages;
} ScriptPlan;

typedef struct LoadedScriptRollback {
    char script_name[256];
    int *page_table;
    int num_pages;
    int loaded_now;
} LoadedScriptRollback;

static int get_script_name_from_path(const char *path, char *script_name_out, size_t script_name_size) {
    // A3 1.2.1: duplicate detection is based on script filename, not the full source path.
    const char *base = strrchr(path, '/');
    base = (base == NULL) ? path : base + 1;

    if (*base == '\0' || snprintf(script_name_out, script_name_size, "%s", base) >= (int)script_name_size) {
        return 1;
    }

    return 0;
}

// Interpret commands and their arguments
int interpreter(char *command_args[], int args_size) {
    int i;

    // these bits of debug output were very helpful for debugging
    // the changes we made to the parser!
    debug("#args: %d\n", args_size);
#ifdef DEBUG
    for (size_t i = 0; i < args_size; ++i) {
        debug("  %ld: %s\n", i, command_args[i]);
    }
#endif

    if (args_size < 1) {
        // This shouldn't be possible but we are defensive programmers.
        fprintf(stderr, "interpreter called with no words?\n");
        exit(1);
    }

    for (i = 0; i < args_size; i++) {   // terminate args at newlines
        command_args[i][strcspn(command_args[i], "\r\n")] = 0;
    }

    if (strcmp(command_args[0], "help") == 0) {
        //help
        if (args_size != 1)
            return badcommand();
        return help();

    } else if (strcmp(command_args[0], "quit") == 0) {
        //quit
        if (args_size != 1)
            return badcommand();
        return quit();

    } else if (strcmp(command_args[0], "set") == 0) {
        //set
        if (args_size != 3)
            return badcommand();
        return set(command_args[1], command_args[2]);

    } else if (strcmp(command_args[0], "print") == 0) {
        if (args_size != 2)
            return badcommand();
        return print(command_args[1]);

    } else if (strcmp(command_args[0], "echo") == 0) {
        if (args_size != 2)
            return badcommand();
        return echo(command_args[1]);

    } else if (strcmp(command_args[0], "my_ls") == 0) {
        if (args_size != 1)
            return badcommand();
        return ls();

    } else if (strcmp(command_args[0], "my_mkdir") == 0) {
        if (args_size != 2)
            return badcommand();
        return my_mkdir(command_args[1]);

    } else if (strcmp(command_args[0], "my_touch") == 0) {
        if (args_size != 2)
            return badcommand();
        return touch(command_args[1]);

    } else if (strcmp(command_args[0], "my_cd") == 0) {
        if (args_size != 2)
            return badcommand();
        return cd(command_args[1]);

    } else if (strcmp(command_args[0], "source") == 0) {
        if (args_size != 2)
            return badcommand();
        return source(command_args[1]);

    } else if (strcmp(command_args[0], "run") == 0) {
        if (args_size < 2)
            return badcommand();
        return run(&command_args[1], args_size - 1);

    } else if (strcmp(command_args[0], "exec") == 0) {
        if (args_size < 3 || args_size > 6)
            return badcommandExec();
        return exec_cmd(&command_args[1], args_size - 1);

    } else
        return badcommand();
}

int help() {

    // note the literal tab characters here for alignment
    char help_string[] = "COMMAND			DESCRIPTION\n \
help			Displays all the commands\n \
quit			Exits / terminates the shell with “Bye!”\n \
set VAR STRING		Assigns a value to shell memory\n \
print VAR		Displays the STRING assigned to VAR\n \
source SCRIPT.TXT		Executes the file SCRIPT.TXT\n \
exec p1 [p2] [p3] POLICY	Executes up to 3 programs\n ";
    printf("%s\n", help_string);
    return 0;
}

int badcommandExec() {
    printf("Bad command: exec\n");
    return 1;
}

int badcommandExecPolicy() {
    printf("Bad command: exec policy\n");
    return 1;
}

int badcommandExecDuplicate() {
    printf("Bad command: exec duplicate program\n");
    return 1;
}

int badcommandExecLoad() {
    printf("Bad command: exec load\n");
    return 1;
}

int parse_policy(char *policy_text, SchedulePolicy *out_policy) {
    // A2 1.2.2: Parse user policy tokens exactly as specified by the assignment.
    if (strcmp(policy_text, "FCFS") == 0) {
        *out_policy = POLICY_FCFS;
        return 0;
    }
    if (strcmp(policy_text, "SJF") == 0) {
        *out_policy = POLICY_SJF;
        return 0;
    }
    if (strcmp(policy_text, "RR") == 0) {
        *out_policy = POLICY_RR;
        return 0;
    }
    if (strcmp(policy_text, "AGING") == 0) {
        *out_policy = POLICY_AGING;
        return 0;
    }
    if (strcmp(policy_text, "RR30") == 0) {
        *out_policy = POLICY_RR30;
        return 0;
    }
    return 1;
}

static void rollback_loaded_scripts(LoadedScriptRollback loaded_scripts[], int count) {
    // A3 1.2.1: unwind partially completed eager loads on any failure path.
    for (int i = 0; i < count; i++) {
        if (loaded_scripts[i].loaded_now) {
            // A3 1.2.1: remove the shared-script registry entry, then free its frames.
            mem_unregister_script(loaded_scripts[i].script_name);
            mem_release_frames(loaded_scripts[i].page_table, loaded_scripts[i].num_pages);
        }
        free(loaded_scripts[i].page_table);
        loaded_scripts[i].page_table = NULL;
    }
}

static void free_planned_pcbs(PCB *pcbs[], int count) {
    // A3 1.2.1: helper for cleaning up PCB allocations before queue insertion.
    for (int i = 0; i < count; i++) {
        free_pcb(pcbs[i]);
        pcbs[i] = NULL;
    }
}

int load_and_schedule_programs(char *scripts[], int script_count, SchedulePolicy policy, int print_exec_load_error, int background_mode) {
    // A3 1.2.1: copy scripts into backing_store and plan eager page loading.
    ScriptPlan plans[3] = { 0 };
    LoadedScriptRollback loaded_scripts[3] = { 0 };
    PCB *pcbs[3] = { NULL, NULL, NULL };
    int total_new_pages = 0;
    int loaded_count = 0;

    for (int i = 0; i < script_count; i++) {
        int already_planned = -1;

        if (get_script_name_from_path(scripts[i], plans[i].script_name, sizeof(plans[i].script_name)) != 0) {
            if (print_exec_load_error) {
                return badcommandExecLoad();
            }
            return 1;
        }

        // A3 1.2.1: if the script is already loaded, later PCBs will reuse its frames.
        if (mem_is_script_loaded(plans[i].script_name)) {
            continue;
        }

        // A3 1.2.1: avoid copying or loading the same new script twice within one exec call.
        for (int j = 0; j < i; j++) {
            if (strcmp(plans[j].script_name, plans[i].script_name) == 0) {
                already_planned = j;
                break;
            }
        }

        if (already_planned >= 0) {
            if (plans[already_planned].needs_load) {
                plans[i].needs_load = 0;
            }
            continue;
        }

        // A3 1.2.1: each unique script name is copied into backing_store exactly once.
        if (mem_copy_script_to_backing_store(scripts[i],
                                             plans[i].script_name, sizeof(plans[i].script_name),
                                             plans[i].backing_path, sizeof(plans[i].backing_path)) != 0) {
            if (print_exec_load_error) {
                return badcommandExecLoad();
            }
            return 1;
        }

        // A3 1.2.1: eager loading still needs full line count up front to size the page table.
        plans[i].line_count = mem_count_script_lines(plans[i].backing_path);
        if (plans[i].line_count < 0) {
            if (print_exec_load_error) {
                return badcommandExecLoad();
            }
            return 1;
        }

        // A3 1.2.1: round up so 1-3 lines use 1 page, 4-6 lines use 2 pages, and so on.
        plans[i].num_pages = (plans[i].line_count + PAGE_SIZE - 1) / PAGE_SIZE;
        plans[i].needs_load = 1;
        total_new_pages += plans[i].num_pages;
    }

    // A3 1.2.1: Task 1 requires enough free frames for all pages before starting the load.
    if (mem_count_free_frames() < total_new_pages) {
        if (print_exec_load_error) {
            return badcommandExecLoad();
        }
        return 1;
    }

    for (int i = 0; i < script_count; i++) {
        int *page_table = NULL;
        int num_pages = 0;

        if (!plans[i].needs_load) {
            continue;
        }

        // A3 1.2.1: load every page from backing_store into a frame now.
        if (mem_load_script_from_backing_store(plans[i].backing_path, plans[i].script_name,
                                               plans[i].line_count, &page_table, &num_pages) != 0) {
            rollback_loaded_scripts(loaded_scripts, loaded_count);
            if (print_exec_load_error) {
                return badcommandExecLoad();
            }
            return 1;
        }

        // A3 1.2.1: register the loaded script so duplicate exec can share frames.
        if (mem_register_script(plans[i].script_name, page_table, num_pages, plans[i].line_count) != 0) {
            mem_release_frames(page_table, num_pages);
            free(page_table);
            rollback_loaded_scripts(loaded_scripts, loaded_count);
            if (print_exec_load_error) {
                return badcommandExecLoad();
            }
            return 1;
        }

        // A3 1.2.1: remember enough state to undo this load if a later step fails.
        loaded_scripts[loaded_count].loaded_now = 1;
        loaded_scripts[loaded_count].num_pages = num_pages;
        loaded_scripts[loaded_count].page_table = page_table;
        snprintf(loaded_scripts[loaded_count].script_name,
                 sizeof(loaded_scripts[loaded_count].script_name), "%s", plans[i].script_name);
        loaded_count++;
    }

    // A3 1.2.1: each PCB keeps a logical PC and its own page-table copy.
    for (int i = 0; i < script_count; i++) {
        int *page_table = NULL;
        int num_pages = 0;
        int line_count = 0;

        // A3 1.2.1: clone shared page mappings into a per-process page table.
        if (mem_clone_script_page_table(plans[i].script_name, &page_table, &num_pages, &line_count) != 0) {
            rollback_loaded_scripts(loaded_scripts, loaded_count);
            free_planned_pcbs(pcbs, i);
            if (print_exec_load_error) {
                return badcommandExecLoad();
            }
            return 1;
        }

        pcbs[i] = make_pcb(plans[i].script_name, line_count, num_pages, page_table);
        if (pcbs[i] == NULL) {
            free(page_table);
            rollback_loaded_scripts(loaded_scripts, loaded_count);
            free_planned_pcbs(pcbs, i);
            if (print_exec_load_error) {
                return badcommandExecLoad();
            }
            return 1;
        }
    }

    // A3 1.2.1: the registry keeps its own copy, so these temporary rollback tables can go away.
    for (int i = 0; i < loaded_count; i++) {
        free(loaded_scripts[i].page_table);
        loaded_scripts[i].page_table = NULL;
    }

    for (int i = 0; i < script_count; i++) {
        if (policy == POLICY_AGING) {
            ready_queue_insert_sorted(pcbs[i]);
        } else {
            ready_queue_add_to_tail(pcbs[i]);
        }
    }

    if (background_mode) {
        return scheduler_run_background(policy);
    }

    return scheduler_run(policy);
}

int quit() {
    printf("Bye!\n");
    
    // For background mode, we need to wait for threads to finish
    // The scheduler_quit flag will be set when all jobs are done
    if (scheduler_is_multithreaded()) {
        // Give threads time to finish
        while (scheduler_is_active()) {
            usleep(10000);  // Wait 10ms
        }
        scheduler_join_workers();
    }
    exit(0);
}

int set(char *var, char *value) {
    mem_set_value(var, value);
    return 0;
}

int print(char *var) {
    char *value = mem_get_value(var);
    if (value) {
        printf("%s\n", value);
        free(value);
    } else {
        printf("Variable does not exist\n");
    }
    return 0;
}

int echo(char *tok) {
    int must_free = 0;
    // is it a var?
    if (tok[0] == '$') {
        tok++;                  // advance pointer, so that tok is now the stuff after '$'
        tok = mem_get_value(tok);
        if (tok == NULL) {
            tok = "";           // must use empty string, can't pass NULL to printf
        } else {
            must_free = 1;
        }
    }

    printf("%s\n", tok);

    // memory management technically optional for this assignment
    if (must_free) free(tok);

    return 0;
}

// We can hide dotfiles in ls using either the filter operand to scandir,
// or by checking the first character ourselves when we go to print
// the names. That would work, and is less code, but this is more robust.
// And this is also better since it won't allocate extra dirents.
int ls_filter(const struct dirent *d) {
    if (d->d_name[0] == '.') return 0;
    return 1;
}

int ls_compare_char(char a, char b) {
    // assumption: a,b are both either digits or letters.
    // If this is not true, the characters will be effectively compared
    // as ASCII when we do the lower_a - lower_b fallback.

    // if both are digits, compare them
    if (isdigit(a) && isdigit(b)) {
        return a - b;
    }
    // if only a is a digit, then b isn't, so a wins.
    if (isdigit(a)) {
        return -1;
    }

    // lowercase both letters so we can compare their alphabetic position.
    char lower_a = tolower(a), lower_b = tolower(b);
    if (lower_a == lower_b) {
        // a and b are the same letter, possibly in different cases.
        // If they are really the same letter, this returns 0.
        // Otherwise, it's negative if A was capital,
        // and positive if B is capital.
        return a - b;
    }

    // Otherwise, compare their alphabetic position by comparing
    // them at a known case.
    return lower_a - lower_b;
}

int ls_compare_str(const char *a, const char *b) {
    // a simple strcmp implementation that uses ls_compare_char.
    // We only check if *a is zero, since if *b is zero earlier,
    // it would've been unequal to *a at that time and we would return.
    // If *b is zero at the same point or later than *a, we'll exit the
    // loop and return the correct value with the last comparison.

    while (*a != '\0') {
        int d = ls_compare_char(*a, *b);
        if (d != 0) return d;
        a++, b++;
    }
    return ls_compare_char(*a, *b);
}

int ls_compare(const struct dirent **a, const struct dirent **b) {
    return ls_compare_str((*a)->d_name, (*b)->d_name);
}

int ls() {
    // straight out of the man page examples for scandir
    // alphasort uses strcoll instead of strcmp,
    // so we have to implement our own comparator to match the ls spec.
    // Note that the test cases weren't very picky about the specified order,
    // so if you just used alphasort with scandir, you should have passed.
    // This was intentional on our part.
    struct dirent **namelist;
    int n;

    n = scandir(".", &namelist, NULL, ls_compare);
    if (n == -1) {
        // something is catastrophically wrong, just give up.
        perror("my_ls couldn't scan the directory");
        return 0;
    }

    for (size_t i = 0; i < n; ++i) {
        printf("%s\n", namelist[i]->d_name);
        free(namelist[i]);
    }
    free(namelist);

    return 0;
}

int str_isalphanum(char *name) {
    for (char c = *name; c != '\0'; c = *++name) {
        if (!(isdigit(c) || isalpha(c))) return 0;
    }
    return 1;
}

int my_mkdir(char *name) {
    int must_free = 0;

    debug("my_mkdir: ->%s<-\n", name);

    if (name[0] == '$') {
        ++name;
        // lookup name
        name = mem_get_value(name);
        debug("  lookup: %s\n", name ? name : "(NULL)");
        if (name) {
            // name exists, should free whatever we got
            must_free = 1;
        }
    }
    if (!name || !str_isalphanum(name)) {
        // either name doesn't exist, or isn't valid, error.
        if (must_free) free(name);
        return badcommandMkdir();
    }
    // at this point name is definitely OK

    // 0777 means "777 in octal," aka 511. This value means
    // "give the new folder all permissions that we can."
    int result = mkdir(name, 0777);

    if (result) {
        // description doesn't specify what to do in this case,
        // (including if the directory already exists)
        // so we just give an error message on stderr and ignore it.
        perror("Something went wrong in my_mkdir");
    }

    if (must_free) free(name);
    return 0;
}

int touch(char *path) {
    // we're told we can assume this.
    assert(str_isalphanum(path));
    // if things go wrong, just ignore it.
    FILE *f = fopen(path, "a");
    fclose(f);
    return 0;
}

int cd(char *path) {
    // we're told we can assume this.
    assert(str_isalphanum(path));

    int result = chdir(path);
    if (result) {
        // chdir can fail for several reasons, but the only one we need
        // to handle here for the spec is the ENOENT reason,
        // aka Error NO ENTry -- the directory doesn't exist.
        // Since that's the only one we have to handle, we'll just assume
        // that that's what happened.
        // Alternatively, you can check if the directory exists
        // explicitly first using `stat`. However it is often better to
        // simply try to use a filesystem resource and then recover when
        // you can't, rather than trying to validate first. If you validate
        // first while two users are on the system, there's a race condition!
        return badcommandCd();
    }
    return 0;
}

int source(char *script) {
    // A3 1.2.1: source now enters the same backing-store and frame-loading path as exec.
    char *scripts[1];

    FILE *p = fopen(script, "rt");
    if (p == NULL) {
        return badcommandFileDoesNotExist();
    }
    fclose(p);

    scripts[0] = script;
    return load_and_schedule_programs(scripts, 1, POLICY_FCFS, 0, 0);
}

int exec_cmd(char *args[], int arg_size) {
    // A3 1.2.1: duplicate script names are allowed now, so only policy/flags are parsed here.
    // Detect background mode (#) and MT option - they can be in any order at the end
    int background_mode = 0;
    int mt_detected = 0;
    
    // Strip both # and MT flags from the end, in any order
    while (arg_size > 0) {
        if (strcmp(args[arg_size-1], "MT") == 0) {
            mt_detected = 1;
            arg_size--;
        } else if (strcmp(args[arg_size-1], "#") == 0) {
            background_mode = 1;
            arg_size--;
        } else {
            break;
        }
    }
    
    int script_count = arg_size - 1;
    char *policy_text = args[arg_size - 1];
    SchedulePolicy policy;

    if (script_count < 1 || script_count > 3) {
        return badcommandExec();
    }

    if (parse_policy(policy_text, &policy) != 0) {
        return badcommandExecPolicy();
    }

    // Enable MT only if flag is present in THIS exec
    if (mt_detected) {
        scheduler_enable_multithreaded();
    } else {
        scheduler_disable_multithreaded();
    }

    return load_and_schedule_programs(args, script_count, policy, 1, background_mode);
}

int run(char *args[], int arg_size) {
    // copy the args into a new NULL-terminated array.
    char **adj_args = calloc(arg_size + 1, sizeof(char *));
    for (int i = 0; i < arg_size; ++i) {
        adj_args[i] = args[i];
    }

    // always flush output streams before forking.
    fflush(stdout);
    // attempt to fork the shell
    pid_t pid = fork();
    if (pid < 0) {
        // fork failed. Report the error and move on.
        perror("fork() failed");
        return 1;
    } else if (pid == 0) {
        // we are the new child process.
        execvp(adj_args[0], adj_args);
        perror("exec failed");
        // The parent and child are sharing stdin, and according to
        // a part of the glibc documentation that you are **not**
        // expected to know for this course, a shared input handle
        // should be fflushed (if it is needed) or closed
        // (if it is not). Handling this exec error case is not even
        // necessary, but let's do it right.
        // (Failure to do this can result in the parent process
        // reading the remaining input twice in batch mode.)
        fclose(stdin);
        exit(1);
    } else {
        // we are the parent process.
        waitpid(pid, NULL, 0);
    }

    return 0;
}
