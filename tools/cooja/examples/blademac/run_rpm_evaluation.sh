#!/bin/bash

REMOTE_DIRECTORY="rpm20171004-tx9-dai30-nocos"
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

NUMRUNS=1

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



DAI=30
sed -i 's/\#define DATA_ARRIVAL_INTERVAL.*/\#define DATA_ARRIVAL_INTERVAL '$DAI'/' $APP_FILE_PATH

cd $SOURCE_DIRECTORY/blademac
make TARGET=sky
cd $SOURCE_DIRECTORY/ccmac
make TARGET=sky
cd $SOURCE_DIRECTORY/cpccmac
make TARGET=sky

TIMEOUT_MS=$((NUMPACKETS * (DAI + 1) * 1000 * 2))
for CSC in "${CSCS[@]}"; do
	sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CSC
done

RPMS=()
for i in $(seq 11 12); do
	for j in 0 1 2 3 4 5 6 7 8 9; do
		RPMS+=($i'.'$j)
	done
done
RPMS+=(13.1)

for RPM in "${RPMS[@]}"; do
	RPM_INT=${RPM/\./}
	
	for CSC in "${CSCS[@]}"; do
		sed -i '0,/<id>/{s/<id>.*/<id>'$RPM_INT'<\/id>/}' $CSC
		sed -i 's/<positions EXPORT.*/<positions EXPORT="copy">\/home\/user\/git\/contiki\/tools\/cooja\/apps\/mobility\/position-dats\/positions.dat_'$RPM'<\/positions>/' $CSC
	done

	for ((i=0;i<NUMRUNS;i++)); do
		SEED=$RANDOM
		for CSC in "${CSCS[@]}"; do
			java -mx1024m -jar $COOJA_JAR_PATH -nogui=$CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
		done
	done
done

find $RESULTS_DIRECTORY -type f -exec curl -1 -v --disable-epsv --ftp-skip-pasv-ip -u mlwymore@iastate.edu:jolly\ jolly\ roger\ 1 -ftp-ssl -T {} ftps://ftp.box.com/Research/Projects/Wind\ Farm\ Monitoring/Paper\ -\ BladeMAC/cooja-results/$REMOTE_DIRECTORY/ --ftp-create-dirs \;

exit 0
