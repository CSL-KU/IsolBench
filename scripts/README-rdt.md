## RDT scripts

### Setup

Ensure that the MSR module is loaded:

	sudo modprobe msr
	
Install the pqos interface tool:

	sudo apt install intel-cmt-cat

Clone, build and install the bandwidth benchmark:

	git clone https://github.com/CSL-KU/IsolBench
	git checkout devel
	cd bench
	make bandwidth
	sudo cp bandwidth /usr/local/bin
	cd ..
	
For scripts that use it, install the MemGuard module:

	git clone https://github.com/heechul/memguard
	cd memguard
	make
	sudo insmod memguard.ko
	
### Running the RDT scripts

*Note that all scripts are meant to be run as root. As such, the commands given for each script can either be run with sudo, or a root level shell can be started using the sudo bash command.*

Each of the RDT scripts can be run as follows:

- rdt-corun.sh: effect of DoS attacks on a victim process without any isolation mechanisms enabled

       usage: ./rdt-corun.sh <victim-size> <victim-iterations> <corunner-size> <corunner-type>
       example: ./rdt-corun.sh 4096 10000 65536 write
       <victim-size>: size of the victim in kb (e.g. 4096 = 4mb)
       <victim-iterations>: how many times the victim should run (LLC-fitting victims should run more iterations, DRAM-fitting should run fewer)
       <corunner-size>: size of each individual corunner in kb (e.g. 65536 = 64mb)
       <corunner-type>: the types of accesses that the corunners will perform, should either be "read" or "write"
	
- rdt-corun-mba.sh: effect of DoS attacks on a victim process with MBA % settings enabled

       usage: ./rdt-corun-mba.sh <victim-size> <victim-iterations> <corunner-size> <corunner-type>		
       example: ./rdt-corun-mba.sh 4096 10000 65536 write
       # the same parameters are used from rdt-corun.sh
	
- rdt-corun-mba.sh: effect of DoS attacks on a victim process with MemGuard enabled

       usage: ./rdt-corun-memguard.sh <victim-size> <victim-iterations> <corunner-size> <corunner-type>		
       example: ./rdt-corun-memguard.sh 4096 10000 65536 write
       # the same parameters are used from rdt-corun.sh
	
- rdt-corun-sdvbs.sh: effect of DoS attacks on benchmarks from the SD-VBS suite
*his script should be run from the SD-VBS suite's benchmarks/ directory.*
	
       usage: ./rdt-corun-sdvbs.sh <corunner-size> <corunner-type>		
       example: ./rdt-corun-spec2017.sh 65536 write
       <corunner-size>: size of each individual corunner in kb (e.g. 65536 = 64mb)
       <corunner-type>: the types of accesses that the corunners will perform, should either be "read" or "write"
	
- rdt-corun-spec2017.sh: effect of DoS attacks on benchmarks from the SPEC2017 suite
*This script should be run from the SPEC2017 suite's root directory.*
	
       usage: ./rdt-corun-spec2017.sh <corunner-size> <corunner-type>			
       example: ./rdt-corun-spec2017.sh 65536 write
       # the same parameters are used from rdt-corun-sdvbs.sh
