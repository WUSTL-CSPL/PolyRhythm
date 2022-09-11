# Torch
import torch
import torch.nn as nn
from torch.autograd import Variable
import torch.optim as optim
import itertools as it


# Lib
import numpy as np
import random
from copy import deepcopy
import os

# Files
from noise import OrnsteinUhlenbeckActionNoise as OUNoise
from replaybuffer import Buffer
from actorcritic import Actor, Critic

# Hyperparameters
ACTOR_LR = 0.03       # learning rate for actor network, 0.0003
CRITIC_LR = 0.003       # learning rate for critic network
MINIBATCH_SIZE = 64     # batch size for updating the actor and critic network
NUM_EPISODES = 9000     # training epoch
NUM_TIMESTEPS = 300     # number of steps to take action before restart the environment
SIGMA = 0.2             # OU-noise parameter
CHECKPOINT_DIR = './checkpoints/manipulator/'   # checkpoint save dir
BUFFER_SIZE = 100000                            # replay buffer size
DISCOUNT = 0.9                                  # Q-value weighted parameter
TAU = 0.001                                     # weight parameter for combining network parameter
WARMUP = 100                                    # number of warmup epoches before updating the network
EPSILON = 1.0                                   # action noise weight
EPSILON_DECAY = 1e-6                            # action decay rate
PLOT_FIG = True
CUDA = False

def obs2state(state_list):
    """Converts observation dictionary to state tensor"""
    return torch.FloatTensor(state_list).view(1, -1)

