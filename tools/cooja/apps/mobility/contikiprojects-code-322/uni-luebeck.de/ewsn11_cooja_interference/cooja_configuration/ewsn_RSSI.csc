<?xml version="1.0" encoding="UTF-8"?>
<simconf>
  <project EXPORT="discard">[CONTIKI_DIR]/tools/cooja/apps/mrm</project>
  <project EXPORT="discard">[CONTIKI_DIR]/tools/cooja/apps/mspsim</project>
  <project EXPORT="discard">[CONTIKI_DIR]/tools/cooja/apps/avrora</project>
  <project EXPORT="discard">[CONTIKI_DIR]/tools/cooja/apps/serial_socket</project>
  <project EXPORT="discard">[CONTIKI_DIR]/tools/cooja/apps/collect-view</project>
  <project EXPORT="discard">[CONFIG_DIR]/../frossi-scanner</project>
  <project EXPORT="discard">[CONFIG_DIR]</project>
  <simulation>
    <title>External Interference (EWSN 2011)</title>
    <delaytime>-2147483648</delaytime>
    <randomseed>123457</randomseed>
    <motedelay_us>1000000</motedelay_us>
    <radiomedium>
      se.sics.mrm.MRM
      <rt_diffr_coefficient>-10.0</rt_diffr_coefficient>
      <bg_noise_var>1.0</bg_noise_var>
      <rt_max_diffractions>0</rt_max_diffractions>
      <snr_threshold>6.0</snr_threshold>
      <rt_max_reflections>1</rt_max_reflections>
      <tx_power>1.5</tx_power>
      <rx_antenna_gain>0.0</rx_antenna_gain>
      <apply_random>false</apply_random>
      <rt_ignore_non_direct>false</rt_ignore_non_direct>
      <rt_refrac_coefficient>-3.0</rt_refrac_coefficient>
      <rt_max_refractions>1</rt_max_refractions>
      <system_gain_var>4.0</system_gain_var>
      <rt_max_rays>1</rt_max_rays>
      <rt_disallow_direct_path>false</rt_disallow_direct_path>
      <rx_sensitivity>-100.0</rx_sensitivity>
      <obstacle_attenuation>-3.0</obstacle_attenuation>
      <rt_reflec_coefficient>-5.0</rt_reflec_coefficient>
      <bg_noise_mean>-100.0</bg_noise_mean>
      <rt_fspl_on_total_length>true</rt_fspl_on_total_length>
      <system_gain_mean>0.0</system_gain_mean>
      <tx_antenna_gain>0.0</tx_antenna_gain>
      <wavelength>0.346</wavelength>
      <obstacles />
    </radiomedium>
    <events>
      <logoutput>4000000</logoutput>
    </events>
    <motetype>
      se.sics.cooja.motes.ImportAppMoteType
      <identifier>apptype1</identifier>
      <description>Microwave</description>
      <motepath>[CONFIG_DIR]/build</motepath>
      <moteclass>MicrowaveInterferer</moteclass>
    </motetype>
    <motetype>
      se.sics.cooja.mspmote.SkyMoteType
      <identifier>sky1</identifier>
      <description>Frossi scanner</description>
      <firmware EXPORT="copy">[CONFIG_DIR]/../frossi-scanner/frossi.sky</firmware>
      <moteinterface>se.sics.cooja.interfaces.Position</moteinterface>
      <moteinterface>se.sics.cooja.interfaces.RimeAddress</moteinterface>
      <moteinterface>se.sics.cooja.interfaces.IPAddress</moteinterface>
      <moteinterface>se.sics.cooja.interfaces.Mote2MoteRelations</moteinterface>
      <moteinterface>se.sics.cooja.interfaces.MoteAttributes</moteinterface>
      <moteinterface>se.sics.cooja.mspmote.interfaces.MspClock</moteinterface>
      <moteinterface>se.sics.cooja.mspmote.interfaces.MspMoteID</moteinterface>
      <moteinterface>se.sics.cooja.mspmote.interfaces.SkyButton</moteinterface>
      <moteinterface>se.sics.cooja.mspmote.interfaces.SkyFlash</moteinterface>
      <moteinterface>se.sics.cooja.mspmote.interfaces.SkyCoffeeFilesystem</moteinterface>
      <moteinterface>se.sics.cooja.mspmote.interfaces.SkyByteRadio</moteinterface>
      <moteinterface>se.sics.cooja.mspmote.interfaces.MspSerial</moteinterface>
      <moteinterface>se.sics.cooja.mspmote.interfaces.SkyLED</moteinterface>
      <moteinterface>se.sics.cooja.mspmote.interfaces.MspDebugOutput</moteinterface>
      <moteinterface>se.sics.cooja.mspmote.interfaces.SkyTemperature</moteinterface>
    </motetype>
    <motetype>
      se.sics.cooja.motes.ImportAppMoteType
      <identifier>apptype2</identifier>
      <description>Wifi basestation</description>
      <motepath>[CONFIG_DIR]/build</motepath>
      <moteclass>WifiInterferer</moteclass>
    </motetype>
    <motetype>
      se.sics.cooja.motes.ImportAppMoteType
      <identifier>apptype3</identifier>
      <description>Bluetooth</description>
      <motepath>[CONFIG_DIR]/build</motepath>
      <moteclass>BluetoothInterferer</moteclass>
    </motetype>
    <motetype>
      se.sics.cooja.motes.ImportAppMoteType
      <identifier>apptype4</identifier>
      <description>Live Capture Trace</description>
      <motepath>[CONFIG_DIR]/build</motepath>
      <moteclass>LiveTraceInterferer</moteclass>
    </motetype>
    <mote>
      <interface_config>
        se.sics.cooja.motes.AbstractApplicationMoteType$SimpleMoteID
        <id>1</id>
      </interface_config>
      <interface_config>
        se.sics.cooja.interfaces.Position
        <x>70.13945681747965</x>
        <y>29.366586704315456</y>
        <z>0.0</z>
      </interface_config>
      <interfererActive />
      <motetype_identifier>apptype1</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        se.sics.cooja.interfaces.Position
        <x>79.80226432092695</x>
        <y>40.15297647560547</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        se.sics.cooja.mspmote.interfaces.MspMoteID
        <id>2</id>
      </interface_config>
      <motetype_identifier>sky1</motetype_identifier>
    </mote>
    <mote>
      <interface_config>
        se.sics.cooja.motes.AbstractApplicationMoteType$SimpleMoteID
        <id>3</id>
      </interface_config>
      <interface_config>
        se.sics.cooja.interfaces.Position
        <x>89.5774300511585</x>
        <y>29.14187025074691</y>
        <z>0.0</z>
      </interface_config>
      <motetype_identifier>apptype2</motetype_identifier>
    </mote>
    <mote>
      <interface_config>
        se.sics.cooja.motes.AbstractApplicationMoteType$SimpleMoteID
        <id>4</id>
      </interface_config>
      <interface_config>
        se.sics.cooja.interfaces.Position
        <x>69.80238213712684</x>
        <y>49.59106752548423</y>
        <z>0.0</z>
      </interface_config>
      <motetype_identifier>apptype3</motetype_identifier>
    </mote>
    <mote>
      <interface_config>
        se.sics.cooja.motes.AbstractApplicationMoteType$SimpleMoteID
        <id>5</id>
      </interface_config>
      <interface_config>
        se.sics.cooja.interfaces.Position
        <x>90.02686295829561</x>
        <y>49.92814220583704</y>
        <z>0.0</z>
      </interface_config>
      <motetype_identifier>apptype4</motetype_identifier>
    </mote>
  </simulation>
  <plugin>
    se.sics.cooja.plugins.SimControl
    <width>259</width>
    <z>0</z>
    <height>205</height>
    <location_x>0</location_x>
    <location_y>0</location_y>
  </plugin>
  <plugin>
    se.sics.cooja.plugins.Visualizer
    <plugin_config>
      <skin>se.sics.cooja.plugins.skins.GridVisualizerSkin</skin>
      <skin>se.sics.cooja.plugins.skins.IDVisualizerSkin</skin>
      <skin>InterferenceSkin</skin>
      <skin>se.sics.cooja.plugins.skins.PositionVisualizerSkin</skin>
      <viewport>13.820310245790855 0.0 0.0 13.820310245790855 -897.4448767846421 -367.38605164426895</viewport>
    </plugin_config>
    <width>435</width>
    <z>1</z>
    <height>432</height>
    <location_x>946</location_x>
    <location_y>0</location_y>
  </plugin>
  <plugin>
    se.sics.cooja.plugins.LogListener
    <plugin_config>
      <filter />
      <coloring />
    </plugin_config>
    <width>746</width>
    <z>2</z>
    <height>523</height>
    <location_x>11</location_x>
    <location_y>328</location_y>
  </plugin>
  <plugin>
    se.sics.cooja.plugins.TimeLine
    <plugin_config>
      <mote>0</mote>
      <mote>1</mote>
      <mote>2</mote>
      <mote>3</mote>
      <mote>4</mote>
      <showRadioRXTX />
      <split>109</split>
      <zoomfactor>2000.0</zoomfactor>
    </plugin_config>
    <width>1277</width>
    <z>4</z>
    <height>120</height>
    <location_x>3</location_x>
    <location_y>857</location_y>
  </plugin>
  <plugin>
    se.sics.mrm.AreaViewer
    <plugin_config>
      <controls_visible>true</controls_visible>
      <zoom_x>2.115207324543569</zoom_x>
      <zoom_y>2.115207324543569</zoom_y>
      <pan_x>3.1358058333208207</pan_x>
      <pan_y>98.70125897717439</pan_y>
      <show_background>true</show_background>
      <show_obstacles>true</show_obstacles>
      <show_channel>true</show_channel>
      <show_radios>true</show_radios>
      <show_activity>true</show_activity>
      <show_arrow>true</show_arrow>
      <vis_type>signalStrengthButton</vis_type>
      <resolution>100</resolution>
    </plugin_config>
    <width>673</width>
    <z>-1</z>
    <height>632</height>
    <location_x>601</location_x>
    <location_y>329</location_y>
    <minimized>true</minimized>
  </plugin>
  <plugin>
    FrossiCoojaPlugin
    <mote_arg>1</mote_arg>
    <width>187</width>
    <z>3</z>
    <height>75</height>
    <location_x>258</location_x>
    <location_y>1</location_y>
  </plugin>
</simconf>
