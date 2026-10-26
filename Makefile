# c. elegans whole animal sim. c99, posix, libm only.
# no ncurses, no malloc in the main loop.

CC       = cc
CFLAGS   = -std=c99 -Wall -Wextra -pedantic -O3 -march=native
CPPFLAGS = -D_DEFAULT_SOURCE
LDLIBS   = -lm

BIN = elegans
SRC = src/main.c src/tui.c src/sim.c src/connectome.c src/neuron.c src/body.c \
      src/world.c src/modulators.c src/plasticity.c src/metrics.c src/rng.c \
      src/evolution.c
OBJ = $(SRC:.c=.o)
HDR = $(wildcard src/*.h)

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c $(HDR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# sanitized build for hunting memory and ub bugs
debug: CFLAGS := -std=c99 -Wall -Wextra -pedantic -O0 -g -fsanitize=address,undefined
debug: clean $(BIN)

run: $(BIN)
	./$(BIN)

batch: $(BIN)
	./$(BIN) --headless --duration 600 --seed 7

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all debug run batch clean

test: $(BIN)
	sh tests/run_tests.sh
