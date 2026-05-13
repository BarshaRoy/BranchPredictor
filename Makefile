# C compiler used to build the simulator
CC = gcc

# Optimization and warning flags for the final simulator build
CFLAGS = -O3 -Wall -Wextra -std=c11

# Default target. Typing "make" builds the executable named sim
all: sim

# Build the simulator from the C source file sim.c
sim: sim.c
	$(CC) $(CFLAGS) -o sim sim.c

# Remove generated build files so the project can be rebuilt from scratch with "make"
clean:
	rm -f sim *.o
