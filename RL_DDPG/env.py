#!/usr/bin/env python3

#from GA.launch import *
from paramiko import SSHClient
import connections
import sysv_ipc
import time

import sys

sys.path.append("/home/polyrhythm/PolyRhythm/RL_DDPG/C-extension/")
import shmextension

NUM_RESOURCES = 5

class Environment():
    """Environment initialization function"""
    def __init__(self, target_ip = '127.0.0.1', is_attack = False):
        #anything needs to be initilized
        print("Initialization!")
        self.action_dic = {
            'cache': 1,
            'memory': 0,
            'network': 2,
            'disk': 1,
            'tlb': 0
        }
        self.state = {
            # 'exetime_growth': 0,
            # 'cache': 0,
            # 'row': 0,
            # 'net': 0,
            # 'disk': 0,
            # 'tlb': 0
            # 'filesys_growth': 0
            'd_action_1': 1.0,
            'd_action_2': 1.0,
            'd_action_3': 1.0,
            'd_action_4': 1.0,
            'd_action_5': 1.0
        }
        self.last_response = {
            'cache_count': 0,
            'memory_count': 0,
            'disk_count': 0,
            'network_count': 0,
            'tlb_count': 0
            # 'filesys_count': 0
        }

        self.target_ip = target_ip

        self.is_attack = is_attack
        ### Attack environment
        if self.is_attack:
            # Register a shared memory to communication with attack process
            self.action_memory = sysv_ipc.SharedMemory(666644)

            # Register a shared memory to communication with attack process
            self.state_memory = sysv_ipc.SharedMemory(666688) 
        ### Traning environment
        else:
            self.socket, ret  = connections.init_connection(self.target_ip)

            if ret != 0:
                print("Establishing connection failed")
                exit(-1)
        
        ### Init the shared memory 
        shmextension.init_shm()

        ### Action index
        self.current_action_index = 0

    
    def update_state(self, responses):
        """Update current state from environment"""
        reward = 0

        # print("Update state: ", responses)    
        
        # Parse response 
        # count = 0
            
        '''
        ### This code block is used for timer-based RL

        ## Beyond the state in our buffer
        if count == 5:
            break
    
        try:
            key, value = state.split(":")
        except:
            # String is not completed
            break

        # Value is valid only if != 0 or != 1 
        if float(value) > 1 :
            # Calculate the reward
            reward += (self.state[key] / float(value))

            # Update self state
            self.state[key] = float(value)

        # Increment the number of states we got
        count = count + 1 
        '''

        # print("states : ", responses)     
        states = responses.split(",")

        self.state['d_action_1'] = float(states[1])
        self.state['d_action_2'] = float(states[2])
        self.state['d_action_3'] = float(states[3])
        self.state['d_action_4'] = float(states[4])
        self.state['d_action_5'] = float(states[5])

        reward = float(states[1]) + float(states[2]) + float(states[3]) + float(states[4]) + float(states[5])
        
        return self.state, reward


    def get_state(self):
        return True, self.state

    def perform_action_training(self, message):
        '''Send actions to remote testing machine
        and then get feedback from them.
        '''
        # The code blocks commented below leverage ssh to send command to the testing platform
        # Now this way to communicate with the testing platform is deprecated
        # There is another file 'data_collector.py' which is responsible for communication

        '''
        ### remote launch the attack tasks and get the feedback
        # construct the attack cmd, uses the mutants as new parameters
        # attack_cmd = construct_enemy_cmd_with_dic(message)
        attack_cmd = construct_enemy_cmd_with_array(message)
        
        client = SSHClient()
        client.load_system_host_keys()
        client.connect("10.228.106.153", username="pi")
        stdin, stdout, stderr = client.exec_command(attack_cmd)
        print(pfmon_color.yellow + attack_cmd + " launched" + pfmon_color.reset)

        print(stdout.readlines())
        print("-------------------")
        responses = stdout.readlines()
        '''

        action_list = message

        ### Send the action list
        connections.send_action(self.socket, action_list)

        ### Wait for feedback states
        responses = connections.wait_for_state(self.socket)

        return (True, responses)

    def perform_action_attacking(self, action):
        '''Send actions to local polyrhythm threads
        and then get feedback from them.
        '''

        ### Write the action list into shared memory
        # self.action_memory.write(action_list)

        shmextension.write_shm(int(action), "StarNight")

        # Increment to the next action
        self.current_action_index += 1

        # Sleep for 5 ms, this is the duration for an action list
        time.sleep(5/1000)
        
        # Read value from shared memory
        responses = self.state_memory.read().decode("utf-8")

        return (True, responses)


    def new_reset(self):
        ### Set state here
        ret, state = self.get_state()
        if ret == False:
            return
        
        return state

    def new_step(self, action):

        msg = ""

        # for a in action_list:
            # if a == 0:
            #     msg += 'cache,'
            # elif a == 1:
            #     msg += 'network,'
            # elif a == 2:
            #     msg += 'adv_disk_io,'
            # elif a == 3:
            #     msg += 'memory_ops,'
            # elif a == 4: 
            #     msg += 'tlb,'

        ### Perform action
        if self.is_attack:
            ret, responses = self.perform_action_attacking(int(action))
        else:
            ret, responses = self.perform_action_training(msg)

        if ret == False:
            return

        state, reward = self.update_state(responses)
        
        # reward = self.state['exetime_growth'] + self.state['memory_growth'] + \
        #          self.state['cache_growth'] + self.state['network_growth'] + self.state['filesys_growth']
        
        return self.state, reward, False
