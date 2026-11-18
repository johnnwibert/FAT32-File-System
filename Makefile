CC := gcc
CPPFLAGS := -Iinclude
CFLAGS := -g -Wall -Wextra -std=c99 -pedantic
OBJS := $(patsubst src/%.c,obj/%.o,$(wildcard src/*.c))

all: filesys

filesys: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

obj/%.o: src/%.c | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

obj:
	mkdir -p $@

run: filesys
	./filesys "$(IMAGE)"

clean:
	rm -rf obj filesys

-include $(OBJS:.o=.d)
.PHONY: all run clean
