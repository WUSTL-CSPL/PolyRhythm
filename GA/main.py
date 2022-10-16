
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


if __name__ == "__main__":

    ### Set up a signal handler to catch Keyboard Interrupt Signal
    signal.signal(signal.SIGINT, signal_handler)
    # print('Registered Singal')

    # Initialize the parser
    parser = argparse.ArgumentParser(
        description="Attack parameter tuning script."
    )

    
    # Optinal channel argument
    parser.add_argument('--channel', type=str,
                    help='The target channel to tune {cache,network,row_buffer,disk_io,tlb}')

    # Optional argument: number of cores
    parser.add_argument('--ncores', type=int,
                    help='The number of cores on the target platform', default=4)

    # Optional argument: perf_binary name
    parser.add_argument('--perfname', type=str,
                    help='The perf command name: e.g., perf, perf_4.9, perf_5.10', default='perf')

    # Optional argument: perf_binary name
    parser.add_argument('--weightmode', type=int,
                    help='The weighting method', default=2)
    

    # Parse arguments
    args = parser.parse_args()
    fix_p1 = False ## Parse these two arguments from command line
    fix_p2 = False

    # print("Perf name: ", args.perfname)

    if args.channel is not None:
        
        init_param = np.array(init_params[args.channel])
        
        ### Init Genetic Algorithm
        g = Genetic(args.channel, init_param, is_param1_fixed = fix_p1, is_param2_fixed = fix_p2, perf_name=args.perfname, weight_mode=args.weightmode, ncores=args.ncores)

        ### Open log file to store optimal parameters 
        log_file = "run_log.txt"
        with open(log_file, 'w') as log:
            ### Start GA to tune
            parameters, success = g.run(log=log)
            print(parameters[0] + "," + parameters[1] + "," + parameters[2] + "," + parameters[3])

            if success:
                print(pfmon_color.green + "Find best parameter!" + pfmon_color.reset)
            else:
                print(pfmon_color.red + "Not a total success." + pfmon_color.reset)


    else: ### We automatically tune all channels

        for channel in init_params: # This is dictionary with all channels

            init_param = np.array(init_params[channel])
            ### Init Genetic Algorithm
            g = Genetic(channel, init_param, is_param1_fixed = fix_p1, is_param2_fixed = fix_p2, perf_name=args.perfname, weight_mode=args.weightmode, ncores=args.ncores)

            ### Open log file to store optimal parameters 
            log_file = channel + "run_log.txt"
            with open(log_file, 'w') as log:
                ### Start GA to tune
                parameters, success = g.run(log=log)
                print(parameters[0] + "," + parameters[1] + "," + parameters[2] + "," + parameters[3])

                if success:
                    print("Find best parameter!")
                else:
                    print("Not a total success.")

    