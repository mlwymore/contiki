import argparse
import numpy as np
import matplotlib.pyplot as plt
plt.rcParams.update({'font.size': 26})
plt.rcParams.update({'legend.handlelength': 2})
parser = argparse.ArgumentParser(description='Plot packet delivery ratio from Cooja files.')
parser.add_argument('-d', dest='withDelay', metavar='Delay', nargs=1, type=bool, help='Include delay plots.')
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

plot_delay = args.withDelay

series = {
	('lpprdc', 2, 100): {
		'color':'red',
		'marker':'o',
		'ls':'solid',
		'label':'RIVER-MAC',
		'z':5
	},
	
	('rimac', 2, 100): {
		'color':'blue',
		'marker':'^',
		'ls':'dashed',
		'label':'RI-MAC',
		'z':4
	},
	
	('contikimac', 2, 100): {
		'color':'green',
		'marker':'s',
		'ls':'-.',
		'label':'ContikiMAC',
		'z':3
	},
	
#	('contikimac', 8, 100): {
#		'color':'yellow'
#	},
	
#	('contikimac', 16, 100): {
#		'color':'orange'
#	},
	
#	('contikimac', 32, 100): {
#		'color':'brown'
#	},
	
#	('lpprdc', 2, 100): {
#		'color':'orange'
#	},
	
#	('lpprdc', 2, 75): {
#		'color':'purple'
#	},
	
#	('lpprdc', 2, 50): {
#		'color':'brown'
#	},
	
#	('lpprdc', 2, 25): {
#		'color':'pink'
#	},
	
#	('lpprdc', 4): {
#		'color' : 'yellow'
#	},
	
#	('rimac', 2, 125): {
#		'color':'blue'
#	},
	
#	('contikimac', 2, 125): {
#		'color':'green'
#	},
	
#	('contikimac', 4): {
#		'color':'brown'
#	},
	
#	('contikimac', 8): {
#		'color':'purple'
#	},
	
#	('contikimac', 16): {
#		'color':'orange'
#	}
}

