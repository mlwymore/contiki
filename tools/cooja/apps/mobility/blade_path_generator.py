import os
import time
import numpy as np
import math
import matplotlib.pyplot as plt

r = 50 #blade length
T = 6 #10 hz

step = 0.001 #1 ms

filename = 'positions.dat'

os.rename("./" + filename, "./" + filename + "." + str(int(time.time())))

with open(filename, 'w') as file:
	thetas = np.linspace(0, 2*math.pi, num = int(T / step))
	
	vfunc = np.vectorize(math.sin)
	xs = r*vfunc(thetas)
	vfunc = np.vectorize(math.cos)
	ys = r*vfunc(thetas)

	xs = xs + 50
	ys = 50 - ys
	
	for i in range(len(xs)):
		file.write('1 ' + str(i*step) + ' ' + str(xs[i]) + ' ' + str(ys[i]) + '\n')
	
	
	
