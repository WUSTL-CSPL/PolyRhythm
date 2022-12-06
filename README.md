# PolyRhythm README

## About PolyRhythm

PolyRhythm is a three-phase attack template that combines primitives across multiple architectural and kernel-based channels:

1. Phase 1 uses an offline genetic algorithm (GA) to tune attack parameters based on the target hardware and OS platform
2. Phase 2 performs an online search for regions of the attack parameter space where contention is most likely
3. Phase 3 runs the attack primitives, using online reinforcement learning (RL) to adapt to dynamic execution patterns in the victim task.

More details about PolyRhythm can be found in the following paper:

*A. Li, M. Sudvarg, H. Liu, Z. Yu, C. Gill, N. Zhang. "PolyRhythm: Adaptive Tuning of a Multi-Channel Attack Template for Timing Interference." IEEE Real-Time Systems Symposium (RTSS), 2022*

## Getting Started

To retrieve and build PolyRhythm:


    $ git clone -b rtss2022artifact-01 https://github.com/WUSTL-CSPL/PolyRhythm
    $ cd PolyRhythm
    $ mkdir build && cd build
    $ cmake .. && make

PolyRhythm can be run with an RL model that dynamically selects attack primitives to target specific contention channels. Communication between the model and attack threads is via shared memory regions; the model's Python scripts use a C-extension to use the memory. To build the extension:

    $ cd ../RL_DDPG/C-extension
    $ make
    
## Running Specified Attack Primitives

The `polyrhythm` binary produced by the build process takes one or more sets of parameters of the following pattern:

    <primitive> <nthreads> <param1> <param2> <param3> <param4> <online>
    
This tells it to launch `nthreads` instances of the `primitive` with the given parameters.

The parameter `online` takes the value `0` or `1` to specify whether to run Phase 2, the online contention region search.

Some primitives take fewer than 4 parameters, or ignore the `online` option. In this case, arguments must still be passed to the command-line, but are ignored (0 values can be used).

Primitives and corresponding parameters are listed below:

__Cache Attack__

Name: `cache`

Targets shared cache resources, attempting to cause delays in miss status holding registers or the writeback buffer even when isolation techniques such as cache coloring or partitioning are in use. Allocates an array then iteratively writes to array elements.

Parameters:

1. `stride` (iteration step as a multiple of the cache line size)
2. `size` (array size, in kB)
3. (`empty`)
4. (`empty`)
5. `online`: online attack attempts to find an eviction set

Note: PolyRhythm assumes the target platform uses a 64 Byte cache line. To change this, set the `CACHE_LINE` constant in `src/Cache_Attacks.c`

__Memory Bandwidth__

Name: `memory`

Targets shared memory resources that constrain memory bandwidth, e.g., the bus and memory controller. Generates a high volume of requests to main memory.

Parameters:

1. `nthreads` (number of threads to launch per PolyRhythm attack instance)
2. `size` (array size, as a multiple of the last-level cache size)
3. `pattern` (access pattern, 0: read, 1: write)
4. (`empty`)
5. `online`: online attack attempts to find a memory region that overlaps with victim usage

Note: Requires defining the last level cache size by setting the `LLC_CACHE_SIZE` constant in `include/Attacks.h`

__DRAM Row Buffer__

Name: `row_buffer`

Targets the DRAM row buffer, attempting to evict victim data held in the buffer, increasing memory request latency. Allocates two arrays, swaps and modifies values between them. 

Parameters:

1. `stride` (iteration step size, for sequential access)
2. `size` (array size, as a multiple of the last-level cache size)
3. `pattern` (0: iterate sequentially over array, 1: access array elements at random)
4. (`empty`)
5. `online`: online attack attempts to find a DRAM row region that overlaps with victim usage

Note: Requires defining the last level cache size by setting the `LLC_CACHE_SIZE` constant in `include/Attacks.h`

__TLB Page Eviction__

Name: `tlb`

Attempts to evict victim page entries from the translation lookaside buffer (TLB). Maps memory pages, zeroes the mapped region, then marks each page read-only, copies contents to the stack, then unmaps the page.

Parameters:

1. `pages` (number of pages to map for each iteration)
2. (`empty`)
3. (`empty`)
4. (`empty`)
5. `online`: not supported

Note: PolyRhythm assumes the target platform uses a 4kB page size. To change this, set the `PAGE_SIZE` constant in `include/Attacks.h`

__Network I/O__

Name: `network`

Attempts to cause contention in the network driver stack (not the network interface itself) by flooding the local loopback address with UDP traffic.

Parameters:

1. `size` (packet size, in Bytes)
2. `domain` (address family to target, 0: ipv4, 1: unix, 2: ipv6)
3. (`empty`)
4. (`empty`)
5. `online`: online attack iterates over open ports, finding the port/domain combination that generates the most contention

__Block Device I/O__

Name: `disk_io`

Attempts to cause contention in the block device driver stack by producing a large number of I/O operations. This should overwhelm the I/O scheduler and cause contention in the request queues. Opens a file, sets file system advice to `POSIX_FADV_RANDOM`, then generates a large number of short writes.