#('name', xs, dai, ccr, flows, txr, tbi, xlabel)
configs = [
#					 ('flows', flows, 1, 'all', 'x', 100, 125, 'Number of flows'),
					 ('flows', flows, 1, 'all', 'x', 75, 'all', 'Number of senders'),
#					 ('tbi', tbis, 1, 'all', 1, 100, 'x', 'Size of initial beacon (bytes)'),
#					 ('tbi', tbis, 1, 'all', 2, 100, 'x', 'Size of initial beacon (bytes)'),
#					 ('tbi', tbis, 1, 'all', 3, 100, 'x', 'Size of initial beacon (bytes)'),
#					 ('tbi', tbis, 1, 'all', 4, 100, 'x', 'Size of initial beacon (bytes)'),
#					 ('tbi', tbis, 2, 'all', 1, 100, 'x', 'Size of initial beacon (bytes)'),
#					 ('tbi', tbis, 2, 'all', 4, 100, 'x', 'Size of initial beacon (bytes)'),
#					 ('dai', dais, 'x', 'all', 1, 100, 125, 'Data arrival interval (s)'),
#					 ('dai', dais, 'x', 'all', 2, 100, 125, 'Data arrival interval (s)'),
#					 ('dai', dais, 'x', 'all', 3, 100, 125, 'Data arrival interval (s)'),
#					 ('dai', dais, 'x', 'all', 4, 100, 'all', 'Data arrival interval (s)')
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
			
		if tbi == 'all':
			ptbis = tbis
		else:
			ptbis = [tbi]
		
		for ptbi in ptbis:
			for pccr in [p for p in pccrs if (prot, p, ptbi) in series]:
				pdrdata[(prot, pccr, ptbi)] = ([], [], [], [], [])
				senderdcdata[(prot, pccr, ptbi)] = ([], [], [], [])
				recvrdcdata[(prot, pccr, ptbi)] = ([], [], [], [])
				delaydata[(prot, pccr, ptbi)] = ([], [], [], [])
			
				for x in xs:
					pdrs = []
					recs = []
					numMotes = 0
					senderdcs = []
					recvrdcs = []
					avgdelays = []
				
					for fileName in [f for f in args.fileNames if 'rdc%s-' % prot in f and
														('ccr%d-' % pccr in f) and
														('lpprdc' not in f or (ptbi == 'x' and 'tbi%d-' % x in f) or (ptbi != 'x' and 'tbi%d-' % ptbi in f)) and
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
							
						if not onLines:
							print('No onlines for %s' % fileName)
							continue
						
						if numMotes == 0:
							words = onLines[-1].split()
							words = words[0].split('_')
							numMotes = int(words[1])
						onLines = onLines[-numMotes:]
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
									rls = rls[0]
								if rls:
									receiveTime = int(rls[0].split()[0])
									delay = (receiveTime - sendTime) / 1000000.0
									delays.append(delay)
									if prot == 'contikimac' and x >= 3:
										print("%s: delay %d for packet %d-%d" % (fileName, delay, sender, seqno))
							if len(delays) != len(receiveLines):
								print("warning: did not find delay for all packets")
							if delays:
								if prot == 'contikimac' and x >= 3:
									print(fileName, delays)
								avgdelays.append(np.mean(delays))		

					if pdrs:
						print(prot, pccr, ptbi, len(pdrs))
						pdrdata[(prot, pccr, ptbi)][0].append(x);
						pdrdata[(prot, pccr, ptbi)][1].append(np.mean(pdrs))
						pdrdata[(prot, pccr, ptbi)][2].append(np.mean(recs))
						pdrdata[(prot, pccr, ptbi)][3].append(np.quantile(pdrs, 0.05))
						pdrdata[(prot, pccr, ptbi)][4].append(np.quantile(pdrs, 0.95))
						#pdrdata[(prot, pccr, ptbi)][3].append(2*np.std(pdrs))
						
					if senderdcs:
						senderdcdata[(prot, pccr, ptbi)][0].append(x)
						senderdcdata[(prot, pccr, ptbi)][1].append(np.mean(senderdcs))
						#senderdcdata[(prot, pccr, ptbi)][2].append(2*np.std(senderdcs))
						senderdcdata[(prot, pccr, ptbi)][2].append(np.quantile(senderdcs, 0.05))
						senderdcdata[(prot, pccr, ptbi)][3].append(np.quantile(senderdcs, 0.95))
						recvrdcdata[(prot, pccr, ptbi)][0].append(x)
						recvrdcdata[(prot, pccr, ptbi)][1].append(np.mean(recvrdcs))
						#recvrdcdata[(prot, pccr, ptbi)][2].append(2*np.std(recvrdcs))
						recvrdcdata[(prot, pccr, ptbi)][2].append(np.quantile(recvrdcs, 0.05))
						recvrdcdata[(prot, pccr, ptbi)][3].append(np.quantile(recvrdcs, 0.95))
						
					if avgdelays:
						delaydata[(prot, pccr, ptbi)][0].append(x)
						delaydata[(prot, pccr, ptbi)][1].append(np.mean(avgdelays))
						delaydata[(prot, pccr, ptbi)][2].append(np.quantile(avgdelays, 0.05))
						delaydata[(prot, pccr, ptbi)][3].append(np.quantile(avgdelays, 0.95))
						#delaydata[(prot, pccr, ptbi)][2].append(2*np.std(avgdelays))
						
				if not pdrdata[(prot, pccr, ptbi)][0]:
					del pdrdata[(prot, pccr, ptbi)]
					
				if not senderdcdata[(prot, pccr, ptbi)][0]:
					del senderdcdata[(prot, pccr, ptbi)]
					del recvrdcdata[(prot, pccr, ptbi)]
					
				if not delaydata[(prot, pccr, ptbi)][0]:
					del delaydata[(prot, pccr, ptbi)]
	
	
	#make pdr fig
	plt.figure(figsize=(10,7.5))
	for key, xys in pdrdata.items():
		if len(xys[0]) > 1:
			plt.errorbar(xys[0], xys[1], yerr=[np.subtract(xys[1], xys[3]), np.subtract(xys[4], xys[1])], capsize=10, capthick=3, elinewidth=3, label=series[key]['label'], c=series[key]['color'], marker=series[key]['marker'], lw=5, ms=18, ls=series[key]['ls'], zorder=series[key]['z'])
	plt.legend()
	plt.xticks(xs)
	plt.xlabel(xlabel)
	plt.ylabel('Avg. PDR')
	plt.ylim([0, 1.05])
	plt.tight_layout()
	plt.savefig('star_%s_avg_pdr_dai%s_tx%s_flow%s_tbi%s.pdf' % (name, dai, tx, flow, tbi))
	plt.close()
	
	#make recs fig
	plt.figure(figsize=(10,7.5))
	for key, xys in pdrdata.items():
		if len(xys[0]) > 1:
			plt.plot(xys[0], xys[2], label=series[key]['label'], c=series[key]['color'], marker=series[key]['marker'], lw=5, ms=18, ls=series[key]['ls'], zorder=series[key]['z'])
	plt.legend()
	plt.xticks(xs)
	plt.xlabel(xlabel)
	plt.ylabel('Avg. packets delivered per sender')
	plt.tight_layout()
	plt.savefig('star_%s_avg_recs_dai%s_tx%s_flow%s_tbi%s.pdf' % (name, dai, tx, flow, tbi))
	plt.close()
	
	#make sender dc fig
	plt.figure(figsize=(10,7.5))
	for key, xys in senderdcdata.items():
		if len(xys[0]) > 1:
			plt.errorbar(xys[0], xys[1], yerr=[np.subtract(xys[1], xys[2]), np.subtract(xys[3], xys[1])], capsize=10, capthick=3, elinewidth=3, label=series[key]['label'], c=series[key]['color'], marker=series[key]['marker'], lw=5, ms=18, ls=series[key]['ls'], zorder=series[key]['z'])
	plt.legend()
	plt.xticks(xs)
	plt.xlabel(xlabel)
	plt.ylabel('Avg. sender DC (%)')
	plt.ylim([10**-1, 10**2])
	plt.yscale('log')
	plt.tight_layout()
	plt.savefig('star_%s_avg_duty_cycle_sender_dai%s_tx%s_flow%s_tbi%s.pdf' % (name, dai, tx, flow, tbi))
	plt.close()
	
	#make receiver dc fig
	plt.figure(figsize=(10,7.5))
	for key, xys in recvrdcdata.items():
		if len(xys[0]) > 1:
			plt.errorbar(xys[0], xys[1], yerr=[np.subtract(xys[1], xys[2]), np.subtract(xys[3], xys[1])], capsize=10, capthick=3, elinewidth=3, label=series[key]['label'], c=series[key]['color'], marker=series[key]['marker'], lw=5, ms=18, ls=series[key]['ls'], zorder=series[key]['z'])
	plt.legend()
	plt.xticks(xs)
	plt.xlabel(xlabel)
	plt.ylabel('Avg. receiver DC (%)')
	plt.ylim([10**-1, 10**2])
	plt.yscale('log')
	plt.tight_layout()
	plt.savefig('star_%s_avg_duty_cycle_receiver_dai%s_tx%s_flow%s_tbi%s.pdf' % (name, dai, tx, flow, tbi))
	plt.close()
	
	#make delay fig
	plt.figure(figsize=(10,7.5))
	if plot_delay:
		for key, xys in delaydata.items():
			if len(xys[0]) > 1:
				plt.errorbar(xys[0], xys[1], yerr=[np.subtract(xys[1], xys[2]), np.subtract(xys[3], xys[1])], capsize=10, capthick=3, elinewidth=3, label=series[key]['label'], c=series[key]['color'], marker=series[key]['marker'], lw=5, ms=18, ls=series[key]['ls'], zorder=series[key]['z'])
		plt.legend(loc='upper right', bbox_to_anchor=(0.65, 0.425))
		plt.xticks(xs)
		plt.xlabel(xlabel)
		plt.ylabel('Avg. delay (s)')
		plt.tight_layout()
		plt.savefig('star_%s_avg_delay_dai%s_tx%s_flow%s_tbi%s.pdf' % (name, dai, tx, flow, tbi))
		plt.close()