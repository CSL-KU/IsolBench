CC=gcc
CXX=g++
PGMS=latency latency-mlp bandwidth pagetype 

CFLAGS=-O2 -Wall -g
CXXFLAGS=$(CFLAGS)
LDFLAGS=-Wl,--no-as-needed -lrt

all: $(PGMS)

latency: latency.o
latency-mlp: latency-mlp.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $< -o $@
bandwidth: bandwidth.o
pagetype: pagetype.o

clean:
	rm *.o *~ $(PGMS)
