import argparse
parser = argparse.ArgumentParser(description='Calculate average forwarding intervals from Cooja files.')
parser.add_argument('fileNames', metavar='Files', nargs='+', type=str, help='Cooja file to plot.')

args = parser.parse_args()

print('Calculating from files %s.' % args.fileNames)


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
		
import numpy as np

numMotes = 0
for dai in dais:
	for (prot, uses_opp) in prots:
		for ccr in ccrs:
			xs = []
			ys = []
			for tx in [tx for tx in txs]:

				for fileName in [f for f in args.fileNames if 'rdc%s-' % prot in f and 'ccr%d-' % ccr in f and 'tx%d-' % tx in f and 'opp%d-' % uses_opp in f and 'dai%d-' % dai in f]:
					with open(fileName) as infile:
						#sendLines = [l.rstrip() for l in infile if 'send unicast' in l]
						sendLines = [l.rstrip() for l in infile if 'send unicast' in l and int(l.split()[0]) > 90000000]
					motesTimes = dict()
					if not sendLines:
						continue
					for l in sendLines:
						mote = int(l.split()[1])
						if mote not in motesTimes:
							motesTimes[mote] = []
						motesTimes[mote].append(int(l.split()[0]))
					avgavg = 0
					for mote, times in motesTimes.items():
						timeDiffs = [times[i + 1] - times[i] for i in range(len(times) - 1)]
						avg = np.mean(timeDiffs)
						avgavg += avg
					avgavg = float(avgavg) / len(motesTimes.keys()) / 1000000.0
					print(fileName, avgavg)
