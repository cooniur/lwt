COBJS		= main.o lwt.o dlinkedlist.o
CFLAGS		= -O3 -I. -Wall -Wextra -std=gnu99
#CFLAGS		= -g -I. -Wall -Wextra -std=gnu99
CC			= gcc

AOBJS		= lwt_trampoline.o
AFLAGS		=
AS			= gcc

BIN			= test
BFLAGS		= -O3 -I. -Wall -Wextra -std=gnu99
#BFLAGS		= -g -I. -Wall -Wextra -std=gnu99
LD			= ld

all: $(BIN)

$(COBJS) : %.o : %.c
	$(info ********** Start making project **********)
	$(info --> Compiling C objs...)
	$(CC) $(CFLAGS) -o $@ -c $<
	$(info --> C objs generated...)
	
$(AOBJS) : %.o : %.s
	$(info --> Compiling assembly objs...)
	$(AS) $(AFLAGS) -o $@ -c $<
	$(info --> Assembly objs generated...)

$(BIN): $(COBJS) $(AOBJS)
	$(info --> Linking objects...)
	$(CC) $(BFLAGS) -o $(BIN) $^
	rm -rf $(COBJS) $(AOBJS)
	$(info --> Finished, run ./$(BIN) to start.)
	$(info ********** End of making project **********)

clean:
	$(info ********** Start cleaning project **********)
	$(info --> Cleaning projects...)
	rm -rf $(BIN) $(COBJS) $(AOBJS)
	$(info --> Projects cleaned.)
	$(info ********** End of cleaning project **********)
