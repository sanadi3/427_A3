# A3 Task 1 Handoff

## What changed and why

In A2, scripts were loaded into one flat chunk of code memory and the PCB just tracked offsets into that block. A3 needs paging, so Task 1 reshapes the internals now: code lives in frames, the PCB uses a logical PC and a page table, and scripts are copied to a backing store. The shell still behaves like A2 from the outside for normal `exec` and `source` runs.

## Memory layout

Memory is now split into two regions: a frame store for script code and a variable store for `set` / `print`. The frame store is divided into 3-line frames. Both sizes are compile-time settings, so you build with:

```
make mysh framesize=X varmemsize=Y
```

On startup the shell prints:

```
Frame Store Size = X; Variable Store Size = Y
```

## PCB changes

The old A2 fields `start`, `end`, and physical `pc` are gone. The PCB now stores `script_name`, logical `PC`, `num_pages`, and `page_table`. The page table stores one frame index per page, or `-1` if that page is not loaded.

## How instruction fetch works now

Instruction fetch is now `PC → page → frame → physical slot`. The translation happens in `get_current_instruction` in `src/scheduler.c` — that is the function to look at first, and the one that Task 2 will modify to trigger page faults when `page_table[page] == -1`.

## Backing store

Scripts are copied into `backing_store/` before they are loaded into frames. That directory is wiped and recreated once at shell startup, so each run starts clean. Task 2 will read from it when loading pages on demand.

## Process sharing

`exec prog1 prog1 RR` is legal now. The script is loaded into frames once, but each process gets its own PCB and its own cloned page-table array. Frames are shared, per-process execution state is not.

## New functions worth knowing

- `get_current_instruction` — `src/scheduler.c` — translates logical `PC` into the actual line to execute. **This is where Task 2 page fault logic goes.**
- `mem_load_script_from_backing_store` — `src/shellmemory.c` — loads all pages of one script into free frames and fills a page table.
- `mem_clone_script_page_table` — `src/shellmemory.c` — clones a script's page table into a new PCB-owned array.
- `mem_is_script_loaded` — `src/shellmemory.c` — checks whether a script is already resident in frames.
- `mem_get_frame_line` — `src/shellmemory.c` — returns the line stored at a given frame index and offset (0–2).
- `mem_copy_script_to_backing_store` — `src/shellmemory.c` — copies a script file into the backing store.
- `mem_ensure_backing_store` — `src/shellmemory.c` — wipes and recreates `backing_store/` at startup.

## Verifying it works

Run TC1 and TC2 from the A3 test suite — neither has page faults, so output should match A2 exactly. If those pass, the scaffolding is solid.

## What's not changing until Task 2

Scheduling logic, all shell commands, and the variable store are unchanged from A2.