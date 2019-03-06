import argparse
parser = argparse.ArgumentParser(description='Plot average end-to-end delay from Cooja files.')
parser.add_argument('fileNames', metavar='Files', nargs='+', type=str, help='Cooja file to plot.')

args = parser.parse_args()

print('Plotting from files %s.' % args.fileNames)


ccrs = set()
prots = set()
dais = set()
txs = set()
flows = set()
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
	ind = fileName.find('flows')
	if ind >= 0:
		f = int(fileName[ind + 5])
		flows.add(f)
ccrs = sorted(list(ccrs))
print(ccrs)
prots = sorted(list(prots))
print(prots)
dais = sorted(list(dais))
print(dais)
txs = sorted(list(txs))
print(txs)
flows = sorted(list(flows))
print(flows)

data = dict() #{prot: ([x1, x2,...], [y1, y2,...]), ...}
		
import numpy as np

tx = txs[0]

numMotes = 0
for dai in dais:
	for (prot, uses_opp) in prots:
		for ccr in ccrs:
			xs = []
			ys = []
			for flow in flows:
				avgdelays = []
				for fileName in [f for f in args.fileNames if 'rdc%s-' % prot in f and 'ccr%d-' % ccr in f and 'tx%d-' % tx in f and 'opp%d-' % uses_opp in f and 'dai%d-' % dai in f and 'flows%d' % flow in f]:
					delays = []
					with open(fileName) as infile:
						sendLines = [l.rstrip() for l in infile if 'SEND' in l]
					with open(fileName) as infile:
						receiveLines = [l.rstrip() for l in infile if 'RECEIVE' in l]
					
					for line in sendLines:
						words = line.split()
						sendTime = int(words[0])
						sender = int(words[3])
						seqno = int(words[4])
						rls = [rl for rl in receiveLines if int(rl.split()[0]) > sendTime and 
							int(rl.split()[4]) == seqno and
							int(rl.split()[3]) == sender]
						if len(rls) > 1:
              						print("warning: multiple receive lines")
              						print(sendTime, sender)
              						print(rls)
              						rls = rls[0]
						if rls:
							receiveTime = int(rls[0].split()[0])
							delay = (receiveTime - sendTime) / 1000000.0
							delays.append(delay)
					print(len(delays), len(receiveLines))
					if len(delays) != len(receiveLines):
						print("warning: did not find delay for all packets")
					if delays:
						avgdelays.append(np.mean(delays))
				if avgdelays:
					xs.append(flow)
					ys.append(np.mean(avgdelays))
			data[(prot, uses_opp, ccr)] = (xs, ys)
			print(data)

	import matplotlib.pyplot as plt
	for key, xy in data.items():
		if len(xy[0]) > 1:
			plt.plot(xy[0], xy[1], label='%s-%d-%d' % key)
		elif len(xy[0]) == 1:
			plt.scatter(xy[0], xy[1], label='%s-%d-%d' % key)
	plt.legend()
	plt.xlabel('Number of flows')
	plt.ylabel('Avg. delay (s)')
	plt.savefig('avg_delay_dai%d_tx%d.pdf' % (dai, tx))
	plt.close()
