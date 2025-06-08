# Run Lucene with LDB & Hiresperf

Before this,  you should have LDB LLVM, libldb, and hiresperf kernel module compiled and installed.

LDB polling interval can be set to 5 us, and hiresperf ~10 us.
## Compile the `LucenePlusPlus` library
```
git clone https://github.com/luceneplusplus/LucenePlusPlus.git
```
Inside the repo dir, we checkout to a specific commit just in case the latest version cannot be built we our Makefile.
```
git checkout 76dc90f2b65d81be018c499714ff11e121ba5585
```
Generate CMake files and drop-in the Makefile given by LDB.
```
mkdir build
cd build
cmake ..
cp ../llvm14-ldb/apps/LucenePlusPlus/build/Makefile .
```
But remember to make sure the `ROOT_PATH` path macros in the begining of the Makefile are correct!

Compiling requires a specific boost library version, so on ubuntu, run
```
sudo apt update
sudo apt install libboost-{atomic,chrono,date-time,filesystem,graph,iostreams,locale,log,math,program-options,random,regex,serialization,system,thread,wave,test,timer}1.74.0
```

Then hit `make -j$(nproc)`!

## Compile `lucene-server`

Recommend to make a copy of the source code.

```
cp -r ~/llvm14-ldb/apps/lucene-server ./
```
Then drop-in replace the `lucene-server.cc` and `freuqnet_terms.csv` from hiresperf repo.
```
cp ./hiresperf/apps/lucene-server/lucene-server.cc ./lucene-server
cp ./hiresperf/apps/lucene-server/frequent_terms.csv ./lucene-server
```
You also need `logger.h` from libldb
```
cp ../llvm14-ldb/libldb/include/ldb/logger.h .
```

Also copy the hiresperf api.

```
cp ./hiresperf/install/hrperf_api.h ./lucene-server
```
Now fix the lucene-server Makefile's `ROOT_PATH` and `LUCENE_PATH`, so that they points to the llvm14-ldb directory and compiled LucenePlusPlus directory correctly.

Then hit `make`!

## Compiling `lcclient`

The client is included in hiresperf repo, and can be compiled with ordinary g++.

So, maybe on another machine, we do
```
cp -r ./hiresperf/apps/lcclient ./
cd lcclient
g++ -o lcclient ./lcclient.cc
```

## Starting lucene server

Before starting the lucene-server, need to make sure there's a `test.csv` dataset present in the same directory.
(On our sapph2 server, there's a copy of this dataset in /home/yliang/lucene-server, this is a 2,000,000 line Amazon reviews dataset cleaned to remove special characters that breaks Lucene).

Then, to start the server, we need to use a command like
```
LD_PRELOAD=/home/cc/llvm14-ldb/libldb/libshim.so taskset -c 4,5,6,7,8 numactl --membind=0 ./lucene-server 8001 1 1 127.0.0.1 50052
```
Notably, the `LD_PRELOAD` need to points to the `libshim.so` built as part of `libldb`, which intercepts all the pthread activities (e.g. tracking the creation of new threads so that the stack scanner is aware of it). The `taskset` is to make sure the server threads run on the cores monitored by hiresperf, and sometimes we want to use `taskset` to avoid colocating hyperthreads in the same physical core. The `numactl` is used to avoid cross node memory access because that makes reasoning about memory bandwidth hard.

For the arguments passed to `lucene-server` itself, please checkout `lucene-server.cc`. Note that normally for analyzing performance consumption, we don't need peers (multiple server instances), so the above example command specifies the number of peers to be 1, for simplicity.

Note that the server needs about a minute to load and index the dataset. When it prints "Done: xxx documents", this means the server is ready to handle requests. At the same moment, the LDB log will start from empty and the Hiresperf kernel module will start polling PMCs (as there are code in `lucene-server.cc` to notify LDB & Hiresperf).

## Initiating `perf sched record`
On the server instance, should start recording scheduling events (for mapping thread_id to cores in later analysis),
```
sudo perf sched record -k CLOCK_MONOTONIC_RAW
```

Some distro releases' `perf` do not support this, in that case, please pull the Linux source code and build your own `perf`.

## Starting the client
On the client instance, you can launch the client code that generates search requests, where the args are
```
Usage: ./lcclient [options]
Options:
  -t, --threads <N>         Number of threads per server
  -s, --server <IP>         Server IP address
  -l, --load <RPS>          Offered load (RPS)
  -p, --port <PORT>         Specify a server port (repeat if using multiple server)
      --saturate            Try to saturate the server (client therads never sleep)
  -h, --help                Show this help message
```

An example command can be
```
./lcclient -t 10 -s 10.10.2.2 -l 100000 -p 8001
```
You will see some RPS statistics being print after all requests processed.

## Terminating Experiment
Unfortunately, now terminating the profiling still needs manual work : (

1. Interrupt the server if it doesn't terminate by itself
2. Under `hiresperf/workloads`, simply `make` then invoke the `./pause` executable to stop PMC polling on the server instance.
3. Interrupt the `perf sched` recording.

## Collected Data
All data collected includes 3 files
1. `ldb.data`
2. `perf.data`
3. `/hrperf_log.bin`
When have all these, you can proceed to use the parsing & analysis scripts.