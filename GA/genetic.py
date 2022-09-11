import numpy as np
from launch import *
import math
import time
 
import statistics
#------output best crafted parameter by genetic algorithm----------

def get_new_pop(elite_pop, elite_pop_scores, pop_size):    
    """Crossover elited parents to obtain next generations.
    :elite_pop: elited population vector
    :elite_pop_scores: scores of elited population
    :pop_size: total population size
    :return: next-gen population after crossover
    """
    # calculate the probability of choosing each population as parents; 
    # the higher the scores, the higher probability it will be chosen
    scores_logits = np.exp(elite_pop_scores - elite_pop_scores.max())      
    elite_pop_probs = scores_logits / scores_logits.sum()

    # Choose two parents
    cand1 = elite_pop[np.random.choice(len(elite_pop), p=elite_pop_probs, size=pop_size)]
    cand2 = elite_pop[np.random.choice(len(elite_pop), p=elite_pop_probs, size=pop_size)]

    # Randomly merge the parameters of two parents
    mask = np.random.rand(pop_size, elite_pop.shape[1]) < 0.5 
    next_pop = mask * cand1 + (1 - mask) * cand2
    next_pop = next_pop.astype(int)

    return next_pop


def mutate_pop(pop, mutation_p, noise_stdev, elite_pop):
    """Mutate current population with given settings.
    :pop: current population vector
    :mutation_p: mutation probability
    :noise_stdev: standard deviation of added noises
    :elite_pop: elited population vector 
    :return: mutated population vector
    """

    # apply noise of normal distribution to the population
    noise = np.transpose(np.transpose(np.random.randn(*pop.shape)) * np.array(noise_stdev))
    print("Noise: ", noise)
    print("Mutation P: ", mutation_p)
    tmp = np.random.rand(pop.shape[0], elite_pop.shape[1]) #< mutation_p 
    print("Before, Mask in mutation: ", tmp)
    
    mask = tmp < mutation_p
    print("After, Mask in mutation: ", mask)
    new_pop = pop + noise * mask
    # filter the negative values
    neg_index = new_pop < 0
    new_pop[neg_index] = 0
    new_pop += neg_index * pop
    new_pop = new_pop.astype(int)

    print("New population : ", new_pop)

    return new_pop

