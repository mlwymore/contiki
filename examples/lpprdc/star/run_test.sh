#!/bin/bash

#REMOTE_DIRECTORY="dai20160928"
AUX_DIRECTORY="/home/user/git/contiki/examples/lpprdc/star"
CONTIKI_DIRECTORY="/home/user/git/contiki"
COOJA_JAR_PATH="$CONTIKI_DIRECTORY/tools/cooja/dist/cooja.jar"
RESULTS_DIRECTORY="$AUX_DIRECTORY/cooja-results"
SOURCE_DIRECTORY="$AUX_DIRECTORY"
SINK_DIRECTORY="$AUX_DIRECTORY"
SOURCE_APP="example-unicast"
SINK_APP="example-unicast"
SOURCE_FILE_PATH="$AUX_DIRECTORY/example-unicast.c"
CSC_LIST=("$AUX_DIRECTORY/unicast-example-z1-1.csc" "$AUX_DIRECTORY/unicast-example-z1-2.csc" "$AUX_DIRECTORY/unicast-example-z1-3.csc" "$AUX_DIRECTORY/unicast-example-z1-4.csc" "$AUX_DIRECTORY/unicast-example-z1-5.csc" "$AUX_DIRECTORY/unicast-example-z1-6.csc")
#CSC_LIST=("$AUX_DIRECTORY/unicast-example-z1-1.csc")
PROJ_CONF_DIR=$AUX_DIRECTORY
PROJ_CONF_LIST=("project-conf.h")
PROTOCOL_LIST=("lpprdc" "rimac" "contikimac" "contikimac")
OPP_CONF_LIST=(0 0 0 0)
CCR_LIST=(2 2 2 16)
MAKEFILE="$AUX_DIRECTORY/Makefile"

DRIFT_FACTOR_LIST=(1) 

if [ ! -d "$RESULTS_DIRECTORY" ]; then
	mkdir -p $RESULTS_DIRECTORY
fi

rm $RESULTS_DIRECTORY/*.txt

NUMRUNS=5

BOOTTIME=10

#Simulation time in seconds - we'll send as many packets as possible. Not including boot time.
RUNLENGTH=300

#CHANNEL_CHECK_RATES=(2 4 8 16)
TX_RANGES=(100)
INTERFERENCE_RANGES=(120)
DAI=1
for JNDEX in "${!TX_RANGES[@]}"; do
TX_RANGE=${TX_RANGES[$JNDEX]}
INTERFERENCE_RANGE=${INTERFERENCE_RANGES[$JNDEX]}

	NUMPACKETS=$((RUNLENGTH / DAI))
	sed -i 's/#define NUM_PACKETS_TO_SEND.*/#define NUM_PACKETS_TO_SEND '$NUMPACKETS'/' $SOURCE_FILE_PATH
#	sed -i 's/#define PERIOD.*/#define PERIOD '$DAI'/' $SOURCE_FILE_PATH
	TIMEOUT_MS=$(((BOOTTIME + (NUMPACKETS + 2) * (DAI)) * 1000))
  for CSC in "${CSC_LIST[@]}"; do
		sed -i 's/"-dai.*/"-dai'$DAI'" +/' $CSC
		sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS', log.append(filename, plugin.radioStatistics());)/' $CSC
		sed -i 's/<transmitting_range>.*/<transmitting_range>'$TX_RANGE'<\/transmitting_range>/' $CSC
		sed -i 's/<interference_range>.*/<interference_range>'$INTERFERENCE_RANGE'<\/interference_range>/' $CSC
		sed -i 's/"-tx.*/"-tx'$TX_RANGE'" +/' $CSC
	done
