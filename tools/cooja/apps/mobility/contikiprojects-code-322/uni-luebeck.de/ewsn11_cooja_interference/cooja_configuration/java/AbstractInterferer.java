/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 */

import java.io.File;
import java.util.ArrayList;
import java.util.Collection;

import org.apache.log4j.Logger;
import org.jdom.Element;

import se.sics.cooja.MoteType;
import se.sics.cooja.RadioPacket;
import se.sics.cooja.Simulation;
import se.sics.cooja.interfaces.ApplicationRadio;
import se.sics.cooja.motes.AbstractApplicationMote;
import se.sics.cooja.util.StringUtils;

public abstract class AbstractInterferer extends AbstractApplicationMote {
	private static Logger logger = Logger.getLogger(AbstractInterferer.class);

	private ApplicationRadio radio;

	private int myNoiseCounter = -1;
	private ArrayList<NoiseLevel> myNoiseLevels = null;

	public AbstractInterferer() {
		super();
	}

	public AbstractInterferer(MoteType moteType, Simulation simulation) {
		super(moteType, simulation);

		simulation.invokeSimulationThread(new Runnable() {
			public void run() {
				if (myNoiseLevels == null) {
					loadTraces();
				}
			}
		});
	}

	private void loadTraces() {
		radio = (ApplicationRadio) getInterfaces().getRadio();

		myNoiseLevels = new ArrayList<NoiseLevel>();
		myNoiseCounter = 0;

		/* Read file contents */
		File file = getTraceFile();
		if (!file.exists()) {
			log("# WARNING: Could not read file: " + file.getAbsolutePath());
			return;
		}
		/*log("# Opening " + file.getAbsolutePath() + "...");*/
		String data = StringUtils.loadFromFile(file);
		if (data == null || data.isEmpty()) {
			log("# WARNING: Could not read file contents: " + file.getAbsolutePath());
			return;
		}

		/* Scan for and remove bad serial characters (Sky bug?) */
		if (data.contains("�")) {
			int oldLength = data.length();
			data = data.replace("�", "");
			if (oldLength - data.length() > 0) {
				log("# Removed " + (oldLength - data.length()) + " instances of bad character: �");
			}
		}

		try {
			for (String l: data.split("[\r\n]")) {
				parseNoiseEntry(l);
			}
			log("# Read " + myNoiseLevels.size() + " trace logs from " + file.getAbsolutePath());
		} catch (RuntimeException e) {
			log("# Error when parsing traces, see console for more information");
			logger.warn("Error when parsing traces: " + e.getMessage(), e);
		}
	}

	private void parseNoiseEntry(String l) {
		if (l.isEmpty()) {
			return;
		}
		if (l.startsWith("#")) {
			return;
		}

		String[] arr = l.split("[\t ]");
		if (arr.length != 2) {
			logger.warn("Bad input: " + l);
			return;
		}

		/* Create log entry */
		long duration = Math.round(Double.parseDouble(arr[0])); /* rounded to us */
		int rssi = Integer.parseInt(arr[1]);

		// CARLO: PT = rssi(d) + 46 + 10*path_loss_exp*log10(d/d0)
		double d = 100.00;
		double d0 = 200.00;
		double d_d0_log = Math.log10(d/d0);
		double path_loss_exp = 20.00;
		//System.out.println(d_d0_log);
		double updated_rssi = rssi + 46 + 10 * path_loss_exp * d_d0_log;
		NoiseLevel lo = new NoiseLevel(duration, (int) updated_rssi);
		myNoiseLevels.add(lo);
	}

	public void execute(long time) {
		if (myNoiseLevels == null) {
			loadTraces();
		}
		if (myNoiseLevels == null || myNoiseLevels.size() == 0) {
			log("# WARNING: No noise entries: " + getTraceFile().getAbsolutePath());
			return;
		}

		if (!active) {
			radio.setNoiseLevel(Integer.MIN_VALUE);
			return;
		}

		/* Update noise rssi level */
		NoiseLevel nl = myNoiseLevels.get(myNoiseCounter);
		/*log(myNoiseCounter + ": " + nl.toString());*/
		radio.setNoiseLevel(nl.rssi);

		scheduleNextWakeup(simulation.getSimulationTime() + nl.duration);
		myNoiseCounter++;
		if (myNoiseCounter > myNoiseLevels.size() - 1) {
			myNoiseCounter = 0;
		}
	}

	public String toString() {
		return "Interferer " + getID();
	}

	public void receivedPacket(RadioPacket p) {
	}
	public void sentPacket(RadioPacket p) {
	}

	private boolean active = false;
	public void setActive(boolean active) {
		this.active = active;
		if (!active) {
			radio.setNoiseLevel(Integer.MIN_VALUE);
		} else {
			simulation.invokeSimulationThread(new Runnable() {
				public void run() {
					myNoiseCounter = 0;
					scheduleNextWakeup(simulation.getSimulationTime());
				}
			});
		}
	}
	public boolean isActive() {
		return active;
	}

	class NoiseLevel {
		long duration;
		int rssi;
		public NoiseLevel(long duration, int rssi) {
			this.duration = duration;
			this.rssi = rssi;
		}
		public String toString() {
			return rssi + " for " + duration;
		}
	}

	public abstract File getTraceFile();

	public Collection<Element> getConfigXML() {
		Collection<Element> config = super.getConfigXML();
		if (isActive()) {
			config.add(new Element("interfererActive"));
		}
		return config;
	}
	public boolean setConfigXML(Simulation simulation,
			Collection<Element> configXML, boolean visAvailable) {
		if (!super.setConfigXML(simulation, configXML, visAvailable)) {
			return false;
		}
		for (Element element : configXML) {
			if (element.getName().equals("interfererActive")) {
				setActive(true);
			}
		}

		simulation.invokeSimulationThread(new Runnable() {
			public void run() {
				if (myNoiseLevels == null) {
					loadTraces();
				}
			}
		});

		return true;
	}
}
