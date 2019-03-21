<?xml version="1.0" encoding="UTF-8"?>
<simconf>
  <project EXPORT="discard">[APPS_DIR]/mrm</project>
  <project EXPORT="discard">[APPS_DIR]/mspsim</project>
  <project EXPORT="discard">[APPS_DIR]/avrora</project>
  <project EXPORT="discard">[APPS_DIR]/serial_socket</project>
  <project EXPORT="discard">[APPS_DIR]/collect-view</project>
  <project EXPORT="discard">[APPS_DIR]/powertracker</project>
  <project EXPORT="discard">[APPS_DIR]/mobility</project>
  <simulation>
    <title>flows4</title>
    <randomseed>123456</randomseed>
    <motedelay_us>1000000</motedelay_us>
    <radiomedium>
      org.contikios.cooja.radiomediums.UDGM
      <transmitting_range>75</transmitting_range>
      <interference_range>75</interference_range>
      <success_ratio_tx>1.0</success_ratio_tx>
      <success_ratio_rx>1.0</success_ratio_rx>
    </radiomedium>
    <events>
      <logoutput>40000</logoutput>
    </events>
    <motetype>
      org.contikios.cooja.mspmote.Z1MoteType
      <identifier>z11</identifier>
      <description>UDP receiver</description>
      <source EXPORT="discard">[CONTIKI_DIR]/examples/lpprdc/star/example-unicast.c</source>
      <commands EXPORT="discard">make example-unicast.z1 TARGET=z1</commands>
      <firmware EXPORT="copy">[CONTIKI_DIR]/examples/lpprdc/star/example-unicast.z1</firmware>
      <moteinterface>org.contikios.cooja.interfaces.Position</moteinterface>
      <moteinterface>org.contikios.cooja.interfaces.RimeAddress</moteinterface>
      <moteinterface>org.contikios.cooja.interfaces.IPAddress</moteinterface>
      <moteinterface>org.contikios.cooja.interfaces.Mote2MoteRelations</moteinterface>
      <moteinterface>org.contikios.cooja.interfaces.MoteAttributes</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.MspClock</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.MspMoteID</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.MspButton</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.Msp802154Radio</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.MspDefaultSerial</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.MspLED</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.MspDebugOutput</moteinterface>
    </motetype>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>0</x>
        <y>0</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>1</id>
      </interface_config>
      <motetype_identifier>z11</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>50</x>
        <y>50</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>2</id>
      </interface_config>
      <motetype_identifier>z11</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>-50</x>
        <y>-50</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>3</id>
      </interface_config>
      <motetype_identifier>z11</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>50</x>
        <y>-50</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>4</id>
      </interface_config>
      <motetype_identifier>z11</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>-50</x>
        <y>50</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>5</id>
      </interface_config>
      <motetype_identifier>z11</motetype_identifier>
    </mote>
  </simulation>
  <plugin>
    org.contikios.cooja.plugins.Visualizer
    <plugin_config>
      <skin>org.contikios.cooja.plugins.skins.IDVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.GridVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.MoteTypeVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.UDGMVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.TrafficVisualizerSkin</skin>
      <viewport>2.782161494977566 0.0 0.0 2.782161494977566 -28.668363458759103 -75.76000126990417</viewport>
    </plugin_config>
    <width>300</width>
    <z>4</z>
    <height>300</height>
    <location_x>153</location_x>
    <location_y>41</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.SimControl
    <width>318</width>
    <z>0</z>
    <height>192</height>
    <location_x>627</location_x>
    <location_y>48</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.LogListener
    <plugin_config>
      <filter />
      <formatted_time />
      <coloring />
    </plugin_config>
    <width>1422</width>
    <z>2</z>
    <height>239</height>
    <location_x>33</location_x>
    <location_y>494</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.TimeLine
    <plugin_config>
      <mote>0</mote>
      <mote>1</mote>
      <mote>2</mote>
      <mote>3</mote>
      <mote>4</mote>
      <showRadioRXTX />
      <showRadioHW />
      <showLogOutput />
      <zoomfactor>200.0</zoomfactor>
    </plugin_config>
    <width>1422</width>
    <z>3</z>
    <height>198</height>
    <location_x>0</location_x>
    <location_y>758</location_y>
  </plugin>
  <plugin>
    PowerTracker
    <width>400</width>
    <z>1</z>
    <height>400</height>
    <location_x>1065</location_x>
    <location_y>61</location_y>
  </plugin>
    <plugin>
    org.contikios.cooja.plugins.ScriptRunner
    <plugin_config>
      <script>/* Begin script */
TIMEOUT(361000, log.append(filename, mote.getSimulation().getCooja().getStartedPlugin("PowerTracker").radioStatistics());)

filename = "/home/user/git/contiki/examples/lpprdc/star/cooja-results/" + 
    mote.getSimulation().getTitle() +
    "-df1" +
    "-dai10" +
    "-tx75" +
    "-rdclpprdc" +
    "-ddccollisionddc" +
    "-opp0" +
    "-ccr2" +
    "-tbi100-" +
    mote.getSimulation().getRandomSeed() + ".txt";

numSenders = 4;
numPackets = 30;
packetCount = 0;

t = mote.getSimulation().getSimulationTime();
while(1) {
    log.append(filename, mote.getSimulation().getSimulationTime() + " " + mote.getID() + " " + msg + "\n");
    if(msg.indexOf("STARTING") != -1) {
        pos = mote.getInterfaces().getPosition();
        log.append(filename, "POSITION MOTE " + mote.getID() + " " + pos.getXCoordinate() + " " + pos.getYCoordinate() + " " + pos.getZCoordinate() + "\n");
    }
    
    if(msg.indexOf("RECEIVE") != -1) {
      if(parseInt(msg.split(" ")[2]) == 1) {
    		if(++packetCount == numSenders) {
    			packetCount = 0;
    			plugin = mote.getSimulation().getCooja().getStartedPlugin("PowerTracker");
   			  if (plugin != null) {
		        plugin.reset();
		      }
      		else {
       	   log.append(filename, "PowerTracker plugin not found.\n");
     			}
     		}
     	}

    	if(parseInt(msg.split(" ")[2]) == numPackets) {
    		if(++packetCount == numSenders) {
    			break;
  			}
			}
		}
    		
    
    YIELD(); /* wait for another mote output */
}

plugin = mote.getSimulation().getCooja().getStartedPlugin("PowerTracker");
      if (plugin != null) {
        stats = plugin.radioStatistics();
        log.append(filename, stats);
      }
      else {
          log.append(filename, "PowerTracker plugin not found.\n");
      }

log.testOK(); /* Report test success and quit */</script>
      <active>false</active>
    </plugin_config>
    <width>1213</width>
    <z>0</z>
    <height>813</height>
    <location_x>181</location_x>
    <location_y>20</location_y>
  </plugin>
</simconf>

