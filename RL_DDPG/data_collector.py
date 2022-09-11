import sys
sys.path.append('../GA/')

from launch import *
# from paramiko import SSHClient
import sysv_ipc
from time import sleep
import subprocess

# For the timer
import threading


sys.path.append("/home/polyrhythm/PolyRhythm/RL_DDPG/C-extension/")

import shmextension 



# Process name to ORB-SLAM and rosbag replay
victim_process_name = " Stereo_Inertial"
rosbag_play_process_name = " play"

# Assume the target machine has 4 cores, we launch three attack processes
construct_enemy_cmd_for_RL()

# Construct cmds that pause victim and attack process
pause_orbslam_cmd = "pkill -TSTP " + victim_process_name
pause_replay_cmd = "pkill -TSTP " + rosbag_play_process_name
pause_polyrhythm_cmd = "pkill -TSTP rl"

# Construct cmds that resume victim and attack process
resume_orbslam_cmd = "pkill -CONT " + victim_process_name
resume_replay_cmd = "pkill -CONT " + rosbag_play_process_name
resume_polyrhythm_cmd = "pkill -CONT rl"

#############################
### Utils for connection
### Rx
#############################

def init_connection():
    '''Connect to the training platform
    '''
    sok = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    port = 2223
    sok.bind(("10.235.235.2", port))
    sok.listen(5)
    print("Waiting for any incoming connection.")
    conn, _ = sok.accept()
    print("Connection to the training platform.")
    return conn

def wait_for_action(conn):
    '''Wait for action list from the training platform
    '''
    while True:
        msg = conn.recv(1024)
        content = msg.decode("utf-8")
        return content
        # if content.startswith("Action"):
        #     # conn.send(bytes("I start recording audio", encoding = 'utf-8'))
        #     print("Action received: ", content)
        #     return content



### Main Function Entry

if __name__ == "__main__":

    poly_rhythm_path = '/home/polyrhythm/PolyRhythm/build/rl'
    # subprocess.Popen([poly_rhythm_path,"cache","1", "1", "16384", "0", "0"])
    subprocess.Popen([poly_rhythm_path, '-w', '-P', '/home/polyrhythm/PolyRhythm/params.txt'])
    subprocess.Popen([poly_rhythm_path, '-P', '/home/polyrhythm/PolyRhythm/params.txt'])
    subprocess.Popen([poly_rhythm_path, '-P', '/home/polyrhythm/PolyRhythm/params.txt'])
    subprocess.Popen([poly_rhythm_path, '-P', '/home/polyrhythm/PolyRhythm/params.txt'])

    while True:
        try:
            # Register a shared memory to communication with attack process
            state_memory = sysv_ipc.SharedMemory(666688) 

        except:
            continue
        else:
            # Register a shared memory to communication with attack process

            print("Polyrhythm is already launched")
            # Pause victim and attack process
            output = subprocess.run(pause_orbslam_cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, bufsize=1)
            output = subprocess.run(pause_polyrhythm_cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, bufsize=1)
            break

    ### Init action shared memory
    shmextension.init_shm()

    # Initialize an arbitrary action list 
    shmextension.write_shm(0, "action")

    # Connection to training platform
    conn = init_connection()

    ### Some variable for action lists
    action_list = [0, 0, 0, 0, 0]
    current_action_index = 0

    # Execution Loop
    while True:

        # Pause victim and attack processpause_replay_cmd
        output = subprocess.run(pause_orbslam_cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, bufsize=1)
        output = subprocess.run(pause_replay_cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, bufsize=1)
        output = subprocess.run(pause_polyrhythm_cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, bufsize=1)
        
        # Write value to action memory
        action = action_list[current_action_index]
        shmextension.write_shm(int(action), "Action")

        # If need read new action lists
        if current_action_index == 4:
            # Read value from shared memory
            state_value = state_memory.read()

            # Send states to RL model under training
            ###############################################
            # Exchange data with RL Model under training
            ###############################################

            # Send state to model under training
            conn.send(state_value)

            # Wait for action from model under training
            action_list_response = wait_for_action(conn)

            i = 0
            for a in action_list_response.split(','):
                action_list[i] = int(a)
                i += 1
                if i > 4:
                    break 
            current_action_index = 0
        else:
            current_action_index += 1

        # Resume victim and attack process
        output = subprocess.run(resume_orbslam_cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, bufsize=1)
        output = subprocess.run(resume_replay_cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, bufsize=1)
        output = subprocess.run(resume_polyrhythm_cmd, shell=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, bufsize=1)

        # Sleep for 5 ms, this is the duration for an action list
        time.sleep(5/1000)