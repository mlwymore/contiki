/*
 * Copyright (c) 2009, Swedish Institute of Computer Science.
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
 */

package org.contikios.cooja.radiomediums;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Observable;
import java.util.Observer;
import java.util.Random;
import org.apache.commons.math3.special.Erf;
import org.apache.commons.collections4.map.MultiKeyMap;

import org.apache.log4j.Logger;
import org.jdom.Element;

import org.contikios.cooja.ClassDescription;
import org.contikios.cooja.Mote;
import org.contikios.cooja.RadioConnection;
import org.contikios.cooja.SimEventCentral.MoteCountListener;
import org.contikios.cooja.Simulation;
import org.contikios.cooja.interfaces.Position;
import org.contikios.cooja.interfaces.Radio;
//import org.contikios.cooja.plugins.Visualizer;

/**
 *
 * @see #SS_STRONG
 * @see #SS_WEAK
 * @see #SS_NOTHING
 *
 * @see DirectedGraphMedium
 * @author Mat Wymore
 */
@ClassDescription("Lognormal Shadowing Radio Model (LSRM)")
public class LSRM extends AbstractRadioMedium {
  private static Logger logger = Logger.getLogger(LSRM.class);

  public double PATH_LOSS_EXPONENT = 2.0;
  public long FREQUENCY_HZ = 2400000000L;
  public double REFERENCE_DISTANCE = 1.0;
  public double REFERENCE_PATH_LOSS = 0;
  public double SIGMA = 0;
  public double NOISE_FLOOR = -100;

  private Random random = null;

  private MultiKeyMap<Radio, Double> pathLossMap;

  public LSRM(Simulation simulation) {
    super(simulation);
    random = simulation.getRandomGenerator();
    pathLossMap = new MultiKeyMap<Radio, Double>();

    /* Register as position observer.
     * If any positions change, update path losses. */
    final Observer positionObserver = new Observer() {
      public void update(Observable o, Object arg) {
        updatePathLosses();
      }
    };
    /* Re-analyze potential receivers if radios are added/removed. */
    simulation.getEventCentral().addMoteCountListener(new MoteCountListener() {
      public void moteWasAdded(Mote mote) {
        mote.getInterfaces().getPosition().addObserver(positionObserver);
        updatePathLosses();
      }
      public void moteWasRemoved(Mote mote) {
        mote.getInterfaces().getPosition().deleteObserver(positionObserver);
        updatePathLosses();
      }
    });
    for (Mote mote: simulation.getMotes()) {
      mote.getInterfaces().getPosition().addObserver(positionObserver);
    }
    updatePathLosses();

    /* Register visualizer skin */
    //Visualizer.registerVisualizerSkin(LSRMVisualizerSkin.class);
  }

  public void removed() {
    super.removed();
  	
    //Visualizer.unregisterVisualizerSkin(LSRMVisualizerSkin.class);
  }

  public RadioConnection createConnections(Radio sender) {
    RadioConnection newConnection = new RadioConnection(sender);

    /* Fail radio transmission randomly - no radios will hear this transmission */
    if (getTxSuccessProbability(sender) < 1.0 && random.nextDouble() > getTxSuccessProbability(sender)) {
      return newConnection;
    }

    for (Radio recv : getRegisteredRadios()) {
      if (recv == sender) {
        continue;
      }

      /* Fail if radios are on different (but configured) channels */ 
      if (sender.getChannel() >= 0 &&
          recv.getChannel() >= 0 &&
          sender.getChannel() != recv.getChannel()) {

          /* Add the connection in a dormant state;
           it will be activated later when the radio will be
           turned on and switched to the right channel. This behavior
           is consistent with the case when receiver is turned off. */
        newConnection.addInterfered(recv);

        continue;
      }

      double distance = sender.getPosition().getDistanceTo(recv.getPosition());

      if (!recv.isRadioOn()) {
        newConnection.addInterfered(recv);
        recv.interfereAnyReception();
      } else if (recv.isInterfered()) {
        /* Was interfered: keep interfering */
        newConnection.addInterfered(recv);
      } else if (recv.isTransmitting()) {
        newConnection.addInterfered(recv);
      } else if (recv.isReceiving() || (random.nextDouble() > getRxSuccessProbability(sender, recv))) {
        /* Was receiving, or reception failed: start interfering */
        newConnection.addInterfered(recv);
        recv.interfereAnyReception();

        /* Interfere receiver in all other active radio connections */
        for (RadioConnection conn : getActiveConnections()) {
          if (conn.isDestination(recv)) {
            conn.addInterfered(recv);
          }
        }

      } else {
        /* Success: radio starts receiving */
        newConnection.addDestination(recv);
      }
    }

    return newConnection;
  }
  
  public double getSuccessProbability(Radio source, Radio dest) {
  	return getTxSuccessProbability(source) * getRxSuccessProbability(source, dest);
  }

  public double getTxSuccessProbability(Radio source) {
    return 1.0;
  }

  /* Translates distance in meters to path loss in dB using the free space path loss model */
  public double freespacePathLoss(double distance) {
    return 20*Math.log10(distance) + 20*Math.log10(FREQUENCY_HZ) - 147.55;
  }

  public double distanceToPathLoss(double distance) {
    return distanceToPathLoss(distance, SIGMA);
  }

