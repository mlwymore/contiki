import argparse
import numpy as np
import matplotlib.pyplot as plt
parser = argparse.ArgumentParser(description='Plot packet delivery ratio from Cooja files.')
parser.add_argument('fileNames', metavar='Files', nargs='+', type=str, help='Cooja files to plot.')

args = parser.parse_args()

print('Plotting from files %s.' % args.fileNames)


ccrs = set()
prots = set()
dais = set()
txs = set()
flows = set()
tbis = set()
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
				dai = float(fileName[ind + 3:ind + 6])
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
	if 'lpprdc' in fileName:
		ind = fileName.find('tbi')
		if ind >= 0:
			try:
				tbi = int(fileName[ind + 3:ind + 6])
			except ValueError:
				try:
					tbi = int(fileName[ind + 3:ind + 5])
				except:
					print("tbi smaller than expected")
			tbis.add(tbi)
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
tbis = sorted(list(tbis))
print(tbis)

plot_delay = True

series = {
	('lpprdc', 2): {
		'color':'red'
	},
	
	('rimac', 2): {
		'color':'blue'
	},
	
	('contikimac', 2): {
		'color':'green'
	},
	
	('contikimac', 4): {
		'color':'brown'
	},
	
	('contikimac', 8): {
		'color':'orange'
	},
	
	('contikimac', 16): {
		'color':'purple'
	}
}

#('name', xs, dai, ccr, flows, txr, tbi, xlabel)
configs = [('flows', flows, 1, 'all', 'x', 100, 125, 'Number of flows'),
					 ('flows', flows, 2, 'all', 'x', 100, 125, 'Number of flows'),
					 ('tbi', tbis, 1, 'all', 1, 100, 'x', 'Size of initial beacon (bytes)'),
					 ('tbi', tbis, 1, 'all', 2, 100, 'x', 'Size of initial beacon (bytes)'),
					 ('tbi', tbis, 1, 'all', 3, 100, 'x', 'Size of initial beacon (bytes)'),
					 ('tbi', tbis, 1, 'all', 4, 100, 'x', 'Size of initial beacon (bytes)'),
					 ('tbi', tbis, 2, 'all', 1, 100, 'x', 'Size of initial beacon (bytes)'),
					 ('tbi', tbis, 2, 'all', 4, 100, 'x', 'Size of initial beacon (bytes)'),
					 ('dai', dais, 'x', 'all', 1, 100, 125, 'Data arrival interval (s)'),
					 ('dai', dais, 'x', 'all', 2, 100, 125, 'Data arrival interval (s)'),
					 ('dai', dais, 'x', 'all', 3, 100, 125, 'Data arrival interval (s)'),
					 ('dai', dais, 'x', 'all', 4, 100, 125, 'Data arrival interval (s)')
					]

