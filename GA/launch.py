#!/usr/bin/python3
# Both Genetic Algorithm and Reinforcement Learning use the functions in this file.

import subprocess
import socket
import os
from datetime import datetime
from genetic import *
import time
from utils import *

### Global variable
perf_name = 'perf'
ncore = 4

def set_perf_binary_name(name):
    ## For pi, it would be perf_4.9
    global perf_name 
    perf_name = name
    print(pfmon_color.yellow + "Set perf command name: ", perf_name + pfmon_color.reset)


def set_number_of_cores(num_core):
    ## For pi, it would be perf_4.9
    global ncore 
    ncore = num_core
    print(pfmon_color.yellow + "Set number of cores on target machine: ", str(ncore) + pfmon_color.reset)


def construct_enemy_cmd(enemy_dict, channel, para_1, para_2, para_3, para_4):
    '''Contruct a cmd that launches PolyRhythm'''

    # Append parameters
    adv_cmd = poly_rhythm_path + " "  + channel + " " + "1" + " " +  str(para_1) + " " + str(para_2) + \
                                     " " + str(para_3) + " " + str(para_4) + " " + str(0) # This 0 is online profiling flag
    

    print(pfmon_color.blue + adv_cmd + pfmon_color.reset)

    return adv_cmd

    # for e in enemy_dict:

def construct_enemy_cmd_for_RL():
    '''Contruct a cmd that launches RL'''
    
    # For RL attack process, it is not necessary to specify the channel name
    # Attack process will ajust attack channel according to action from RL model 
    # We put 'cache' here for uniformed interface.   
    adv_cmd = poly_rhythm_path + ' ' + 'cache' + " " + "1" + " " +  '1' + " " + '875' + \
                                                                 ' ' + '0' + " " + '0' 
    print(pfmon_color.blue + adv_cmd + pfmon_color.reset)
    return adv_cmd


def construct_enemy_cmd_with_dic(enemy_dict):
    '''Contruct a cmd that launches PolyRhyme from a dictionary
    :enemy_dict: {'attack channel': num of threads}
    '''

    adv_cmd = poly_rhythm_path
    # print(pfmon_color.blue + adv_cmd + pfmon_color.reset)

    for e in enemy_dict:
        adv_cmd += ' ' + e + ' ' + str(enemy_dict[e]) + ' ' + str(1) + ' ' + str(850) + ' ' \
                         ' ' + str(0) + ' ' + str(0) # these two paras are dummy

    return adv_cmd

def construct_enemy_cmd_with_array(action_list):
    '''Contruct a cmd that launches PolyRhyme from an array'''

    adv_cmd = poly_rhythm_path
    # print(pfmon_color.blue + adv_cmd + pfmon_color.reset)

    for e in action_list:
        pp = perfect_parameters[e]
        adv_cmd += ' ' + e + ' ' + str(1) + ' ' + str(pp[0]) + ' ' + str(pp[1]) + ' ' \
                         ' ' + str(pp[2]) + ' ' + str(pp[3]) # these two paras are dummy

    return adv_cmd


def construct_perf_cmd(put_cmd):
    '''Contruct a perf cmd
    This cmd will be used as prefix of victim tasks
    :put_cmd:the command line to launch the "program under test"
    '''
    # This block constructs Linux command 'perf' to monitor PMU counters
    
    # Due to the error of multiplexing, it is better to sample one or two events.
    
    # events = ['instructions', 'cycles'] # Number of retired instructions, number of cycles (in real frequency)
    # events = ['instructions'] # Number of retired instructions,
    events = ['cycles'] # Number of cycles (in real frequency)
    # events.extend(['dTLB-load-misses'])
    # events.extend(['bus-cycles'])
    # events.extend(['L1-dcache-loads', 'L1-dcache-stores']) # load/store operations to L1D, a proxy indicator of memory access instructions
    # events.extend(['cache-references', 'cache-misses']) # LLC (demand by instructions, exclude prefetcher read/write)
    events.extend(['cache-misses']) # LLC (demand by instructions, exclude prefetcher read/write)
    events_str = ','.join(events)
    
    # Construct perf command
    perf_cmd = '%s stat -a -x, -e %s -- %s' % (global_params.perf_bin, events_str, put_cmd)

    # Log perf command
    print(pfmon_color.blue + perf_cmd + pfmon_color.reset)
    return perf_cmd

