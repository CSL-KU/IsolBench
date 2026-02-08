CC = gcc
CXX = g++

GLOBAL_FLAGS = -O3 -Wall -march=native -g
CFLAGS = -std=gnu17 $(GLOBAL_FLAGS)
CXXFLAGS = -std=gnu++17 $(GLOBAL_FLAGS)

PGMS = latency bandwidth bandwidth-rt pll pagetype cpuhog

all: $(PGMS)

bandwidth-rt: bandwidth-rt.o
	$(CC) $(CFLAGS) $< -o $@ -lrt -lpthread

install:
	cp -v $(PGMS) /usr/local/bin

clean:
	rm -f *.o *~ $(PGMS)
