#!/bin/bash

REMOTE_DIRECTORY="dyn20160923"
AUX_DIRECTORY="/home/mlwymore/blademac"
CONTIKI_DIRECTORY="/home/mlwymore/contiki"
COOJA_JAR_PATH="$CONTIKI_DIRECTORY/tools/cooja/dist/cooja.jar"
RESULTS_DIRECTORY="/home/mlwymore/blademac/results"
SOURCE_DIRECTORY="$CONTIKI_DIRECTORY/examples/blademac/evaluation/source"
SINK_DIRECTORY="$CONTIKI_DIRECTORY/examples/blademac/evaluation/sink"
APP_FILE_PATH="/home/mlwymore/blademac/example-broadcast.c"
BLADEMAC_CSC="/home/mlwymore/blademac/blademac_eval.csc"
CCMAC_CSC="/home/mlwymore/blademac/ccmac_eval.csc"
CPCCMAC_CSC="/home/mlwymore/blademac/cpccmac_eval.csc"
POSITIONS_FILE_DIR="$CONTIKI_DIRECTORY/tools/cooja/apps/mobility"
PROJ_CONF_BLADEMAC_SINK="$AUX_DIRECTORY/blademac-sink-project-conf.h"
PROJ_CONF_CCMAC_SINK="$AUX_DIRECTORY/ccmac-sink-project-conf.h"
PROJ_CONF_CPCCMAC_SINK="$AUX_DIRECTORY/cpccmac-sink-project-conf.h"
PROJ_CONF_BLADEMAC_SOURCE="$AUX_DIRECTORY/blademac-source-project-conf.h"
PROJ_CONF_CCMAC_SOURCE="$AUX_DIRECTORY/ccmac-source-project-conf.h"
PROJ_CONF_CPCCMAC_SOURCE="$AUX_DIRECTORY/cpccmac-source-project-conf.h"
BLADEMAC_FILE_PATH="$CONTIKI_DIRECTORY/core/net/mac/ccmac/blademac.c"

if [ ! -d "$RESULTS_DIRECTORY" ]; then
	mkdir -p $RESULTS_DIRECTORY
fi

rm $RESULTS_DIRECTORY/*.txt

sed -i 's/\#define TRACE_ON.*/\#define TRACE_ON 1/' $BLADEMAC_FILE_PATH

NUMRUNS=3

NUMPACKETS=250

TB_MULT=$((250 / 25))
for PROJ_CONF in ${!PROJ_CONF*}; do
	sed -i 's/CCMAC_CONF_INITIAL_TBEACON.*/CCMAC_CONF_INITIAL_TBEACON RTIMER_SECOND\/4/' ${!PROJ_CONF}
done

cd $SINK_DIRECTORY/blademac
make TARGET=sky
cd $SINK_DIRECTORY/ccmac
make TARGET=sky
cd $SINK_DIRECTORY/cpccmac
make TARGET=sky

sed -i 's/\#define NUM_PACKETS_TO_SEND.*/\#define NUM_PACKETS_TO_SEND '$NUMPACKETS'/' $APP_FILE_PATH

DAI=25
sed -i 's/\#define DATA_ARRIVAL_INTERVAL.*/\#define DATA_ARRIVAL_INTERVAL '$DAI'/' $APP_FILE_PATH

TIMEOUT_MS=$((NUMPACKETS * (DAI + 1) * 1000 * 2))
sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $BLADEMAC_CSC
sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CCMAC_CSC
sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CPCCMAC_CSC

cd $SOURCE_DIRECTORY/blademac
make TARGET=sky
cd $SOURCE_DIRECTORY/ccmac
make TARGET=sky
cd $SOURCE_DIRECTORY/cpccmac
make TARGET=sky

RPMS=()
for i in $(seq 0 1); do
#for i in 1; do
	for j in $(seq 0 9); do
		RPMS+=($i'.'$j)
	done
done
RPMS+=(2.0)
for RPM in "${RPMS[@]}"; do
	
	sed -i 's/positions\.dat.*<\/positions>/positions.dat_rpmrange'$RPM'<\/positions>/' $BLADEMAC_CSC
	sed -i 's/positions\.dat.*<\/positions>/positions.dat_rpmrange'$RPM'<\/positions>/' $CCMAC_CSC
	sed -i 's/positions\.dat.*<\/positions>/positions.dat_rpmrange'$RPM'<\/positions>/' $CPCCMAC_CSC

	RPM_INT=${RPM/\./}

	sed -i '0,/<id>/{s/<id>.*/<id>'$RPM_INT'<\/id>/}' $BLADEMAC_CSC
	sed -i '0,/<id>/{s/<id>.*/<id>'$RPM_INT'<\/id>/}' $CCMAC_CSC
	sed -i '0,/<id>/{s/<id>.*/<id>'$RPM_INT'<\/id>/}' $CPCCMAC_CSC

	for ((i=0;i<NUMRUNS;i++)); do
		SEED=$RANDOM
		
		java -Xmx2048m -jar $COOJA_JAR_PATH -nogui=$BLADEMAC_CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
		java -Xmx2048m -jar $COOJA_JAR_PATH -nogui=$CCMAC_CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
		java -Xmx2048m -jar $COOJA_JAR_PATH -nogui=$CPCCMAC_CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
	done
done

find $RESULTS_DIRECTORY -type f -exec curl -1 -v --disable-epsv --ftp-skip-pasv-ip -u mlwymore@iastate.edu:jolly\ jolly\ roger\ 1 -ftp-ssl -T {} ftp://ftp.box.com/Research/Projects/Wind\ Farm\ Monitoring/Paper\ -\ BladeMAC/cooja-results/$REMOTE_DIRECTORY/ --ftp-create-dirs \;

exit 0
