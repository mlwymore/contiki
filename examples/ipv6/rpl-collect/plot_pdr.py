import argparse
parser = argparse.ArgumentParser(description='Plot packet delivery ratio from Cooja files.')
parser.add_argument('fileNames', metavar='Files', nargs='+', type=str, help='Cooja files to plot.')

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
		prots.add(prot)
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

for dai in dais:
	for prot in prots:
		for ccr in ccrs:
			xs = []
			ys = []
			for tx in txs:
				pdrs = []
				for fileName in [f for f in args.fileNames if 'rdc%s-' % prot in f and 'ccr%d-' % ccr in f and 'tx%d-' % tx in f and 'dai%d-' % dai in f]:
					with open(fileName) as infile:
						sent = len([l for l in infile if 'SEND' in l])
					with open(fileName) as infile:
						delivered = len([l for l in infile if 'RECEIVE' in l])
					print(sent)
					print(delivered)
					print(fileName)
					pdrs.append(float(delivered) / float(sent))
				if pdrs:
					xs.append(tx)
					ys.append(np.mean(pdrs))
			data[(prot, ccr)] = (xs, ys)

	import matplotlib.pyplot as plt
	for key, xy in data.items():
		if len(xy[0]) > 1:
			plt.plot(xy[0], xy[1], label='%s-%d' % key)
		elif len(xy[0]) == 1:
			plt.scatter(xy[0], xy[1], label='%s-%d' % key)
	plt.legend()
	plt.savefig('avg_pdr_dai%d.pdf' % dai)
	plt.close()
