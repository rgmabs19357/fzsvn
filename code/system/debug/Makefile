#
#	Makefile for C languages
#

CC = gcc
CFLAGS = -Wall -ggdb
LDFLAGS = -ldl

ifneq (".deps", "$(wildcard .deps)")
	DEPS = .deps
endif

OBJS = dbg_print.o dbg_log.o dbg_segfault.o debug_test.o

TARGET = debug_test

.PHONY : all clean depclean

# for all target
all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

# for test target
test : $(TEST)


# for clean target
clean :
	rm -f *.o $(TARGET) $(TEST)


# for depclean target
depclean:
	rm -rf *.o $(TARGET) $(TEST) .deps


%.o : %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $< -MD -MF .deps/$(<:.c=.d)

$(DEPS) :
	@mkdir -p .deps


-include .deps/*.d


