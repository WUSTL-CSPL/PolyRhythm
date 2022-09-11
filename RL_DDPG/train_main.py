import torch
import os
import numpy as np
import sys

sys.path.append("..")

from env import Environment
from ddpg import DDPG


if __name__=="__main__":
    # Environment for getting states and peforming actions
    env = Environment()

    # Init ddpg agent
    agent = DDPG(env)
    
    # Start training
    agent.train()