Parameters:

1. `filesize` (size of the file, as a multiple of page size)
2. `writesize` (size of string written in each operation, in Bytes)
3. `stride` (iteration step size, for sequential access)
4. `pattern` (0: iterate sequentially over array, 1: access array elements at random)
5. `online`: not supported

Note: PolyRhythm assumes the target platform uses a 4kB page size. To change this, set the `PAGE_SIZE` constant in `include/Attacks.h`

__Fork Bomb__

Name: `spawn`

Continuously forks new processes (each with its own address space) to cause contention in the scheduler, process data structure allocator, IPC endpoint queues, etc.

Parameters:

1. `nthreads` (Number of threads to spawn)
2. (`empty`)
3. (`empty`)
4. (`empty`)
5. `online`: not supported



### Example

To have PolyRhythm launch two instances of the cache attack with a stride of 1 cache line, over an 835kB region, using online search for contention regions; and launch once instance of the DRAM row buffer attack, using an interation step of size 1, a region 15 times the size of the LLC, and sequential access, use the following command:

    ./polyrhythm
        cache 2 1 835 0 0 1
        row_buffer 1 1 15 0 0 0

## Tuning Platform-Specific Parameters

PolyRhythm uses an offline genetic algorithm (GA) to tune attack parameters based on the target hardware and OS platform. This requires you to have root access to a copy of the target platform, but does not require a copy of the victim workloads that will ultimately be attacked. The GA runs `polyrhythm` to tune each primitive independently, finding the parameters that maximize its interference potential (measured using various performance counters) over a representative victim task that aggregates several benchmarks from the __stress-ng__ suite. By running several instances of `polyrhythm` with guessed parameters at each generation, the GA can converge to an optimal set of parameters for each primitive.

To perform the tuning:

### 1. Turn on hardware events
    
    $ sudo -i
    ### Allow use of (almost) all events by all users
    $ echo -1 > /proc/sys/kernel/perf_event_paranoid
    ### Exit su 
    $ exit

### 2. Retrieve and build the victim workloads

__CortexSuite__

CortexSuite, previously known as the San Diego Vision Benchmark Suite (SD-VBS), is a "suite of diverse vision applications drawn from the vision domain." More information can be found in the paper:

*S. Thomas, C. Gohkale, E. Tanuwidjaja, T. Chong, D. Lau, S. Garcia, and M. B. Taylor, “Cortexsuite: A synthetic brain benchmark suite,” in 2014 IEEE International Symposium on Workload Characterization (IISWC). IEEE, 2014, pp. 76–79.*

It can be retrieved from https://cseweb.ucsd.edu/groups/bsg/

To retrieve:

    $ git clone https://bitbucket.org/taylor-bsg/cortexsuite.git

PolyRhythm uses the `disparity` benchmark as a victim workload when training. To build:

    ### Go the directory of disparity
    $ cd cortexsuite/vision/benchmarks/disparity/data/sqcif
    $ make c-run
    
    ### Troubleshooting:
    ### make fails on some versions of Linux (including the VM)
    ### because of the gmake system
    ### When it fails, run:
    $ make c-run -n
    ### This will tell you the failed compilation command
    ### Copy the command, manually append -lm, then run it directly
    ### After the command completes, the build should be done

You will need to update the `GA/utils.py` to reflect the installation path for the `disparity` benchmark.

__stress-ng__

stress-ng is designed to stress various resources on a system, based on runtime selections. This makes it easy to launch synthetic workloads that are heavily constrained to a given resource (e.g., cache, main memory, disk, network, etc.).

Documentation for stress-ng is available from: https://wiki.ubuntu.com/Kernel/Reference/stress-ng

It can be retrieved from: https://github.com/ColinIanKing/stress-ng

To retrieve and build:

    $ git clone https://github.com/ColinIanKing/stress-ng
    $ cd stress-ng
    $ make

You will need to update the `GA/utils.py` to reflect the installation path for `stress-ng`.

__3. Launch the GA to tune parameters.__

*Note*: Make sure you have updated `GA/utils.py` with the correct paths to the victim workloads.

To run the GA:

    $ cd ~/PolyRhythm/GA
    $ python3 main.py

This tunes each primitive independently. The GA takes the additional arguments:

* `--channel`: Tune an individual primitive. Values match the names listed above for each primitive.
* `--ncores`: The number of cores on the target system. Defaults to 4. Launches one victim thread, then `ncores-1` attacker instances.
* `--params`: Outputs parameters to the given filename. Values are then used by the dynamic RL-based online attack.
* `--perfname`: The binary name for the `perf` command on the target system. Defaults to "perf"
* `--weightmode`: The scheme for assigning relative weights to each performance event that contributes to the interference potential of a given attack primitive. 1 (default): assigns weights according to the stability of the event. 2: assigns weights according to the relative impact of the primitive on each event. More details can be found in the paper.

*Note*: On real hardware, we suggest disabling frequency scaling on the target platform, which reduces noise in measured performance event counts. In a virtual environment where other performance counters are unavailable, PolyRhythm will fall back to profiling the `task-clock` event.

## Training RL Model for Dynamic Primitive Selection

