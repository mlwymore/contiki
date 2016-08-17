import os
import time
import numpy as np
import math

r = 50 #blade length
z = 8 #blade/tower clearance
T = 6 #10 hz

step = 0.001 #1 ms

#sigma = 0.5

filename = 'positions.dat'

#os.rename("./" + filename, "./" + filename + "." + str(int(time.time())))

with open(filename, 'w') as file:
	thetas = np.linspace(0, 2*math.pi, num = int(T / step))
	
	vfunc = np.vectorize(math.sin)
	xs = r*vfunc(thetas)
	vfunc = np.vectorize(math.cos)
	ys = r*vfunc(thetas)
	
	#xvars = np.random.normal(scale=sigma, size=len(xs))
	#yvars = np.random.normal(scale=sigma, size=len(ys))

	xs = xs + r# + xvars*(xs)
	ys = (r - ys)# + yvars*(r - ys)
	
	file.write('1 0 ' + str(r) + ' ' + str(2*r) + ' 0\n')
	for i in range(len(xs)):
		file.write('0 ' + str(i*step) + ' ' + str(xs[i]) + ' ' + str(ys[i]) + ' ' + str(z) + '\n')
	
	
	
