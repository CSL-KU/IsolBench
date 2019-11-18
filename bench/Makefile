CC=gcc
CXX=g++
PGMS=latency latency-mlp bandwidth pagetype bandwidth-rt attacker attacker2 attacker3

CFLAGS=-O2 -Wall -g
CXXFLAGS=$(CFLAGS)
LDFLAGS=-Wl,--no-as-needed -lrt

all: $(PGMS)

latency: latency.o
latency-mlp: latency-mlp.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $< -o $@
attacker2: attacker2.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $< -o $@
attacker3: attacker3.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $< -o $@

bandwidth: bandwidth.o
bandwidth-rt: bandwidth-rt.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ -lpthread
attacker.s: attacker.c
	$(CC) $(CFLAGS) -S -c $(LDFLAGS) $< -o $@ 
attacker: attacker.o attacker.s
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ -lpthread
pagetype: pagetype.o

clean:
	rm *.o *~ $(PGMS)
