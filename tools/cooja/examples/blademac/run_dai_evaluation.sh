#!/bin/bash

REMOTE_DIRECTORY="dai20171005-tx0-3"
AUX_DIRECTORY="/home/user/blademac-tests"
CONTIKI_DIRECTORY="/home/user/git/contiki"
COOJA_JAR_PATH="$CONTIKI_DIRECTORY/tools/cooja/dist/cooja.jar"
RESULTS_DIRECTORY="$AUX_DIRECTORY/results"
SOURCE_DIRECTORY="$CONTIKI_DIRECTORY/examples/blademac/evaluation/source"
SINK_DIRECTORY="$CONTIKI_DIRECTORY/examples/blademac/evaluation/sink"
APP_FILE_PATH="$AUX_DIRECTORY/example-broadcast.c"
PREFIXES=("blademac" "ccmac" "cpccmac")
CSC_POSTFIX="_eval.csc"
SINK_CONF_POSTFIX="-sink-project-conf.h"
SOURCE_CONF_POSTFIX="-source-project-conf.h"
MAC_FILE_PATH="$CONTIKI_DIRECTORY/core/net/mac/ccmac/"

if [ ! -d "$RESULTS_DIRECTORY" ]; then
	mkdir -p $RESULTS_DIRECTORY
fi

rm $RESULTS_DIRECTORY/*.txt

for PREFIX in "${PREFIXES[@]}"; do
	sed -i 's/\#define TRACE_ON.*/\#define TRACE_ON 0/' $MAC_FILE_PATH$PREFIX.c
done

NUMRUNS=5

NUMPACKETS=250

declare -a PROJ_CONFS
INDEX=0
for PREFIX in "${PREFIXES[@]}"; do
	PROJ_CONFS[INDEX]=$AUX_DIRECTORY/$PREFIX$SINK_CONF_POSTFIX
	INDEX=$INDEX+1
	PROJ_CONFS[INDEX]=$AUX_DIRECTORY/$PREFIX$SOURCE_CONF_POSTFIX
	INDEX=$INDEX+1
done

declare -a CSCS
INDEX=0
for PREFIX in "${PREFIXES[@]}"; do
	CSCS[INDEX]=$AUX_DIRECTORY/$PREFIX$CSC_POSTFIX
	INDEX=$INDEX+1
done

#TB_MULT=$((250 / 25))
for PROJ_CONF in "${PROJ_CONFS[@]}"; do
	sed -i 's/CCMAC_CONF_INITIAL_TBEACON.*/CCMAC_CONF_INITIAL_TBEACON RTIMER_SECOND\/4/' $PROJ_CONF
done

cd $SINK_DIRECTORY/blademac
make TARGET=sky
cd $SINK_DIRECTORY/ccmac
make TARGET=sky
cd $SINK_DIRECTORY/cpccmac
make TARGET=sky

sed -i 's/\#define NUM_PACKETS_TO_SEND.*/\#define NUM_PACKETS_TO_SEND '$NUMPACKETS'/' $APP_FILE_PATH

RPM=12.1
for CSC in "${CSCS[@]}"; do
	sed -i 's/<positions EXPORT.*/<positions EXPORT="copy">\/home\/user\/git\/contiki\/tools\/cooja\/apps\/mobility\/position-dats\/positions.dat_'$RPM'<\/positions>/' $CSC
done

#DAIS=(1)
DAIS=(1 5 10 25 50 75 100)
DAIS=(25 26 27 28 29 30)
DAIS=(1 7 14 28 56 84 112)
for DAI in "${DAIS[@]}"; do
	sed -i 's/\#define DATA_ARRIVAL_INTERVAL.*/\#define DATA_ARRIVAL_INTERVAL '$DAI'/' $APP_FILE_PATH
	TIMEOUT_MS=$((NUMPACKETS * (DAI + 1) * 1000 * 2))
	for CSC in "${CSCS[@]}"; do
		sed -i '0,/<id>/{s/<id>.*/<id>'$DAI'<\/id>/}' $CSC
		sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CSC
	done

	cd $SOURCE_DIRECTORY/blademac
	make TARGET=sky
	cd $SOURCE_DIRECTORY/ccmac
	make TARGET=sky
	cd $SOURCE_DIRECTORY/cpccmac
	make TARGET=sky

	for ((i=0;i<NUMRUNS;i++)); do
		SEED=$RANDOM
		for CSC in "${CSCS[@]}"; do
			java -mx1024m -jar $COOJA_JAR_PATH -nogui=$CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
		done
	done
done

find $RESULTS_DIRECTORY -type f -exec curl -1 -v --disable-epsv --ftp-skip-pasv-ip -u mlwymore@iastate.edu:jolly\ jolly\ roger\ 1 -ftp-ssl -T {} ftps://ftp.box.com/Research/Projects/Wind\ Farm\ Monitoring/Paper\ -\ BladeMAC/cooja-results/$REMOTE_DIRECTORY/ --ftp-create-dirs \;

exit 0
