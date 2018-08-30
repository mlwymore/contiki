#!/bin/bash

#REMOTE_DIRECTORY="dai20160928"
AUX_DIRECTORY="/home/user/git/contiki/examples/lpprdc/clique-unicast"
CONTIKI_DIRECTORY="/home/user/git/contiki"
COOJA_JAR_PATH="$CONTIKI_DIRECTORY/tools/cooja/dist/cooja.jar"
RESULTS_DIRECTORY="$AUX_DIRECTORY/cooja-results"
SOURCE_DIRECTORY="$AUX_DIRECTORY"
SINK_DIRECTORY="$AUX_DIRECTORY"
SOURCE_APP="example-unicast"
SINK_APP="example-unicast"
SOURCE_FILE_PATH="$AUX_DIRECTORY/example-unicast.c"
CSC_LIST=("$AUX_DIRECTORY/unicast-example-z1-1.csc" "$AUX_DIRECTORY/unicast-example-z1-2.csc" "$AUX_DIRECTORY/unicast-example-z1-3.csc" "$AUX_DIRECTORY/unicast-example-z1-4.csc")
#CSC_LIST=("$AUX_DIRECTORY/unicast-example-z1-2.csc")
PROJ_CONF_DIR=$AUX_DIRECTORY
PROJ_CONF_LIST=("project-conf.h")
#PROTOCOL_LIST=("lpprdc" "rimac" "contikimac" "contikimac" "contikimac" "contikimac")
#OPP_CONF_LIST=(0 0 0 0 0 0)
#CCR_LIST=(2 2 2 4 8 16)
#TBI_LIST=(125 125 125 125 125 125)
PROTOCOL_LIST=("lpprdc" "lpprdc" "lpprdc" "lpprdc" "lpprdc" "rimac" "contikimac" "contikimac" "contikimac" "contikimac")
CCR_LIST=(2 2 2 2 2 2 2 4 8 16)
OPP_CONF_LIST=(0 0 0 0 0 0 0 0 0 0)
TBI_LIST=(25 50 75 100 125 125 125 125 125 125)
DAI_LIST=(16)
MAKEFILE="$AUX_DIRECTORY/Makefile"

if [ ! -d "$RESULTS_DIRECTORY" ]; then
	mkdir -p $RESULTS_DIRECTORY
fi

#rm $RESULTS_DIRECTORY/*.txt

NUMRUNS=3

BOOTTIME=10

#Simulation time in seconds - we'll send as many packets as possible. Not including boot time.
RUNLENGTH=300

#CHANNEL_CHECK_RATES=(2 4 8 16)
TX_RANGES=(100)
INTERFERENCE_RANGES=(120)
for DAI in "${DAI_LIST[@]}"; do
	for JNDEX in "${!TX_RANGES[@]}"; do
	TX_RANGE=${TX_RANGES[$JNDEX]}
	INTERFERENCE_RANGE=${INTERFERENCE_RANGES[$JNDEX]}

		if [ "$DAI" == "0.5" ]; then
			NUMPACKETS=$((RUNLENGTH * 2))
		else
			NUMPACKETS=$((RUNLENGTH / DAI))
		fi
		sed -i 's/#define NUM_PACKETS_TO_SEND.*/#define NUM_PACKETS_TO_SEND '$NUMPACKETS'/' $SOURCE_FILE_PATH
		sed -i 's/#define DATA_INTERVAL.*/#define DATA_INTERVAL '$DAI'/' $SOURCE_FILE_PATH
		if [ "$DAI" == "0.5" ]; then
			TIMEOUT_MS=$(((BOOTTIME + 1 + (NUMPACKETS + 1) * (1)) * 1000))
		else
			TIMEOUT_MS=$(((BOOTTIME + DAI + (NUMPACKETS + 1) * (DAI + 1)) * 1000))
		fi

		for CSC in "${CSC_LIST[@]}"; do
			sed -i 's/"-dai.*/"-dai'$DAI'" +/' $CSC
			sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS', log.append(filename, plugin.radioStatistics());)/' $CSC
			sed -i 's/<transmitting_range>.*/<transmitting_range>'$TX_RANGE'<\/transmitting_range>/' $CSC
			sed -i 's/<interference_range>.*/<interference_range>'$INTERFERENCE_RANGE'<\/interference_range>/' $CSC
			sed -i 's/"-tx.*/"-tx'$TX_RANGE'" +/' $CSC
		done
	for CSC in "${CSC_LIST[@]}"; do
			NUM_FLOWS=$(echo $CSC | cut -f '5' -d '-' | cut -f '1' -d '.')
			echo $NUM_FLOWS
			sed -i 's/numFlows = .*/numFlows = '$NUM_FLOWS';/' $CSC
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
				TBI=${TBI_LIST[$INDEX]}
		    for PROJ_CONF in "${PROJ_CONF_LIST[@]}"; do
			  	sed -i 's/#define NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE.*/#define NETSTACK_CONF_RDC_CHANNEL_CHECK_RATE '$CHANNEL_CHECK_RATE'/' $PROJ_CONF_DIR/$PROJ_CONF
			  	sed -i 's/#define LPPRDC_CONF_INITIAL_PROBE_SIZE.*/#define LPPRDC_CONF_INITIAL_PROBE_SIZE '$TBI'/' $PROJ_CONF_DIR/$PROJ_CONF
		    done
			          
			  sed -i 's/"-ccr.*/"-ccr'$CHANNEL_CHECK_RATE'" +/' $CSC
				sed -i 's/"-rdc.*/"-rdc'$PROTOCOL'" +/' $CSC
				sed -i 's/"-tbi.*/"-tbi'$TBI'-" +/' $CSC
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
