CC=gcc
CXX=g++
PGMS=latency latency-mlp bandwidth pagetype bandwidth-rt attacker attacker2 attacker3 attacker4 slice-att gpuwrite attacker4-coffeelake ocl_simple_add

CFLAGS=-O2 -Wall -g
CXXFLAGS=$(CFLAGS)
LDFLAGS=-Wl,--no-as-needed -lrt
OCLLIB=-lOpenCL -lm

all: $(PGMS)

latency: latency.o
latency-mlp: latency-mlp.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $< -o $@
attacker2: attacker2.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $< -o $@
attacker3: attacker3.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $< -o $@
attacker4: attacker4.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $< -o $@
gpuwrite: gpuwrite.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $(OCLLIB) $< -o $@
ocl_simple_add: ocl_simple_add.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $(OCLLIB) $< -o $@
slice-att: slice-att.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $< -o $@
attacker4-coffeelake: attacker4-coffeelake.o
	$(CXX) $(CFLAGS) $(LDFLAGS) $(OCLLIB) $< -o $@

bandwidth: bandwidth.o
bandwidth-rt: bandwidth-rt.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ -lpthread -latomic
attacker.s: attacker.c
	$(CC) $(CFLAGS) -S -c $(LDFLAGS) $< -o $@ 
attacker: attacker.o attacker.s
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ -lpthread
pagetype: pagetype.o

install:
	cp -v $(PGMS) /usr/local/bin
clean:
	rm *.o *~ $(PGMS)