def construct_perf_cmd_for_VM(put_cmd):
    '''Contruct a perf cmd
    This is only for VM
    perf system in VM does not support most of hardware events
    '''
    events = ['task-clock'] # Number of retired instructions, number of cycles (in real frequency)
    events_str = ','.join(events)
    # Construct perf command

    perf_cmd = '%s stat -a -x, -e %s -- %s' % (global_params.perf_bin, events_str, put_cmd)

    # Log perf command
    print(pfmon_color.blue + perf_cmd + pfmon_color.reset)
    return perf_cmd


def parse_perf_output(perf_output):
    '''Contruct a perf cmd
    This cmd will be used as prefix of victim tasks
    :perf_output:The output log produced by perf
    :return:A dictionary that stores information of each events
    '''
    ret = {}
    log_in_lines = perf_output.stderr.decode('utf-8').strip().split('\n')

    # Parse perf output
    for line in log_in_lines:
        # print(line)
        content = line.strip().split(',')
        try:    # Some events are not supported 
            num = float(content[0])
        except:
            continue
        event = content[2]
        # print("Event : ", event, " num : ", num)
        ret[event] = num
        
    return ret

def launch_attack_one_run(channel, param_1, param_2, param_3, param_4, n_att_threads, is_VM):
    '''Launch one run
    The attack processes will be launched first
    Then victim tasks
    The profilings of victim tasks are returned
    '''

    # launch the adversarial attack first
 
    # construct the attack cmd, uses the mutants as new parameters
    attack_cmd = construct_enemy_cmd({}, channel, param_1, param_2, param_3, param_4)
    # Launche attack threads
    for i in range(n_att_threads):
        # TODO: pin these attacks on different cores 
        # subprocess.run('taskset -c ' + str(i) + ' + attack_cmd + ' &', shell=True) 
        # '&'  set the attack process in background
        subprocess.run(attack_cmd + ' &', shell=True) 
 
    print(pfmon_color.yellow + "Three attack processes launched." + pfmon_color.reset)

    ### Sleep for a while to wait the attack tasks
    time.sleep(0.5)

    # Construct put with perf command
    put_cmd = victim_tasks[channel]
    # print("PUT :", put_cmd)

    # VM has no perf hardware event such that we implement a separate function for it.
    if not is_VM:
        perf_cmd = construct_perf_cmd(put_cmd)
    else:
        perf_cmd = construct_perf_cmd_for_VM(put_cmd) ## Only for VM
    # print(perf_cmd)  

    # Set the command line execution environment as the same of OS
    os_env = os.environ.copy()
    os_env["PATH"] = "/usr/sbin:/sbin:" + os_env["PATH"]


    # Execute the perf command and get its profiling results
    output = subprocess.run(perf_cmd, shell=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, bufsize=1, env=os_env)

    # print("output: ", output) 
    events = parse_perf_output(output)


    # Try to access to results to make sure perf launched successfully
    if not is_VM:
        test_event_name = "cycles"
    else:
        test_event_name = "task-clock"

    try:
        event_count = events[test_event_name]
    except:
        print(pfmon_color.red + "Launch command with perf failed !" + pfmon_color.reset)
        err_msg = output.stderr.decode('utf-8')
        print(pfmon_color.red + "Error message: ", err_msg + pfmon_color.reset)

        # If this is on RPi3, try to use perf_4.9
        perf_name = 'perf_4.9'

    # print("Events : ", events)
    return events

def launch_victim_one_run(channel, is_VM):
    # Construct put with perf command
    put_cmd = victim_tasks[channel]

    # VM has no perf hardware event such that we implement a separate function for it.
    if not is_VM:
        perf_cmd = construct_perf_cmd(put_cmd)
    else:
        perf_cmd = construct_perf_cmd_for_VM(put_cmd) ## Only for VM
    # print(perf_cmd)

    # Execute the perf command and get its profiling results
    output = subprocess.run(perf_cmd, shell=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, bufsize=1)
    
    events = parse_perf_output(output)

    # Try to access to results to make sure perf launched successfully
    if not is_VM:
        test_event_name = "cycles"
    else:
        test_event_name = "task-clock"

    try:
        event_count = events[test_event_name]
    except:
        print("Events", )
        print(pfmon_color.red + "Launch command with perf failed !" + pfmon_color.reset)
        err_msg = output.stderr.decode('utf-8')
        print(pfmon_color.red + "Error message: ", err_msg + pfmon_color.reset)
    
    return events


def kill_attack_proc():
    kill_cmd = 'pkill polyrhythm'
    subprocess.run(kill_cmd, shell=True)
