CC=gcc
CXX=g++
PGMS=latency latency-mlp bandwidth bandwidth-rt pll pagetype cpuhog 

CFLAGS=-O3 -Wall -march=native -g
CXXFLAGS=$(CFLAGS)
LDFLAGS=-lrt -lpthread

all: $(PGMS)

# latency: latency.o
# latency-mlp: latency-mlp.o
# 	$(CXX) $(CFLAGS) $(LDFLAGS) $< -o $@
# bandwidth: bandwidth.o
# bandwidth-rt: bandwidth-rt.o
# 	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ -lpthread
# pagetype: pagetype.o

install:
	cp -v $(PGMS) /usr/local/bin
clean:
	rm *.o *~ $(PGMS)