class Genetic():
    def __init__(self, channel, init_param, ncores = 4, is_param1_fixed = False, is_param2_fixed = False):
        self.pop_size = 10                                 # total population size in each round 
        self.elite_size = 10                               # elite population size in each round
        self.mutation_p = 0.01                             # mutation probability
        # self.noise_stdev = [[30],[40]]                    
       
        self.noise_stdev = []                              # stadard deviation of added noises, different parameters have different deviations               
        
        ### Initialize the step size in mutation
        for i in range(len(init_param)):
            if init_param[i] != -1:
                # noise is related to the scope of initial values of parameters
                self.noise_stdev.append([
                        math.ceil(init_param[i]/10)])
            else:
                # We do not have to mutate this parameter
                self.noise_stdev.append([0])
                init_param[i] = 0
        
        self.mu = 0.9                                       # momentum mutation parameter
        self.alpha = 0.01                                   # momentum mutation weight parameter
        self.max_iters = 3000                               # maximum iterations
        self.stop_thres = 1e-2                              # stopping criteria of score changes
        self.pop = np.expand_dims(init_param, axis=0)        
        self.pop = np.tile(self.pop, (self.pop_size, 1))    # initial population
        self.is_in_VM = False                               # if in the VM environment

        self.is_param1_fixed = is_param1_fixed              # if we mutate parameter 1 or not
        self.is_param2_fixed = is_param2_fixed
        self.fixed_param1 = init_param[0]                   # record the initial values
        self.fixed_param2 = init_param[1]

        self.ncores = ncores                                # number of cores on the target platform
        self.channel = channel                              # Name of resource channel to tune
 
        self.baseline_score = 0                             # Baseline score for normalization

    def init_baseline_score(self):
        """Get baseline score without attackers.
           Baseline score is obtained by run victim task in isolation
        """
        primary_scores = []
        secondary_scores = []
        # Try ten times to remove the outliers produced by PMUs
        for i in range(10):

            profilings = launch_victim_one_run(self.channel, self.is_in_VM)

            primary_score = profilings['cycles'] / 10e8
            primary_scores.append(primary_score)
            
            time.sleep(0.5)
        
        self.baseline_score = statistics.mean(primary_scores)

        print("Baseline Score: ", self.baseline_score)

    # get fitness score according to the defined objective function
    def get_fitness_score(self):
        """Get fitness score of self.pop."""
        
        # Run pop times if not parallel
        score_list = []
        for i in range(self.pop_size):

            instructions_scores = []
            cache_misses_scores = []

            # Run one round of attack with corresponding parameters, store the system parameters in variable
            # Try ten times to remove the outliers produced by PMUs
            for j in range(10):                
                # Launch attack and victim tasks, return profiling results from PMU
                param1 = self.pop[i][0]
                param2 = self.pop[i][1]
                param3 = self.pop[i][2]
                param4 = self.pop[i][3]

                if self.is_param1_fixed: 
                    param1 = self.fixed_param1
                elif self.is_param2_fixed:
                    param2 = self.fixed_param2
                    
                profilings = launch_attack_one_run(self.channel, param1, param2, param3, param4,
                                                        (self.ncores - 1), False)

                # Kill PolyRhythm process to restart the system status
                kill_attack_proc()

                print("Profiling : ", profilings)
                
                '''
                Note: since Virtualbox machines do not support most of hardware events
                We switch to use 'task-clock' as the feedback events in GA instead of the events we mentioned in our paper, such as cache-misses
                '''
                # Get scores
                #instructions_score = profilings['instructions'] / 10e8
                instructions_score = profilings['cycles'] / 10e8
                # bus_cycles_score = profilings['bus-cycles'] / 10e7
                cache_misses_score = profilings['cache-misses'] / 10e5

                instructions_scores.append(instructions_score)
                cache_misses_scores.append(cache_misses_score)

            inst_var = statistics.variance(instructions_scores)
            cache_var = statistics.variance(cache_misses_scores)
            # print("inst variance : ", inst_var)
            # print("cache variance : ", cache_var)


            ''' We analyze the sensitivity of hardware events.
            We find the largest and smallest,
            if the events is more sensitive than [cycles] 
            we increase its weight in score.
            '''
            max_index = 0
            min_index = 0

            max_inst_score = max(instructions_scores)
            max_indexs = [i for i, s in enumerate(instructions_scores) if max_inst_score == s]
            if max_indexs: # max list is no empty
                max_index = max_indexs[0]

            min_inst_score = min(instructions_scores)
            min_indexs = [i for i, s in enumerate(instructions_scores) if min_inst_score == s]
            if min_indexs: # min list is no empty
                min_index = min_indexs[0]

            instruction_ratio = instructions_scores[max_index] / instructions_scores[min_index]
            cache_ratio = cache_misses_scores[max_index] / cache_misses_scores[min_index]

            weight = cache_ratio / instruction_ratio
            print("Weight : ", weight)
            ### Remove the outliers until the variance is below a threshold
            while inst_var > 10e-5 and len(instructions_scores) > 2:
                instructions_scores.remove(max(instructions_scores))
                instructions_scores.remove(min(instructions_scores))
                inst_var = statistics.variance(instructions_scores)
            while cache_var > 10e-5 and len(cache_misses_scores) > 2:
                cache_misses_scores.remove(max(cache_misses_scores))
                cache_misses_scores.remove(min(cache_misses_scores))
                cache_var = statistics.variance(cache_misses_scores)

            #### Consider drop this result that if the variaton is too large
            inst_var = statistics.variance(instructions_scores)
            cache_var = statistics.variance(cache_misses_scores)

            ### Calculate the average score
            ave_instructions_score = statistics.mean(instructions_scores)
            ave_cache_misses_score = statistics.mean(cache_misses_scores)

            ### Get the final score
            raw_score = instructions_score + weight * cache_misses_score
            ### Normalize the score
            score = raw_score / self.baseline_score

            score_list.append(cache_misses_score)

        return np.array(score_list)

    def get_fitness_score_for_VM(self):
        """Get fitness score of self.pop."""
        '''
        Note: since Virtualbox machines do not support most of hardware events
        We switch to use 'task-clock' as the feedback events in GA instead of the events we mentioned in our paper, such as cache-misses
        '''

        # Run pop times if not parallel
        score_list = []
        for i in range(self.pop_size):

            task_clock_scores = []
            cache_misses_scores = []

            # Run one round of attack with corresponding parameters, store the system parameters in variable
            # Try ten times to remove the outliers produced by PMUs
            for i in range(10):                
                # Launch attack and victim tasks, return profiling results from PMU
                param1 = self.pop[i][0]
                param2 = self.pop[i][1]
                param3 = self.pop[i][2]
                param4 = self.pop[i][3]
                
                if self.is_param1_fixed: 
                    param1 = self.fixed_param1
                elif self.is_param2_fixed:
                    param2 = self.fixed_param2
                    
                profilings = launch_attack_one_run(self.channel, param1, param2, param3, param4,
                                                        (self.ncores - 1), True)

                # Kill PolyRhythm process to restart the system status
                kill_attack_proc()

                print("Profiling : ", profilings)
    
                # Get scores
                task_clock_score = profilings['task-clock']
                task_clock_scores.append(task_clock_score)
  

            clock_var = statistics.variance(task_clock_scores)
            # print("inst variance : ", inst_var)
            # print("cache variance : ", cache_var)

            ### Remove the outliers until the variance is below a threshold
            while clock_var > 10e-5 and len(task_clock_scores) > 2:
                task_clock_scores.remove(max(task_clock_scores))
                task_clock_scores.remove(min(task_clock_scores))
                clock_var = statistics.variance(task_clock_scores)

            ### Get the final score
            score = statistics.variance(task_clock_scores)
            score_list.append(score)

        return np.array(score_list)
                    
    def run(self, log=None):
        """Main logic of genetic algorithm."""
        
        max_fitness_score = float('-inf') 
        score_diff = float('inf')
        itr = 1
        prev_score = 0
        
        self.init_baseline_score()

        # Repeat the loop until reaching the maximum iterations or the scores no longer changes
        while itr <= self.max_iters and score_diff > self.stop_thres:

            if not self.is_in_VM:
                try:
                    pop_scores = self.get_fitness_score()
                except:
                    # We are in a platform that some hardware events are not supported
                    self.is_in_VM = True
                    continue
            else:
                pop_scores = self.get_fitness_score_for_VM() # For VM
            
            # Sort and elite the population according to the fitness scores
            elite_ind = np.argsort(pop_scores)[-self.elite_size:]
            elite_pop, elite_pop_scores = self.pop[elite_ind], pop_scores[elite_ind]

            # Change mutation probability by momentum mutation
            if prev_score is not None and prev_score != elite_pop_scores[-1]: 
                self.mutation_p = self.mu * self.mutation_p + min(self.alpha / np.abs(prev_score - elite_pop_scores[-1]), 0.5) 

            # Obtain the next-round population
            next_pop = get_new_pop(elite_pop, elite_pop_scores, self.pop_size)
            # Mutate the population
            self.pop = mutate_pop(next_pop, self.mutation_p, self.noise_stdev, elite_pop)

            # Calculate the different between the current iteration and the last iteration
            score_diff = np.abs(elite_pop_scores[-1]-prev_score)

            # Print out the score in each iteration to observe the score change
            print(pfmon_color.red + "Current score : ", elite_pop_scores[-1])
            print(pfmon_color.red + "Last score : ", prev_score)
            print(pfmon_color.red + "Score different : ", score_diff)

            prev_score = elite_pop_scores[-1]

            if log is not None:
                log.write("------------------------------------------------" + '\n')
                log.write("Current Score: " + str(elite_pop_scores[-1]) + '\n')
                log.write("Diff Score: "+ str(score_diff) + '\n')
                log.write("Current parameter: " + str(self.pop[-1,0]) + ", " + str(self.pop[-1,1]))
                log.flush()

            # Store the best parameter and scores
            if prev_score > max_fitness_score:
                
                max_fitness_score = prev_score

                # Save the best score into logfile
                if log is not None:
                    log.write('Current itr: ' +  str(itr) + ";")
                    best_param_one, best_param_two, best_param_three, best_param_four  = self.pop[-1,0], self.pop[-1,1], self.pop[-1,2], self.pop[-1,3] 
                    log.write('Best param1: ' + str(best_param_one) + ",")
                    log.write("Best param2: " + str(best_param_two) + ",")
                    log.write("Best param3: " + str(best_param_two) + ",")
                    log.write("Best param4: " + str(best_param_two) + ";")
                    log.write('Best score: ' + str(prev_score) + '\n')
                    log.flush()
            if log is not None:
                log.write("------------------------------------------------"+'\n')
            itr += 1
        return self.pop[-1], itr < self.max_iters



