import torch
import os
import numpy as np
import sys
import argparse

sys.path.append("..")

from env import Environment
from ddpg import DDPG


if __name__=="__main__":

    # Initialize the parser
    parser = argparse.ArgumentParser(
        description="Attack parameter tuning script."
    )

    # IP of target for training
    parser.add_argument('--ip', type=str,
                    help="The target's IP address for training", default='127.0.0.1')

    # Parse arguments
    args = parser.parse_args()

    # Environment for getting states and peforming actions
    env = Environment(target_ip=args.ip)

    # Init ddpg agent
    agent = DDPG(env)
    
    # Start training
    agent.train()