import os
import time
import numpy as np
import math

r = 50 #blade length
z = 8 #blade/tower clearance
T = 6 #10 hz

step = 0.001 #1 ms

sigma = 0.9

filename = 'positions.dat'

os.rename("./" + filename, "./" + filename + "." + str(int(time.time())))

with open(filename, 'w') as file:
	thetas = np.linspace(0, 2*math.pi, num = int(T / step))
	
	vfunc = np.vectorize(math.sin)
	xs = r*vfunc(thetas)
	vfunc = np.vectorize(math.cos)
	ys = r*vfunc(thetas)

	vfunc = np.vectorize(math.sqrt)
	dists = vfunc((xs + r)*(xs +r) + (r - ys)*(r - ys))
	print dists
	
	dvars = np.random.geometric(p=sigma, size=len(dists))

	dists = dists + dvars*dists
	
	file.write('1 0 0 0 0\n')
	for i in range(len(dists)):
		file.write('0 ' + str(i*step) + ' 0 ' + str(dists[i]) + ' 0\n')
	
	
	
