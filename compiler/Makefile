CC      = clang
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -g -Isrc
SRCS    = src/main.c src/arena.c src/error.c src/lexer.c \
          src/parser.c src/sema.c src/codegen.c
OUT     = przp

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OUT)
