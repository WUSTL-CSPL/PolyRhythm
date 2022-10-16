#!/usr/bin/python3

### May need to configure this path if test on other platform
poly_rhythm_path = "/home/pi/PolyRhythm/build/polyrhythm"
stessng_path = "/home/pi/tests/stress-ng/"


# For cahce, may use ./stress-ng --stream 1 --stream-ops 100 -e100 --metrics
victim_tasks = { "cache": "/home/pi/cortexsuite/vision/benchmarks/disparity/data/sqcif/disparity\
                           /home/pi/cortexsuite/vision/benchmarks/disparity/data/sqcif",
                #  "cache": "./stress-ng --stream 1 --stream-ops 1000  --metrics",
                 "network": stessng_path + "./stress-ng --udp 1 --udp-ops 50000 --metrics",
                 "row_buffer": stessng_path + "./stress-ng --stream 1 --stream-ops 50 --metrics",
                 "disk_io": stessng_path + "./stress-ng --io 1 --io-ops 20000  --metrics",
                 "tlb": stessng_path + "./stress-ng --brk 1 --brk-ops 500000 --metrics"
}

# Initial parameters for different channels
init_params = {
    "cache": [1, 500, -1, -1],
    "network": [5000, -1, -1, -1],
    "row_buffer": [1, 10, 0, -1],
    "disk_io": [50, 50000, 1, 1],
    "tlb": [40, -1, -1, -1],
}

# Optimal parameters for Raspberry Pi 3b
# cache: 1 835 0 0
# row_buffer: 1 15 0 0
# network: 65351 0 0 0
# disk io: 50321 10 1 0 0
# tlb: 10 0 0 0
# memory: 2 428 1 0

# Colorful output
class pfmon_color:
    red     ="\x1b[31m"
    green   ="\x1b[32m"
    yellow  ="\x1b[33m"
    blue    ="\x1b[34m"
    magenta ="\x1b[35m"
    cyan    ="\x1b[36m"
    reset   ="\x1b[0m"