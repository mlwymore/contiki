import os
import time
import math

filename = 'rssi_trace_realsim.txt'
inputFilename = 'rssi_trace_cooja.txt'
COOJA = True

if COOJA:
	secondsIndex = 2
	tickIndex = 3
	rssIndex = 4
else:
	secondsIndex = 0
	tickIndex = 1
	rssIndex = 2

if os.path.exists("./" + filename):
	os.rename("./" + filename, "./" + filename + "." + str(int(time.time())))

with open(filename, 'w') as outputFile:
	with open(inputFilename, 'r') as inputFile:
		#outputFile.write('0;addnode;1.0;Sky Mote Type #skysource\n')
		#outputFile.write('0;addnode;2.0;Sky Mote Type #skysink\n')
		for inputLine in inputFile:
			words = inputLine.split()
			print words
			timeInMs = int((int(words[secondsIndex]) + (int(words[tickIndex]) / 65535.0))*1000)
			outputFile.write(str(timeInMs) + ';setedge;1.0;2.0;100;' + str(words[rssIndex]) + ';0\n')
			outputFile.write(str(timeInMs) + ';setedge;2.0;1.0;100;' + str(words[rssIndex]) + ';0\n')
		#ouptutFile.write('1 0 ' + str(r) + ' ' + str(2*r) + ' 0\n')
		#for i in range(len(xs)):
		#	outputFile.write('0 ' + str(i*step) + ' ' + str(xs[i]) + ' ' + str(ys[i]) + ' ' + str(z) + '\n')
	
	
	
