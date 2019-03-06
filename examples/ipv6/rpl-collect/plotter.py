import matplotlib.pyplot as plt
import matplotlib.colors as colors
import matplotlib.cm as cmx

class Plotter:
	def __init__(self, figureName=''):
		self.figureName = figureName
		self.nodeSize = 300
		self.nodeLabelFontsize = 8
		self.nodeLabelColor = 'black'
		self.colorMapName = 'plasma'
		
		self.connectionsZ = 0
		self.nodesZ = 1
		self.nodeLabelsZ = 2
		
		self.nodeColor = 'white'
		self.nodeWithPacketColor = 'green'
		self.nodeWithRadioOnColor = 'red'
		self.nodeInForwarderSetColor = 'lightblue'
		self.nodeBeaconingColor = 'purple'
		
		self.connectionLineStyle = 'solid'
		self.activeConnectionLineStyle = 'solid'
		
	def setFigureLimits(self, x, y):
		plt.xlim((0, x))
		plt.ylim((0, y))
		
	def saveFigure(self):
		plt.savefig(self.figureName)
		plt.close()
		
	def getGradientColorList(self, motes):
		metricVals = [m[3] for m in motes]
		finiteVals = [m for m in metricVals if m != -1]
		cm = plt.get_cmap(self.colorMapName)
		cNorm  = colors.Normalize(vmin=min(finiteVals), vmax=max(finiteVals))
		scalarMap = cmx.ScalarMappable(norm=cNorm, cmap=cm)
		colorList = []
		for val in metricVals:
			if val != -1:
				colorVal = scalarMap.to_rgba(val)
			else:
				colorVal = 'white'
			colorList.append(colorVal)
		return colorList
		
	def drawConnections(self, network, withState=False):
		artists = []
		for connection in network.connections:
			ls = self.connectionLineStyle
			if withState and connection.active:
				ls = self.activeConnectionLineStyle
			endPoints = connection.getEndPoints(network)
			artists.extend(plt.plot([endPoints[0][0], endPoints[1][0]], [endPoints[0][1], endPoints[1][1]], 
				color='black', ls=ls, zorder=self.connectionsZ))
		return artists
			
	def drawConnectionsInForwarderSets(self, network, sinkId):
		for connection in network.connections:
			for id in connection.nodeIds:
				i = network.nodes[id]
				jid = connection.getOtherNodeId(id)
				if i.forwarderSetContains(jid, sinkId):
					endPoints = connection.getEndPoints(network)
					plt.plot([endPoints[0][0], endPoints[1][0]], [endPoints[0][1], endPoints[1][1]], 
						color='black', ls=self.connectionLineStyle, zorder=self.connectionsZ)
					break

	def drawTreeConnections(self, motes, tree):
		for e in tree:
			start = (motes[e[0] - 1][1], motes[e[0] - 1][2])
			end = (motes[e[1] - 1][1], motes[e[1] - 1][2])
			r = 10
			x = end[0] - start[0]
			y = end[1] - start[1]
			import math
			dr = math.sqrt(x*x + y*y) - r

			angle = math.atan2(y, x)
			
			#angle -= math.pi / 2
			#if x < 0 and y <= 0:
			#	angle += math.pi
			dx = math.cos(angle) * dr
			dy = math.sin(angle) * dr
			plt.arrow(start[0], start[1], dx, dy,
				length_includes_head=True, width=1, zorder=self.connectionsZ)
			#plt.plot([start[0], end[0]], [start[1], end[1]],
			#	color='black', ls=self.connectionLineStyle, zorder=self.connectionsZ)

	def drawTreeNodes(self, motes, tree):
                colorList = self.getGradientColorList(motes)
		artists = []
		for i, m in enumerate(motes):
			c = 'white'
			if i > len(colorList) / 2:
				c = 'black'
			artists.append(plt.scatter(m[1], m[2], c=colorList[i], s=self.nodeSize, zorder=self.nodesZ))
			print(m)
			artists.append(plt.text(m[1], m[2], '%d\n%d' % (m[0], m[3]), ha='center', va='center', color=c, weight='bold',
				fontsize=self.nodeLabelFontsize, zorder=self.nodeLabelsZ))
		return artists
	
	def drawNodes(self, network, withState=False):
		artists = []
		if withState:
			nodesInForwarderSets = set()
			for node in [n for n in network.nodes if n.packetsRemaining()]:
				pkt = node.getNextPacketToSend()
				nodesInForwarderSets.update(node.getNextHopSet(pkt.sourceId, pkt.sequenceNumber))
		for node in network.nodes:
			c = self.nodeColor
			if withState:
				if node.beaconing:
					c = self.nodeWithRadioOnColor
				elif node.packetsRemaining():
					c = self.nodeWithPacketColor
				elif node.radioIsOn:
					c = self.nodeWithRadioOnColor
				elif node.id in nodesInForwarderSets:
					c = self.nodeInForwarderSetColor
			artists.append(plt.scatter(node.position.x, node.position.y, c=c, s=self.nodeSize, zorder=self.nodesZ))
			artists.append(plt.text(node.position.x, node.position.y, node.id, ha='center', va='center', 
				fontsize=self.nodeLabelFontsize, zorder=self.nodeLabelsZ))
		return artists
			
	def drawNodesWithGradient(self, network, sinkId):
		colorList = self.getGradientColorList(network, sinkId)
		for i, node in enumerate(network.nodes):
			plt.scatter(node.position.x, node.position.y, c=colorList[i], s=self.nodeSize, zorder=self.nodesZ)
			plt.text(node.position.x, node.position.y, node.id, ha='center', va='center', 
				fontsize=self.nodeLabelFontsize, zorder=self.nodeLabelsZ)
				
	def drawClock(self, simulation):
		artist = plt.text(0, 0, simulation.t, ha='left', va='bottom')
		return artist
	
	def plotSingleSinkNetwork(self, simulation, sinkId):
		self.setFigureLimits(simulation)	
		self.drawConnectionsInForwarderSets(simulation.network, sinkId)
		self.drawNodesWithGradient(simulation.network, sinkId)
		self.saveFigure()
		
	def plotNetworkState(self, simulation):
		artists = []
		self.setFigureLimits(simulation)
		artists.extend(self.drawConnections(simulation.network, withState=True))
		artists.extend(self.drawNodes(simulation.network, withState=True))
		artists.append(self.drawClock(simulation))
		#plt.close()
		return artists
		
	def plotTopology(self, motes, tree):
		minX = min([m[1] for m in motes])
		if minX < 10:
			motes = [(m[0], m[1] + (10 - minX), m[2], m[3]) for m in motes]
		minY = min([m[2] for m in motes])
		if minY < 10:
			motes = [(m[0], m[1], m[2] + (10 - minY), m[3]) for m in motes]
		self.setFigureLimits(max([m[1] + 10 for m in motes]), max([m[2] + 10 for m in motes]))
		self.drawTreeConnections(motes, tree)
		self.drawTreeNodes(motes, tree)
		self.saveFigure()
		
		
