import argparse
parser = argparse.ArgumentParser(description='Plot average duty cycles from Cooja files.')
parser.add_argument('fileNames', metavar='Files', nargs='+', type=str, help='Cooja file to plot.')

args = parser.parse_args()

print('Plotting from files %s.' % args.fileNames)


ccrs = set()
prots = set()
dais = set()
txs = set()
for fileName in args.fileNames:
	ind = fileName.find('ccr')
	if ind >= 0:
		try:
			ccr = int(fileName[ind + 3:ind + 5])
		except ValueError:
			ccr = int(fileName[ind + 3:ind + 4])
		ccrs.add(ccr)
	rdc = [w for w in fileName.split('-') if w.find('rdc') == 0][0]
	if rdc:
		prot = rdc.replace('rdc', '', 1)
		opp = [w for w in fileName.split('-') if w.find('opp') == 0][0]
		if opp:
			uses_opp = opp.replace('opp', '', 1)
		prots.add((prot, int(uses_opp)))
	ind = fileName.find('dai')
	if ind >= 0:
		try:
			dai = int(fileName[ind +3:ind + 6])
		except ValueError:
			try:
				dai = int(fileName[ind + 3:ind + 5])
			except ValueError:
				dai = int(fileName[ind + 3:ind + 4])
		dais.add(dai)
	ind = fileName.find('tx')
	if ind >= 0:
		try:
			tx = int(fileName[ind + 2:ind + 5])
		except ValueError:
			tx = int(fileName[ind + 2:ind + 4])
		txs.add(tx)
ccrs = sorted(list(ccrs))
print(ccrs)
prots = sorted(list(prots))
print(prots)
dais = sorted(list(dais))
print(dais)
txs = sorted(list(txs))
print(txs)

data = dict() #{prot: ([x1, x2,...], [y1, y2,...]), ...}
		
import numpy as np

numMotes = 0
for dai in dais:
	for (prot, uses_opp) in prots:
		for ccr in ccrs:
			xs = []
			ys = []
			for tx in [tx for tx in txs]:
				dcs = []
				for fileName in [f for f in args.fileNames if 'rdc%s-' % prot in f and 'ccr%d-' % ccr in f and 'tx%d-' % tx in f and 'opp%d-' % uses_opp in f and 'dai%d-' % dai in f]:
					with open(fileName) as infile:
						onLines = [l.rstrip() for l in infile if ' ON ' in l]
					print(fileName)
					if numMotes == 0:
						words = onLines[-1].split()
						words = words[0].split('_')
						numMotes = int(words[1])
					onLines = onLines[-numMotes:]
					#onLines = onLines[1:numMotes]
					vals = []
					for l in onLines[1:]:
						words = l.split()
						vals.append(float(words[4]))
		                	print(vals)
					dcs.append(np.mean(vals))
				if dcs:
					xs.append(tx)
					ys.append(np.mean(dcs))
			data[(prot, uses_opp, ccr)] = (xs, ys)

	import matplotlib.pyplot as plt
	for key, xy in data.items():
		if len(xy[0]) > 1:
			plt.plot(xy[0], xy[1], label='%s-%d-%d' % key)
		elif len(xy[0]) == 1:
			plt.scatter(xy[0], xy[1], label='%s-%d-%d' % key)
	plt.legend()
	plt.savefig('avg_duty_cycle_dai%d.pdf' % dai)
	plt.close()
