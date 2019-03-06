import argparse
parser = argparse.ArgumentParser(description='Plot average duty cycles from Cooja files.')
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
		if uses_opp == '':
			uses_opp = '0'
		prots.add((prot, int(uses_opp)))
	ind = fileName.find('dai')
	if ind >= 0:
		try:
			dai = int(fileName[ind + 3:ind + 7])
		except ValueError:
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

senderdata = dict() #{prot: ([x1, x2,...], [y1, y2,...]), ...}
recvrdata = dict()
		
import numpy as np

tx = txs[0]

for dai in dais:
	for (prot, uses_opp) in prots:
		for ccr in ccrs:
			for flow in flows:
				numMotes = 0
				senderdcs = []
				recvrdcs = []
				for fileName in [f for f in args.fileNames if 'rdc%s-' % prot in f and 'ccr%d-' % ccr in f and 'tx%d-' % tx in f and ('opp%d-' % uses_opp in f or 'opp' in f) and 'dai%d-' % dai in f and 'flows%d' % flow in f]:
					with open(fileName) as infile:
						onLines = [l.rstrip() for l in infile if ' ON ' in l]
					print(fileName)
					if numMotes == 0:
						words = onLines[-1].split()
						words = words[0].split('_')
						numMotes = int(words[1])
					onLines = onLines[-numMotes:]
					print(len(onLines))
					#onLines = onLines[1:numMotes]
					sendervals = []
					recvrvals = []
					for l in onLines:
						words = l.split()
						moteid = int(words[0].split('_')[1])
						if moteid != 1:
							sendervals.append(float(words[4]))
						else:
							recvrvals.append(float(words[4]))
					senderdcs.append(np.mean(sendervals))
					recvrdcs.append(np.mean(recvrvals))
				if senderdcs:
					try:
						senderdata[(prot, uses_opp, ccr)][0].append(flow)
						senderdata[(prot, uses_opp, ccr)][1].append(np.mean(senderdcs))
					except KeyError:
						senderdata[(prot, uses_opp, ccr)] = [[flow], [np.mean(senderdcs)]]
					try:
						recvrdata[(prot, uses_opp, ccr)][0].append(flow)
						recvrdata[(prot, uses_opp, ccr)][1].append(np.mean(recvrdcs))
					except KeyError:
						recvrdata[(prot, uses_opp, ccr)] = [[flow], [np.mean(recvrdcs)]]

	import matplotlib.pyplot as plt
	for key, xy in senderdata.items():
		if len(xy[0]) > 1:
			plt.plot(xy[0], xy[1], label='%s-%d-%d' % key)
		elif len(xy[0]) == 1:
			plt.scatter(xy[0], xy[1], label='%s-%d-%d' % key)
	plt.legend()
	plt.xlabel('Number of senders')
	plt.ylabel('Avg. sender duty cycle (%%)')
	plt.savefig('avg_duty_cycle_sender_dai%d_tx%d.pdf' % (dai, tx))
	plt.close()
	
	for key, xy in recvrdata.items():
		if len(xy[0]) > 1:
			plt.plot(xy[0], xy[1], label='%s-%d-%d' % key)
		elif len(xy[0]) == 1:
			plt.scatter(xy[0], xy[1], label='%s-%d-%d' % key)
	plt.legend()
	plt.xlabel('Number of senders')
	plt.ylabel('Avg. receiver duty cycle (%%)')
	plt.savefig('avg_duty_cycle_receiver_dai%d_tx%d.pdf' % (dai, tx))
	plt.close()