for DF in "${DRIFT_FACTOR_LIST[@]}"; do
	sed -i 's/$define DRIFT_FACTOR.*/#define DRIFT_FACTOR '$DRIFT_FACTOR'/' ${PROJ_CONF_LIST[0]}
	for CSC in "${CSC_LIST[@]}"; do
		sed -i 's/"-df.*/"-df'$DF'" +/' $CSC
	
		NUM_SENDERS=$(echo $CSC | cut -f '4' -d '-' | cut -f '1' -d '.')
		echo $NUM_SENDERS
		sed -i 's/#define NUM_SENDERS.*/#define NUM_SENDERS '$NUM_SENDERS'/' $SOURCE_FILE_PATH
		sed -i 's/numSenders = .*/numSenders = '$NUM_SENDERS';/' $CSC
		sed -i 's/numPackets = .*/numPackets = '$NUMPACKETS';/' $CSC

		for ((i=0;i<NUMRUNS;i++)); do
			SEED=$RANDOM


			
			#for INDEX in "${!PROTOCOL_LIST[@]}"; do
			#	sed -i 's/-DPROJECT_CONF_H.*/-DPROJECT_CONF_H=\\"'${PROJ_CONF_LIST[$INDEX]}'\\"/' $MAKEFILE
			#	sed -i 's/"-rdc.*/"-rdc'${PROTOCOL_LIST[$INDEX]}'-" +/' ${CSC_LIST[0]}
			for INDEX in "${!PROTOCOL_LIST[@]}"; do
				PROTOCOL=${PROTOCOL_LIST[$INDEX]}
				OPP_CONF=${OPP_CONF_LIST[$INDEX]}
				CHANNEL_CHECK_RATE=${CCR_LIST[$INDEX]}
		                for PROJ_CONF in "${PROJ_CONF_LIST[@]}"; do
			          sed -i 's/#define NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE.*/#define NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE '$CHANNEL_CHECK_RATE'/' $PROJ_CONF_DIR/$PROJ_CONF
		                done
		                #for CSC in "${CSC_LIST[@]}"; do
			          sed -i 's/"-ccr.*/"-ccr'$CHANNEL_CHECK_RATE'-" +/' $CSC
		                #done
				sed -i 's/"-rdc.*/"-rdc'$PROTOCOL'" +/' $CSC
				sed -i 's/#define NETSTACK_CONF_RDC .*/#define NETSTACK_CONF_RDC '$PROTOCOL'_driver/' $PROJ_CONF_DIR/${PROJ_CONF_LIST[0]}
				if [ "$PROTOCOL" == "contikimac" ]; then
					sed -i 's/#define NETSTACK_CONF_FRAMER .*/#define NETSTACK_CONF_FRAMER '$PROTOCOL'_framer/' $PROJ_CONF_DIR/${PROJ_CONF_LIST[0]}
					sed -i 's/#define WITH_SOFTACKS.*/#define WITH_SOFTACKS 0/' $PROJ_CONF_DIR/${PROJ_CONF_LIST[0]}
				else
					sed -i 's/#define NETSTACK_CONF_FRAMER .*/#define NETSTACK_CONF_FRAMER framer_802154/' $PROJ_CONF_DIR/${PROJ_CONF_LIST[0]}
					sed -i 's/#define WITH_SOFTACKS.*/#define WITH_SOFTACKS 1/' $PROJ_CONF_DIR/${PROJ_CONF_LIST[0]}
				fi
				if [ $OPP_CONF == 1 ]; then
					sed -i 's/#define RPL_CONF_OPP_ROUTING .*/#define RPL_CONF_OPP_ROUTING 1/' $PROJ_CONF_DIR/${PROJ_CONF_LIST[0]}
					sed -i 's/#define RPL_CONF_OF_OCP .*/#define RPL_CONF_OF_OCP RPL_OCP_EEP/' $PROJ_CONF_DIR/${PROJ_CONF_LIST[0]}
				else
					sed -i 's/#define RPL_CONF_OPP_ROUTING .*/#define RPL_CONF_OPP_ROUTING 0/' $PROJ_CONF_DIR/${PROJ_CONF_LIST[0]}
					sed -i 's/#define RPL_CONF_OF_OCP .*/#define RPL_CONF_OF_OCP RPL_OCP_MRHOF/' $PROJ_CONF_DIR/${PROJ_CONF_LIST[0]}
				fi
		                	#for CSC in "${CSC_LIST[@]}"; do
			        		sed -i 's/"-opp.*/"-opp'$OPP_CONF'" +/' $CSC
		                	#done
				#cd $SOURCE_DIRECTORY
				#make clean TARGET=z1
				#make $SOURCE_APP TARGET=z1
				#cd $SINK_DIRECTORY
				#make clean TARGET=z1
				#make $SINK_APP TARGET=z1
				java -mx1024m -jar $COOJA_JAR_PATH -nogui=$CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
			done
		done
	done
done
done

#find $RESULTS_DIRECTORY -type f -exec curl -1 -v --disable-epsv --ftp-skip-pasv-ip -u user@place.net:pass\ word -ftp-ssl -T {} ftp://ftp.box.com/path/to/$REMOTE_DIRECTORY/ --ftp-create-dirs \;

exit 0
