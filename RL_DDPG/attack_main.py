import torch
import os
import numpy as np
import sys
import threading
import time

sys.path.append("../RL_DDPG/")
sys.path.append("C-extension/")

from env import Environment
from ddpg import DDPG
import shmextension

# change this to the location of the checkpoint file
CHECKPOINT_FILE = 'checkpoints/manipulator/checkpoint.pth.tar'

next_time = None

def periodic_prediction():
    agent.predict()
    threading.Timer(0.005, periodic_prediction, [agent]).start()
    

if __name__=="__main__":
    # Environment for getting states and peforming actions
    env = Environment(is_attack = True)
    
    # Init ddpg agent
    agent = DDPG(env)

    # Load saved checkpoints
    agent.loadCheckpoint(CHECKPOINT_FILE)
    
    # Start predicting
    # agent.predict()
    # periodic_prediction()
    next_time = time.time()
    
    while True:
        next_time += 0.005
        agent.predict()
        time.sleep(time.time() - next_time)
