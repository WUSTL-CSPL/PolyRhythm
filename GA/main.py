
import subprocess
import socket
import argparse
import os
from datetime import datetime
from genetic import *
from launch import *
from utils import *
import numpy as np

import signal
import sys


### Handler for Keyboard Interrupt
def signal_handler(sig, frame):
    
    # Kill all polyrhythm processes and then exit
    kill_cmd = 'pkill polyrhythm'
    subprocess.run(kill_cmd, shell=True)
    print("PolyRhythm Killed.")
    sys.exit(0)

def run_channel(channel, params_file = None):
    init_param = np.array(init_params[channel])

    ### Init Genetic Algorithm
    g = Genetic(channel, init_param, ncores = global_params.ncores, is_param1_fixed = fix_p1, is_param2_fixed = fix_p2)

    ### Open log file to store optimal parameters 
    log_file = channel + "run_log.txt"
    with open(log_file, 'w') as log:
        ### Start GA to tune
        parameters, success = g.run(log=log)
        print(f"{parameters[0]},{parameters[1]},{parameters[2]},{parameters[3]}")

        if params_file is not None:
            params_file.write(f"{channel},{parameters[0]},{parameters[1]},"
                              f"{parameters[2]},{parameters[3]},0\n")

        if success:
            print("Found best parameter!")
        else:
            print("Not a total success.")


if __name__ == "__main__":

    ### Set up a signal handler to catch Keyboard Interrupt Signal
    signal.signal(signal.SIGINT, signal_handler)
    # print('Registered Signal')
    
    # Initialize the parser
    parser = argparse.ArgumentParser(
        description="Attack parameter tuning script."
    )

    
    # Optional channel argument
    parser.add_argument('--channel', type=str,
                    help='The target channel to tune {cache,network,row_buffer,disk_io,tlb}')

    # Optional argument: number of cores
    parser.add_argument('--ncores', type=int,
                    help='The number of cores on the target platform', default=4)

    # Optional argument: which perf to use, defaults to "perf"
    parser.add_argument('--perf', type=str,
                    help='The name of the perf binary to use', default="perf")

    # Optional argument: parameter file to output
    parser.add_argument('--params', type=str,
                    help='The name of the params file to output')

    # Parse arguments
    args = parser.parse_args()
    fix_p1 = False ## Parse these two arguments from command line
    fix_p2 = False

    if args.perf is not None:
        global_params.perf_bin = args.perf

    if args.ncores is not None:
        global_params.ncores = int(args.ncores)
        if global_params.ncores < 1 :
            print("Specified --ncores", args.ncores, "is not valid!")
            exit()
    
    params_file = None
    if args.params is not None:
        params_file = open(args.params, 'w')
        if params_file is None:
            print("Could not open", args.params, "for writing")        

    if args.channel is not None:       
        run_channel(args.channel, params_file)
    else: ### We automatically tune all channels
        for channel in init_params:
            run_channel(channel, params_file)
