import numpy as np
import matplotlib.pyplot as plt


times = []
with open('./build/TrackingTimeStats.txt', 'r') as f:
    lines = f.readlines()


count = 0
for line in lines:
    if count == 0: # Skip the first line
        count = count + 1
        continue
    content = line.split(",")
    times.append(float(content[-1]))


plt.plot(times)
plt.show()