for config in configs:

	pdrdata = dict() #{prot: ([x1, x2,...], [pdr1, pdr2,...], [recs1, recs2,...]), ...}
	senderdcdata = dict()
	recvrdcdata = dict()
	delaydata = dict()

	name = config[0]
	xs = config[1]
	dai = config[2]
	ccr = config[3]
	flow = config[4]
	txr = config[5]
	tbi = config[6]
	xlabel = config[7]
	
	for prot in prots:
		
		if ccr == 'all':
			pccrs = ccrs
		else:
			pccrs = [ccr]
		
		for pccr in pccrs:
			pdrdata[(prot, pccr)] = ([], [], [])
			senderdcdata[(prot, pccr)] = ([], [])
			recvrdcdata[(prot, pccr)] = ([], [])
			delaydata[(prot, pccr)] = ([], [])
		
			for x in xs:
				pdrs = []
				recs = []
				numMotes = 0
				senderdcs = []
				recvrdcs = []
				avgdelays = []
			
				for fileName in [f for f in args.fileNames if 'rdc%s-' % prot in f and
													('ccr%d-' % pccr in f) and
													('lpprdc' not in f or (tbi == 'x' and 'tbi%d-' % x in f) or (tbi != 'x' and 'tbi%d-' % tbi in f)) and
													((txr == 'x' and 'txr%d-' % x in f) or (txr != 'x' and 'tx%d-' % txr in f)) and
													((dai == 'x' and 'dai%s-' % x in f) or (dai != 'x' and 'dai%s-' % dai in f)) and
													((flow == 'x' and 'flows%d' % x in f) or (flow != 'x' and 'flows%d' % flow in f))]:

					#pdr stuff
					with open(fileName) as infile:
						sent = len([l for l in infile if 'SEND' in l])
						
					with open(fileName) as infile:
						delivered = len([l for l in infile if 'RECEIVE' in l])
						
					pdrs.append(float(delivered) / float(sent))
					if flow != 'x':
						recs.append(float(delivered) / float(flow))
					else:
						recs.append(float(delivered) / float(x))
					
					#duty cycle stuff
					with open(fileName) as infile:
						onLines = [l.rstrip() for l in infile if ' ON ' in l]
					
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
						if moteid % 2 == 0:
							sendervals.append(float(words[4]))
						else:
							recvrvals.append(float(words[4]))
					senderdcs.append(np.mean(sendervals))
					recvrdcs.append(np.mean(recvrvals))
					
					#delay stuff
					if plot_delay:
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

				if pdrs:
					pdrdata[(prot, pccr)][0].append(x);
					pdrdata[(prot, pccr)][1].append(np.mean(pdrs))
					pdrdata[(prot, pccr)][2].append(np.mean(recs))
					
				if senderdcs:
					senderdcdata[(prot, pccr)][0].append(x)
					senderdcdata[(prot, pccr)][1].append(np.mean(senderdcs))
					recvrdcdata[(prot, pccr)][0].append(x)
					recvrdcdata[(prot, pccr)][1].append(np.mean(recvrdcs))
					
				if avgdelays:
					delaydata[(prot, pccr)][0].append(x)
					delaydata[(prot, pccr)][1].append(np.mean(avgdelays))
					
			if not pdrdata[(prot, pccr)][0]:
				del pdrdata[(prot, pccr)]
				
			if not senderdcdata[(prot, pccr)][0]:
				del senderdcdata[(prot, pccr)]
				del recvrdcdata[(prot, pccr)]
				
			if not delaydata[(prot, pccr)][0]:
				del delaydata[(prot, pccr)]
	
	#make pdr fig
	for key, xys in pdrdata.items():
		if len(xys[0]) > 1:
			plt.plot(xys[0], xys[1], label='%s-%d' % key, c=series[key]['color'])
		elif len(xys[0]) == 1:
			plt.scatter(xys[0], xys[1], label='%s-%d' % key)
	plt.legend()
	plt.xlabel(xlabel)
	plt.ylabel('Avg. packet delivery ratio')
	plt.savefig('%s_avg_pdr_dai%s_tx%s_flow%s_tbi%s.pdf' % (name, dai, tx, flow, tbi))
	plt.close()
	
	#make recs fig
	for key, xys in pdrdata.items():
		if len(xys[0]) > 1:
			plt.plot(xys[0], xys[2], label='%s-%d' % key, c=series[key]['color'])
		elif len(xys[0]) == 1:
			plt.scatter(xys[0], xys[2], label='%s-%d' % key)
	plt.legend()
	plt.xlabel(xlabel)
	plt.ylabel('Avg. packets delivered per flow')
	plt.savefig('%s_avg_recs_dai%s_tx%s_flow%s_tbi%s.pdf' % (name, dai, tx, flow, tbi))
	plt.close()
	
	#make sender dc fig
	for key, xy in senderdcdata.items():
		if len(xy[0]) > 1:
			plt.plot(xy[0], xy[1], label='%s-%d' % key, c=series[key]['color'])
		elif len(xy[0]) == 1:
			plt.scatter(xy[0], xy[1], label='%s-%d' % key)
	plt.legend()
	plt.xlabel(xlabel)
	plt.ylabel('Avg. sender duty cycle (%)')
	plt.savefig('%s_avg_duty_cycle_sender_dai%s_tx%s_flow%s_tbi%s.pdf' % (name, dai, tx, flow, tbi))
	plt.close()
	
	#make receiver dc fig
	for key, xy in recvrdcdata.items():
		if len(xy[0]) > 1:
			plt.plot(xy[0], xy[1], label='%s-%d' % key, c=series[key]['color'])
		elif len(xy[0]) == 1:
			plt.scatter(xy[0], xy[1], label='%s-%d' % key)
	plt.legend()
	plt.xlabel(xlabel)
	plt.ylabel('Avg. receiver duty cycle (%)')
	plt.savefig('%s_avg_duty_cycle_receiver_dai%s_tx%s_flow%s_tbi%s.pdf' % (name, dai, tx, flow, tbi))
	plt.close()
	
	#make delay fig
	if plot_delay:
		for key, xy in delaydata.items():
			if len(xy[0]) > 1:
				plt.plot(xy[0], xy[1], label='%s-%d' % key, c=series[key]['color'])
			elif len(xy[0]) == 1:
				plt.scatter(xy[0], xy[1], label='%s-%d' % key)
		plt.legend()
		plt.xlabel(xlabel)
		plt.ylabel('Avg. delay (s)')
		plt.savefig('%s_avg_delay_dai%s_tx%s_flow%s_tbi%s.pdf' % (name, dai, tx, flow, tbi))
		plt.close()
