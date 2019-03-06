#!/bin/bash

#REMOTE_DIRECTORY="dai20160928"
AUX_DIRECTORY="/home/user/git/contiki/examples/ipv6/simple-udp-rpl"
CONTIKI_DIRECTORY="/home/user/git/contiki"
COOJA_JAR_PATH="$CONTIKI_DIRECTORY/tools/cooja/dist/cooja.jar"
RESULTS_DIRECTORY="$AUX_DIRECTORY/cooja-results"
SOURCE_DIRECTORY="$AUX_DIRECTORY"
SINK_DIRECTORY="$AUX_DIRECTORY"
SOURCE_APP="unicast-sender"
SINK_APP="unicast-receiver"
SOURCE_FILE_PATH="$AUX_DIRECTORY/unicast-sender.c"
CSC_LIST=("$AUX_DIRECTORY/unicast-example-z1-2.csc")
PROJ_CONF_DIR=$AUX_DIRECTORY
PROJ_CONF_LIST=("project-conf.h")
PROTOCOL_LIST=("lpprdc" "rimac" "contikimac")
OPP_CONF_LIST=(0 0 0)
CCR_LIST=(2 2 2)
MAKEFILE="$AUX_DIRECTORY/Makefile"

if [ ! -d "$RESULTS_DIRECTORY" ]; then
	mkdir -p $RESULTS_DIRECTORY
fi

rm $RESULTS_DIRECTORY/*.txt

NUMRUNS=1

BOOTTIME=60

#Simulation time in seconds - we'll send as many packets as possible. Not including boot time.
RUNLENGTH=650

#CHANNEL_CHECK_RATES=(2 4 8 16)
TX_RANGES=(100)
INTERFERENCE_RANGES=(120)
DAIS=(1)
for JNDEX in "${!TX_RANGES[@]}"; do
TX_RANGE=${TX_RANGES[$JNDEX]}
INTERFERENCE_RANGE=${INTERFERENCE_RANGES[$JNDEX]}
sed -i 's/<transmitting_range>.*/<transmitting_range>'$TX_RANGE'<\/transmitting_range>/' ${CSC_LIST[0]}
sed -i 's/<interference_range>.*/<interference_range>'$INTERFERENCE_RANGE'<\/interference_range>/' ${CSC_LIST[0]}
sed -i 's/"-tx.*/"-tx'$TX_RANGE'" +/' ${CSC_LIST[0]}
for DAI in "${DAIS[@]}"; do
	NUMPACKETS=$((RUNLENGTH / DAI))
	sed -i 's/#define NUM_PACKETS_TO_SEND.*/#define NUM_PACKETS_TO_SEND '$NUMPACKETS'/' $SOURCE_FILE_PATH
#	sed -i 's/#define PERIOD.*/#define PERIOD '$DAI'/' $SOURCE_FILE_PATH
	TIMEOUT_MS=$(((BOOTTIME + DAI + (NUMPACKETS + 2) * (DAI + 1)) * 1000))
        for CSC in "${CSC_LIST[@]}"; do
		sed -i 's/"-dai.*/"-dai'$DAI'" +/' $CSC
		sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CSC
	done

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
                  for CSC in "${CSC_LIST[@]}"; do
	            sed -i 's/"-ccr.*/"-ccr'$CHANNEL_CHECK_RATE'-" +/' $CSC
                  done
			sed -i 's/"-rdc.*/"-rdc'$PROTOCOL'" +/' ${CSC_LIST[0]}
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
                  	for CSC in "${CSC_LIST[@]}"; do
	          		sed -i 's/"-opp.*/"-opp'$OPP_CONF'" +/' $CSC
                  	done
			#cd $SOURCE_DIRECTORY
			#make clean TARGET=z1
			#make $SOURCE_APP TARGET=z1
			cd $SINK_DIRECTORY
			make clean TARGET=z1
			make $SINK_APP TARGET=z1
			java -mx1024m -jar $COOJA_JAR_PATH -nogui=$CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
		done
	done
done
done

#find $RESULTS_DIRECTORY -type f -exec curl -1 -v --disable-epsv --ftp-skip-pasv-ip -u user@place.net:pass\ word -ftp-ssl -T {} ftp://ftp.box.com/path/to/$REMOTE_DIRECTORY/ --ftp-create-dirs \;

exit 0
