# Hiresperf: a high-resolution, low-overhead profiler based on LDB and performance counters

Related Papers:\
Granular Resource Demand Heterogeneity (HotOS'25) - [Link](https://sigops.org/s/conferences/hotos/2025/papers/hotos25-102.pdf)\
LDB: An Efficient Latency Profiling Tool (NSDI'24) - [Link](https://www.usenix.org/conference/nsdi24/presentation/cho)

## Getting Started
**Pull the repo and submodules**
```
git clone https://github.com/yizhuoliang/hiresperf.git
cd hiresperf/
git submodule update --init --recursive
```
**Make configurations**

Before building hiresperf, check and edit `src/config.h` carefully to make hardware-specific configurations. Especially, specify the microarchitecture and choose which cores to monitor. Intensive comments has been made in the config file.


**Build and install hiresperf**
```
make
make install
```
**Setup/cleanup the hiresperf kernel module**

Setup
```
sudo insmod ./install/hrperf.ko && sudo chmod 666 /dev/hrperf_device
```
Cleanup (for later)
```
sudo rmmod hrperf && sudo rm -f /hrperf_log.bin
```

**Building LDB-instrumented `clang` and apps**

For building LDB-instrumented compiler, see instructions in [the LDB repo](https://github.com/yizhuoliang/llvm14-ldb). This is actually somewhat complex.

Note that some apps requires further patching (e.g. toggling hiresperf start/pause, hint the compiler to avoid inlining key functions, etc). Such modified source files of each app is included in this repo's `apps/` directory (which is currently not so organized).

**Toggle hiresperf kernel module**
There are two ways, first is including calls to `hrperf_start()`/`hrperf_pause()` in your program, using the C or Python helper functions in `install/`.

The other way is to manually toggle it by
```
cd workloads
make
# or `sudo ./start` if you didn't run `sudo chmod 666 /dev/hrperf_device` beforehand.
./start
```
Either way it's just using `ioctl` to interact with the kernel module.

**Running the app to profile it**
Eventually, you will need to compose a command like
```
LD_PRELOAD=~/hiresperf/deps/llvm14-ldb/libldb/libshim.so numactl --physcpubind=4,5,6,7,8,9 --membind=0 ./my_instrumented_executable
```

Here we are doing
1. setting up LDB's shim layer library
2. use `numactl` or `taskset` to specify which cores the program runs on (or you can set core affinity directly in the program)
3. use `numactl` to specify which NUMA node to use
4. run your executable and give arguments

After this, you should get data files
1. a `ldb.data` file in your local directory, 
2. a `/hrperf_log.bin` file in the root directory,
3. a `perf.data` file containing the scheduling info

## Parsing and analyzing the results
Firstly spin-up a virtual env with our python dependencies:
```
source ./bin/workon.sh
```

Then, parsing both LDB, hiresperf, and analysis can be done with the scripts under `parsing/`, which all read and writes to a duckdb instance `analysis.duckdb`. More details in [this doc](docs/parsing.md).

For more ad-hoc explorations on the parsed data, install duckdb commandline tool [here](https://duckdb.org/docs/installation/?version=stable&environment=cli&platform=linux&download_method=direct&architecture=x86_64).