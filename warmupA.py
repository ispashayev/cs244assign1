import numpy as np
import matplotlib.pyplot as plt

data = np.loadtxt('warmupA.dat', skiprows=1)
plt.plot(data[:,1], data[:,2], 'o')
plt.xlabel('Average Throughput')
plt.ylabel('Signal Delay')
plt.show()

scores = np.log(data[:,1] / data[:,2])
print 'Best window size:', int(data[np.argmax(scores),0])
