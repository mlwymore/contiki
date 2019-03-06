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
    <title>eep test</title>
    <randomseed>123456</randomseed>
    <motedelay_us>1000000</motedelay_us>
    <radiomedium>
      org.contikios.cooja.radiomediums.DirectedGraphMedium
      <edge>
        <source>6</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>5</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>6</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>3</radio>
          <ratio>0.8</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>6</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>4</radio>
          <ratio>0.5000000000000001</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>5</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>2</radio>
          <ratio>0.9</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>5</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>3</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>2</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>1</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>2</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>3</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>3</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>1</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>3</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>4</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>4</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>1</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>5</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>6</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>3</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>6</radio>
          <ratio>0.8</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>4</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>6</radio>
          <ratio>0.5000000000000001</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>2</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>5</radio>
          <ratio>0.9</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>3</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>5</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>1</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>2</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>3</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>2</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>1</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>3</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>4</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>3</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
      <edge>
        <source>1</source>
        <dest>
          org.contikios.cooja.radiomediums.DGRMDestinationRadio
          <radio>4</radio>
          <ratio>1.0</ratio>
          <signal>-10.0</signal>
          <lqi>105</lqi>
          <delay>0</delay>
          <channel>-1</channel>
        </dest>
      </edge>
    </radiomedium>
    <events>
      <logoutput>40000</logoutput>
    </events>
    <motetype>
      org.contikios.cooja.mspmote.Z1MoteType
      <identifier>z11</identifier>
      <description>Z1 Mote Type #z11</description>
      <source EXPORT="discard">[CONTIKI_DIR]/examples/ipv6/rpl-collect/udp-sink.c</source>
      <commands EXPORT="discard">make udp-sink.z1 TARGET=z1</commands>
      <firmware EXPORT="copy">[CONTIKI_DIR]/examples/ipv6/rpl-collect/udp-sink.z1</firmware>
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
    <motetype>
      org.contikios.cooja.mspmote.Z1MoteType
      <identifier>z12</identifier>
      <description>Z1 Mote Type #z12</description>
      <source EXPORT="discard">[CONTIKI_DIR]/examples/ipv6/rpl-collect/udp-sender.c</source>
      <commands EXPORT="discard">make udp-sender.z1 TARGET=z1</commands>
      <firmware EXPORT="copy">[CONTIKI_DIR]/examples/ipv6/rpl-collect/udp-sender.z1</firmware>
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
        <x>-61.07076877987839</x>
        <y>60.16596768479104</y>
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
        <x>-32.034004071984455</x>
        <y>34.21528994896488</y>
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
      <motetype_identifier>z12</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>-30.82047342624834</x>
        <y>58.218022278943224</y>
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
      <motetype_identifier>z12</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>-29.501253121013278</x>
        <y>84.12310623472598</y>
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
      <motetype_identifier>z12</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>9.074600532647011</x>
        <y>44.430433766162814</y>
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
      <motetype_identifier>z12</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>27.571341501959022</x>
        <y>72.00675515339968</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>6</id>
      </interface_config>
      <motetype_identifier>z12</motetype_identifier>
    </mote>
  </simulation>
  <plugin>
    org.contikios.cooja.plugins.SimControl
    <width>280</width>
    <z>5</z>
    <height>160</height>
    <location_x>400</location_x>
    <location_y>0</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.Visualizer
    <plugin_config>
      <moterelations>true</moterelations>
      <skin>org.contikios.cooja.plugins.skins.IDVisualizerSkin</skin>
      <viewport>2.5046617093501835 0.0 0.0 2.5046617093501835 239.59797975990372 -6.317884315600817</viewport>
    </plugin_config>
    <width>400</width>
    <z>1</z>
    <height>400</height>
    <location_x>1</location_x>
    <location_y>1</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.LogListener
    <plugin_config>
      <filter />
      <formatted_time />
      <coloring />
    </plugin_config>
    <width>1280</width>
    <z>4</z>
    <height>240</height>
    <location_x>400</location_x>
    <location_y>160</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.TimeLine
    <plugin_config>
      <mote>0</mote>
      <mote>1</mote>
      <mote>2</mote>
      <mote>3</mote>
      <mote>4</mote>
      <mote>5</mote>
      <showRadioRXTX />
      <showRadioHW />
      <showLEDs />
      <zoomfactor>500.0</zoomfactor>
    </plugin_config>
    <width>1680</width>
    <z>3</z>
    <height>166</height>
    <location_x>0</location_x>
    <location_y>806</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.Notes
    <plugin_config>
      <notes>Enter notes here</notes>
      <decorations>true</decorations>
    </plugin_config>
    <width>1000</width>
    <z>2</z>
    <height>160</height>
    <location_x>680</location_x>
    <location_y>0</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.DGRMConfigurator
    <width>400</width>
    <z>0</z>
    <height>300</height>
    <location_x>222</location_x>
    <location_y>426</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.ScriptRunner
    <plugin_config>
      <script>/* Begin script */
TIMEOUT(214000);

filename = "/home/user/git/contiki/examples/ipv6/rpl-collect/cooja-results/" + 
    mote.getSimulation().getTitle() +
    "-dai30" +
    "-rdccontikimac" +
    "-ccr2-" +
    mote.getSimulation().getRandomSeed() + ".txt";

doneBootingUp = false;

t = mote.getSimulation().getSimulationTime();
while(1) {
    if(mote.getSimulation().getSimulationTime() - t > 60000000) {
      plugin = mote.getSimulation().getCooja().getStartedPlugin("PowerTracker");
      if (plugin != null) {
        stats = plugin.radioStatistics();
        log.append(filename, stats);
        if(!doneBootingUp) {
          doneBootingUp = true;
          plugin.reset();
        }
      }
      else {
          log.append(filename, "PowerTracker plugin not found.\n");
      }
      t = mote.getSimulation().getSimulationTime();
    }
    log.append(filename, mote.getSimulation().getSimulationTime() + " " + mote.getID() + " " + msg + "\n");
    if(msg.indexOf("STARTING") != -1) {
        pos = mote.getInterfaces().getPosition();
        log.append(filename, "POSITION MOTE " + mote.getID() + " " + pos.getXCoordinate() + " " + pos.getYCoordinate() + " " + pos.getZCoordinate() + "\n");
    }
    
    YIELD(); /* wait for another mote output */
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