The RL model is trained using hardware-in-the-loop to prevent the overhead of training from unduly impacting the behavior of the target system. A training machine establishes a remote SSH connection to the target machine. The target machine runs its victim workload and a low-overhead data collection script that profiles its own execution to infer contention with the victim, then sends the data back to the training machine. 

### Target Machine

__1. Retrieve IP__

Retrieve the IP address of the target machine, and ensure it is on the same network (or otherwise reachable) by the training machine over port 2223. The target machine will listen for connections on this port.

__2. Run Victim Workload__

__3. Run Data Collector__

    $ python3 data_collector.py

### Training Machine

Run the model training script:

    $ cd ~/PolyRhythm/RL_DDPG
    $ python3 train_main.py --ip xxx.xxx.xxx.xxx

Specify the IP address of the target machine as the `--ip` argument.

After the script is launched on the training machine, the `data_collector.py` script on the target machine should output:

> Connection to the training platform.
  
The training script will save a new model to `RL_DDPG/checkpoints/manipulator` every 20 epochs. This may take several hours to complete, depending on the training platform.

## Running PolyRhythm with Trained Model

__1. Retrieve Model__

Copy the model file (`RL_DDPG/checkpoints/manipulator`) from the training machine to the target machine.

__2. Run Victim Workload__

__3. Run Attack Process(es)__

Run the version of PolyRhythm that dynamically selects attack primitives based on the RL model:

    $ ./rl -P params.txt
    
Here, `params.txt` is the parameters file output by the GA (as specified by its `--params` argument).

You can run multiple instances, pin instances to cores with the `taskset` command, or launch as real-time processes using `chrt`. Alternatively, PolyRhythm comes with a wrapper program that enables synchronized launching of multiple real-time attack and victim processes.

__4. Launch the RL Model__

The script that runs the model must be run concurrently with the attack binaries:

    $ python3 RL_DDPG/attack_main.py
        
## Real-Time Launcher

__Overview__

We provide a special launcher program in `~/PolyRhythm/rt-launcher.c`. The binary is compiled with PolyRhythm as part of the CMake build process.

The launcher is used to run multiple instances of multiple programs, which can be assigned to the Linux CFS (__SCHED_OTHER__ scheduling class), given fixed priorities (under the __SCHED_RR__ scheduling class), or assigned periods and deadlines (under __SCHED_DEADLINE__). Deadline-based tasks can also have utilization constrained via CBS. The launcher can also optionally pin each instance to a specified core.

It synchronizes all instances by performing all scheduling-related assignments in the parent process while children wait to proceed by reading from a pipe. Once all setup has completed, the parent writes sufficient bytes to the pipe to allow the children to proceed and exec the specified commands.

__Command-Line__

The command-line argument structure looks like this:

```./launcher <num_programs>```

For each program, additional arguments must be provided:

```<num_instances> <scheduler attrs ...> <num_args> <command>```

The scheduler attributes depend on the value of scheduler:

        c: Use CFS. No additional attributes.
        f: Use SCHED_RR. A priority is specified
        d: Use SCHED_DEADLINE. A period and deadline are specified (in ms)
        
Then for each instance launched, additional arguments are used:

```<core> <utilization?>```

Utilization is only specified for `SCHED_DEADLINE`.

In this case, runtime is set to period * utilization

``<num_args>`` specifies the number of tokens (including the path to the executable) in the `<command>` arguments. This is what will be launched.

If `<core>` is -1, then no core pinning is used. Otherwise, the specified core is pinned.

__Behavior__

The launcher forks all children, which wait on a pipe to proceed. It sets their core affinity and scheduling attributes as needed. Once all children are launched, the launcher sets itself to the highest priority:

* If all children are CFS, it does nothing.
* If some children are SCHED_RR, it uses the maximum priority + 1
* If some children are SCHED_DEADLINE, it instead uses half the shortest period and deadline
 
It then writes a number of bytes to the pipe equal to the number of children. The children can proceed, but the parent process is at the highest priority. The parent prints its own process group, then executes a shell (`sh`). This way, if the system hangs, the process group can be killed without shutting down the system:

```kill -9 -<pgid>```


__Example__

    ./launcher 3 \
        1 f 10 1 slam -1 \
        4 d 50 50 3 ./rl -P ../params.txt 0 0.2 1 0.3 2 0.4 3 0.5 \
        1 d 5 1 2 python3 DDPG.py 0 1.0
        
This will run 3 programs:

The first program has 1 instance using SCHED_RR at priority 10. It is not pinned to a core.

The second program has 4 instances using `SCHED_DEADLINE` with 50ms period/deadline. It is launched with 3 arguments:
    
```./rl -P ../params.txt```

The first instance is on core 0 with a runtime of 10ms
The second instance is on core 1 with a runtime of 15ms
The third instance is on core 2 with a runtime of 20ms
The fourth instance is on core 3 with a runtime of 25ms

The third program has 1 instance using `SCHED_DEADLINE` with a 5ms period and 1ms deadline. It is launched with 2 arguments: `python3 DDPG.py`. Its instance is pinned to core 0.



