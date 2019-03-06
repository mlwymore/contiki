import argparse
parser = argparse.ArgumentParser(description='Plot a RPL tree from a Cooja file.')
parser.add_argument('fileName', metavar='File', type=str, help='Cooja file to plot.')

args = parser.parse_args()

print('Plotting from file %s.' % args.fileName)

motes = [] #[(moteId, X, Y, rank), ...]
tree = [] #[(moteId, fp), ...]

with open(args.fileName) as infile:
	for line in infile:
		if 'POSITION' in line:
			words = line.split()
			motes.append((int(words[2]), float(words[3]), float(words[4]), -1))

		elif 'RPL' in line and ('fp' in line or ' p ' in line):
			words = line.split()
			tree.append((int(words[1]), int(words[4]), float(words[8])))

		elif 'RPL' in line and 'rank' in line:
			words = line.split()
			moteId = int(words[1])
			mote = [m for m in motes if m[0] == moteId][0]
			motes.remove(mote)
                        mote = (mote[0], mote[1], mote[2], int(words[8]))
			motes.append(mote)
			

motes.sort()
tree.sort()
print('Motes: %s' % motes)
print('Tree: %s' % tree)

import plotter
plotter = plotter.Plotter('%s-tree.pdf' % args.fileName)
plotter.plotTopology(motes, tree)


			
