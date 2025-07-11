CC = gcc
CXX = g++
CFLAGS = -O3 -Wall -march=native -g
CXXFLAGS = $(CFLAGS)

PGMS = latency latency-mlp bandwidth bandwidth-rt pll pagetype cpuhog

all: $(PGMS)

bandwidth-rt: bandwidth-rt.o
	$(CC) $(CFLAGS) $< -o $@ -lrt -lpthread

install:
	cp -v $(PGMS) /usr/local/bin

clean:
	rm -f *.o *~ $(PGMS)