  public double distanceToPathLoss(double distance, double sigma) {
    double refPL = REFERENCE_PATH_LOSS;
    if (refPL == 0) {
      refPL = freespacePathLoss(REFERENCE_DISTANCE);
    }

    double totalPL = refPL + 10*PATH_LOSS_EXPONENT*Math.log10(distance / REFERENCE_DISTANCE);
    totalPL += random.nextGaussian() * sigma;
    return totalPL;
  }

  /* From TOSSIM code (CpmModelC.nc:prr_estimate_from_snr).
     SNR should be in dB. */
  public double snrToPrr(double SNR) {
    double beta1 = 0.9794;
    double beta2 = 2.3851;
    double X = SNR - beta2;
    double PSE = 0.5*Erf.erfc(beta1 * X / Math.sqrt(2));
    double prr_hat = Math.pow(1 - PSE, 23*2);
    if (prr_hat > 1) {
      prr_hat = 1.1;
    }
    else if (prr_hat < 0) {
      prr_hat = -0.1;
    }
    return prr_hat;
  }

  public double getRxSuccessProbability(Radio source, Radio dest) {
    double distance = source.getPosition().getDistanceTo(dest.getPosition());
    double txPower = source.getCurrentOutputPower();
    
    double rxPower = txPower - getPathLoss(source, dest);
    double noise = dest.getCurrentSignalStrength();
    double snr = rxPower - noise;
    return snrToPrr(snr);
  }

  private double getPathLoss(Radio sender, Radio receiver) {
    return pathLossMap.get(sender, receiver);
  }

  private void updatePathLosses() {
    pathLossMap = new MultiKeyMap<Radio, Double>();
    for (Radio sender : getRegisteredRadios()) {
      for (Radio recv : getRegisteredRadios()) {
        if (sender == recv) {
          continue;
        }
        double distance = sender.getPosition().getDistanceTo(recv.getPosition());
        double pathLoss = distanceToPathLoss(distance);
        pathLossMap.put(sender, recv, pathLoss);
      }
    }
  }

  public void updateSignalStrengths() {
    
    /* Reset signal strengths */
    for (Radio radio : getRegisteredRadios()) {
      radio.setCurrentSignalStrength(Math.max(getBaseRssi(radio), NOISE_FLOOR));
    }

    /* Set signal strength to below strong on destinations */
    RadioConnection[] conns = getActiveConnections();
    for (RadioConnection conn : conns) {
      if (conn.getSource().getCurrentSignalStrength() < SS_STRONG) {
        conn.getSource().setCurrentSignalStrength(SS_STRONG);
      }
      ArrayList<Radio> radios = new ArrayList<Radio>();
      radios.addAll(Arrays.asList(conn.getDestinations()));
      radios.addAll(Arrays.asList(conn.getInterfered()));
      for (Radio dstRadio : radios) {
        if (dstRadio == conn.getSource()) {
          continue;
        }
        if (conn.getSource().getChannel() >= 0 &&
            dstRadio.getChannel() >= 0 &&
            conn.getSource().getChannel() != dstRadio.getChannel()) {
          continue;
        }

        double signalStrength = conn.getSource().getCurrentOutputPower() - getPathLoss(conn.getSource(), dstRadio);

        if (dstRadio.getCurrentSignalStrength() < signalStrength) {
          dstRadio.setCurrentSignalStrength(signalStrength);
        }
      }
    }
  }

  public Collection<Element> getConfigXML() {
    Collection<Element> config = super.getConfigXML();
    Element element;

    element = new Element("path_loss_exponent");
    element.setText(Double.toString(PATH_LOSS_EXPONENT));
    config.add(element);

    element = new Element("frequency_hz");
    element.setText(Long.toString(FREQUENCY_HZ));
    config.add(element);

    element = new Element("reference_distance");
    element.setText(Double.toString(REFERENCE_DISTANCE));
    config.add(element);

    element = new Element("reference_path_loss");
    element.setText(Double.toString(REFERENCE_PATH_LOSS));
    config.add(element);

    element = new Element("sigma");
    element.setText(Double.toString(SIGMA));
    config.add(element);

    element = new Element("noise_floor");
    element.setText(Double.toString(NOISE_FLOOR));
    config.add(element);

    return config;
  }

  public boolean setConfigXML(Collection<Element> configXML, boolean visAvailable) {
    super.setConfigXML(configXML, visAvailable);
    for (Element element : configXML) {
      if (element.getName().equals("path_loss_exponent")) {
        PATH_LOSS_EXPONENT = Double.parseDouble(element.getText());
      }

      if (element.getName().equals("frequency_hz")) {
        FREQUENCY_HZ = Long.parseLong(element.getText());
      }

      if (element.getName().equals("reference_distance")) {
        REFERENCE_DISTANCE = Double.parseDouble(element.getText());
      }

      if (element.getName().equals("reference_path_loss")) {
        REFERENCE_PATH_LOSS = Double.parseDouble(element.getText());
      }

      if (element.getName().equals("sigma")) {
        SIGMA = Double.parseDouble(element.getText());
      }

      if (element.getName().equals("noise_floor")) {
        NOISE_FLOOR = Double.parseDouble(element.getText());
      }
    }
    return true;
  }

}