class DDPG:
    def __init__(self, env):
        self.env = env
        self.stateDim = 5       # dimensions of state space
        self.actionDim = 32     # dimensions of action space
        if CUDA:
            self.actor = Actor(self.stateDim, self.actionDim).cuda()
            self.critic = Critic(self.stateDim, self.actionDim).cuda()
            self.targetActor = deepcopy(Actor(self.stateDim, self.actionDim)).cuda()
            self.targetCritic = deepcopy(Critic(self.stateDim, self.actionDim)).cuda()
        else:
            self.actor = Actor(self.stateDim, self.actionDim)
            self.critic = Critic(self.stateDim, self.actionDim)
            self.targetActor = deepcopy(Actor(self.stateDim, self.actionDim))
            self.targetCritic = deepcopy(Critic(self.stateDim, self.actionDim))
        
        # Configure all parameters
        self.actorOptim = optim.Adam(self.actor.parameters(), lr=ACTOR_LR)
        self.criticOptim = optim.Adam(self.critic.parameters(), lr=CRITIC_LR)
        self.criticLoss = nn.MSELoss()
        self.noise = OUNoise(mu=np.zeros(self.actionDim), sigma=SIGMA)
        self.replayBuffer = Buffer(BUFFER_SIZE)
        self.batchSize = MINIBATCH_SIZE
        self.checkpoint_dir = CHECKPOINT_DIR
        self.discount = DISCOUNT
        self.warmup = WARMUP
        self.epsilon = EPSILON
        self.epsilon_decay = EPSILON_DECAY
        self.rewardgraph = []
        self.start = 0
        self.end = NUM_EPISODES

        ### Temporarily for convenience
        self.current_action_list = []
        self.last_actionToBuffer = None
        self.last_reward = None
        
        #init map from network output vectors to actions
        action_list = list(it.product(range(2),repeat=5))
        self.net_act_dict = {}
        for i in range(len(action_list)):
            self.net_act_dict[str(i)] = action_list[i]
        print("ddpg initialization complete.")
        
    def getQTarget(self, nextStateBatch, rewardBatch, terminalBatch):       
        """Inputs: Batch of next states, rewards and terminal flags of size self.batchSize
            Calculates the target Q-value from reward and bootstraped Q-value of next state
            using the target actor and target critic
           Outputs: Batch of Q-value targets"""
        if CUDA:
            targetBatch = torch.FloatTensor(rewardBatch).cuda() 
        else:
            targetBatch = torch.FloatTensor(rewardBatch)
        nonFinalMask = torch.ByteTensor(tuple(map(lambda s: s != True, terminalBatch)))
        nextStateBatch = torch.cat(nextStateBatch)
        nextActionBatch = self.targetActor(nextStateBatch)
        with torch.no_grad():
            qNext = self.targetCritic(nextStateBatch, nextActionBatch)  
        nonFinalMask = self.discount * nonFinalMask.type(torch.FloatTensor)
        targetBatch += nonFinalMask * qNext.squeeze().data
        
        return Variable(targetBatch, volatile=False)

    def updateTargets(self, target, original):
        """Weighted average update of the target network and original network
            Inputs: target actor(critic) and original actor(critic)"""
        for targetParam, orgParam in zip(target.parameters(), original.parameters()):
            targetParam.data.copy_((1 - TAU)*targetParam.data + \
                                          TAU*orgParam.data)

    def getMaxAction(self, curState):
        """Inputs: Current state of the episode
            Returns the action which maximizes the Q-value of the current state-action pair"""
        if CUDA:
            noise = self.epsilon * Variable(torch.FloatTensor(self.noise()), volatile=True).cuda()
        else:
            noise = self.epsilon * Variable(torch.FloatTensor(self.noise()), volatile=True)
        action = self.actor(curState)
        actionWithNoise = action + noise
        
        # get the chosen action and action with noises for training
        action_list = actionWithNoise.tolist()[0]
        max_action = max(action_list)
        max_index = action_list.index(max_action)
        
        return max_index, actionWithNoise
        
    def train(self):
        if not os.path.exists(self.checkpoint_dir):
            os.makedirs(self.checkpoint_dir)
            
        print('Training started...')

        for episode in range(self.start, self.end):
            # Restart the environment
            state = self.env.new_reset()
            ep_reward = 0
            for step in range(NUM_TIMESTEPS):
                
                state_1 = state['d_action_1']
                state_2 = state['d_action_2']
                state_3 = state['d_action_3']
                state_4 = state['d_action_4']
                state_5 = state['d_action_5']

                # cache_state = state['cache']
                # memory_state = state['row']
                # network_state = state['net']
                # disk_state = state['disk']
                # tlb_state = state['tlb']

                # Converts observation dictionary to state tensor
                if CUDA:
                    curState = Variable(obs2state([state_1,state_2,state_3,state_4,state_5]), volatile=True).cuda()
                else:
                    curState = Variable(obs2state([state_1,state_2,state_3,state_4,state_5]), volatile=True) 
                
                self.actor.eval()

                # Choose the action according to the state
                with torch.no_grad():
                    action, actionToBuffer = self.getMaxAction(curState)
                if CUDA:
                    actionToBuffer = Variable(actionToBuffer).cuda()
                
                # Convert network vector to action tensor
                action_list = self.net_act_dict[str(action)]
                
                self.actor.train()
                
                # Take the action and get the new state, reward
                state, reward, done = self.env.new_step(action_list)


                print('Reward: {}'.format(reward))
                

                state_1 = state['d_action_1']
                state_2 = state['d_action_2']
                state_3 = state['d_action_3']
                state_4 = state['d_action_4']
                state_5 = state['d_action_5']

                # cache_state = state['cache']
                # memory_state = state['row']
                # network_state = state['net']
                # disk_state = state['disk']
                # tlb_state = state['tlb']
                
                # Converts observation dictionary to state tensor
                # if CUDA:
                #     nextState = Variable(obs2state([cache_state,memory_state,network_state,disk_state,tlb_state]), volatile=True).cuda()
                # else:
                #     nextState = Variable(obs2state([cache_state,memory_state,network_state,disk_state,tlb_state]), volatile=True) 
                
                # Converts observation dictionary to state tensor
                if CUDA:
                    nextState = Variable(obs2state([state_1,state_2,state_3,state_4,state_5]), volatile=True).cuda()
                else:
                    nextState = Variable(obs2state([state_1,state_2,state_3,state_4,state_5]), volatile=True) 

                ep_reward += reward
                
                # Update replay bufer
                self.replayBuffer.append((curState, actionToBuffer, nextState, reward, done))
                
                # Training loop
                if len(self.replayBuffer) >= self.warmup:
                    print("--------begin update----------")
                    curStateBatch, actionBatch, nextStateBatch, \
                    rewardBatch, terminalBatch = self.replayBuffer.sample_batch(self.batchSize)
                    curStateBatch = torch.cat(curStateBatch)
                    actionBatch = torch.cat(actionBatch)
                    
                    qPredBatch = self.critic(curStateBatch, actionBatch)
                    qTargetBatch = self.getQTarget(nextStateBatch, rewardBatch, terminalBatch)
                    
                    # Critic update
                    self.criticOptim.zero_grad()
                    criticLoss = self.criticLoss(qPredBatch, qTargetBatch)
                    print('Critic Loss: {}'.format(criticLoss))
                    criticLoss.backward()
                    self.criticOptim.step()
            
                    # Actor update
                    self.actorOptim.zero_grad()
                    actorLoss = -torch.mean(self.critic(curStateBatch, self.actor(curStateBatch)))
                    print('Actor Loss: {}'. format(actorLoss))
                    actorLoss.backward()
                    self.actorOptim.step()
                    
                    # Update Targets                        
                    self.updateTargets(self.targetActor, self.actor)
                    self.updateTargets(self.targetCritic, self.critic)
                    self.epsilon -= self.epsilon_decay
                    
            if episode % 20 == 0:
                self.save_checkpoint(episode)
            self.rewardgraph.append(ep_reward)
        
        if PLOT_FIG:
            plt.plot(self.rewardgraph, color='darkorange')  # total rewards in an iteration or episode
            # plt.plot(avg_rewards, color='b')  # (moving avg) rewards
            plt.xlabel('Episodes')
            plt.savefig('final.png')

    def predict(self):
        ''' This is attack function
            read states and make action.
        '''
        if self.env.current_action_index == 4 or len(self.current_action_list) == 0:
            ''' We need predict new five actions '''

            ### Record the last state
            last_state_1 = self.env.state['d_action_1']
            last_state_2 = self.env.state['d_action_2']
            last_state_3 = self.env.state['d_action_3']
            last_state_4 = self.env.state['d_action_4']
            last_state_5 = self.env.state['d_action_5']

            # Converts observation dictionary to state tensor
            if CUDA:
                lastState = Variable(obs2state([last_state_1,last_state_2,last_state_3,last_state_4,last_state_5]), volatile=True).cuda()
            else:
                lastState = Variable(obs2state([last_state_1,last_state_2,last_state_3,last_state_4,last_state_5]), volatile=True) 

            ### Read states from shared memory           
            responses = self.env.state_memory.read().decode("utf-8")
            state, reward = self.env.update_state(responses)

            ### Save the state into
            state_1 = state['d_action_1']
            state_2 = state['d_action_2']
            state_3 = state['d_action_3']
            state_4 = state['d_action_4']
            state_5 = state['d_action_5']

            # Converts observation dictionary to state tensor
            if CUDA:
                curState = Variable(obs2state([state_1,state_2,state_3,state_4,state_5]), volatile=True).cuda()
            else:
                curState = Variable(obs2state([state_1,state_2,state_3,state_4,state_5]), volatile=True) 
            
            # Update replay bufer
            self.replayBuffer.append((lastState, self.last_actionToBuffer, curState, self.last_reward, True))

            ### Start to predict action
            self.actor.eval()

            # Choose the action according to the state
            with torch.no_grad():
                action, actionToBuffer = self.getMaxAction(curState)
            if CUDA:
                actionToBuffer = Variable(actionToBuffer).cuda()
            
            # Convert network vector to action tensor
            
            self.current_action_list = self.net_act_dict[str(action)]

            # Reset the current action index
            self.env.current_action_index = 0

            self.last_actionToBuffer = actionToBuffer
            self.last_reward = reward

        # Else, used the old one
        # Perform the action
        curr_action = self.current_action_list[self.env.current_action_index] 
        state, reward, done = self.env.new_step(curr_action)




                


    def save_checkpoint(self, episode_num):
        """Utility function for saving the model"""
        checkpointName = self.checkpoint_dir + 'ep{}.pth.tar'.format(episode_num)
        checkpoint = {
            'episode': episode_num,
            'actor': self.actor.state_dict(),
            'critic': self.critic.state_dict(),
            'targetActor': self.targetActor.state_dict(),
            'targetCritic': self.targetCritic.state_dict(),
            'actorOpt': self.actorOptim.state_dict(),
            'criticOpt': self.criticOptim.state_dict(),
            'replayBuffer': self.replayBuffer,
            'rewardgraph': self.rewardgraph,
            'epsilon': self.epsilon
        } 
        torch.save(checkpoint, checkpointName)
    
    def loadCheckpoint(self, checkpointName):
        """Utility function for loading the model"""
        if os.path.isfile(checkpointName):
            print("Loading checkpoint...")
            checkpoint = torch.load(checkpointName)
            self.start = checkpoint['episode'] + 1
            self.actor.load_state_dict(checkpoint['actor'])
            self.critic.load_state_dict(checkpoint['critic'])
            self.targetActor.load_state_dict(checkpoint['targetActor'])
            self.targetCritic.load_state_dict(checkpoint['targetCritic'])
            self.actorOptim.load_state_dict(checkpoint['actorOpt'])
            self.criticOptim.load_state_dict(checkpoint['criticOpt'])
            self.replayBuffer = checkpoint['replayBuffer']
            self.rewardgraph = checkpoint['rewardgraph']
            self.epsilon = checkpoint['epsilon']
            print('Checkpoint loaded')
        else:
            raise OSError('Checkpoint not found')