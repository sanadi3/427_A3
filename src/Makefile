CC=gcc
CFLAGS=
# A3 1.2.1: compile-time sizes for frame store and variable store.
framesize ?= 18
varmemsize ?= 10
CPPFLAGS += -D FRAME_STORE_SIZE=$(framesize) -D VAR_STORE_SIZE=$(varmemsize)
FMT=indent
OBJS=shell.o interpreter.o shellmemory.o pcb.o ready_queue.o scheduler.o

mysh: shell.c interpreter.c shellmemory.c pcb.c ready_queue.c scheduler.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c shell.c interpreter.c shellmemory.c pcb.c ready_queue.c scheduler.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o mysh $(OBJS)

style: shell.c shell.h interpreter.c interpreter.h shellmemory.c shellmemory.h
	$(FMT) $?

clean: 
	$(RM) mysh; $(RM) *.o; $(RM) *~
