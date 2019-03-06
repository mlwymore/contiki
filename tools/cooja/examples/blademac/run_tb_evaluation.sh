#!/bin/bash

REMOTE_DIRECTORY="tb20160914"
CONTIKI_DIRECTORY="/home/mlwymore/contiki"
AUX_DIRECTORY="/home/mlwymore/blademac"
COOJA_JAR_PATH="$CONTIKI_DIRECTORY/tools/cooja/dist/cooja.jar"
RESULTS_DIRECTORY="$AUX_DIRECTORY/results"
SOURCE_DIRECTORY="$CONTIKI_DIRECTORY/examples/blademac/evaluation/source"
SINK_DIRECTORY="$CONTIKI_DIRECTORY/examples/blademac/evaluation/sink"
APP_FILE_PATH="$AUX_DIRECTORY/example-broadcast.c"
BLADEMAC_CSC="$AUX_DIRECTORY/blademac_eval.csc"
CCMAC_CSC="$AUX_DIRECTORY/ccmac_eval.csc"
CPCCMAC_CSC="$AUX_DIRECTORY/cpccmac_eval.csc"
PROJ_CONF_BLADEMAC_SINK="$AUX_DIRECTORY/blademac-sink-project-conf.h"
PROJ_CONF_CCMAC_SINK="$AUX_DIRECTORY/ccmac-sink-project-conf.h"
PROJ_CONF_CPCCMAC_SINK="$AUX_DIRECTORY/cpccmac-sink-project-conf.h"
PROJ_CONF_BLADEMAC_SOURCE="$AUX_DIRECTORY/blademac-source-project-conf.h"
PROJ_CONF_CCMAC_SOURCE="$AUX_DIRECTORY/ccmac-source-project-conf.h"
PROJ_CONF_CPCCMAC_SOURCE="$AUX_DIRECTORY/cpccmac-source-project-conf.h"

if [ ! -d "$RESULTS_DIRECTORY" ]; then
	mkdir -p $RESULTS_DIRECTORY
fi

rm $RESULTS_DIRECTORY/*.txt

NUMRUNS=3

NUMPACKETS=50

sed -i 's/\#define NUM_PACKETS_TO_SEND.*/\#define NUM_PACKETS_TO_SEND '$NUMPACKETS'/' $APP_FILE_PATH

RPM=10.5
sed -i 's/positions\.dat.*<\/positions>/positions.dat_'$RPM'<\/positions>/' $BLADEMAC_CSC
sed -i 's/positions\.dat.*<\/positions>/positions.dat_'$RPM'<\/positions>/' $CCMAC_CSC
sed -i 's/positions\.dat.*<\/positions>/positions.dat_'$RPM'<\/positions>/' $CPCCMAC_CSC

DAI=25
TIMEOUT_MS=$((NUMPACKETS * (DAI + 1) * 1000 * 2))
sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $BLADEMAC_CSC
sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CCMAC_CSC
sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CPCCMAC_CSC
sed -i 's/\#define DATA_ARRIVAL_INTERVAL.*/\#define DATA_ARRIVAL_INTERVAL '$DAI'/' $APP_FILE_PATH

TBS=(100 175 250 325 400 475)
for TB in "${TBS[@]}"; do
	TB_MULT=$((TB * 32768 / 1000))
	echo $TB_MULT

	sed -i '0,/<id>/{s/<id>.*/<id>'$TB'<\/id>/}' $BLADEMAC_CSC
	sed -i '0,/<id>/{s/<id>.*/<id>'$TB'<\/id>/}' $CCMAC_CSC
	sed -i '0,/<id>/{s/<id>.*/<id>'$TB'<\/id>/}' $CPCCMAC_CSC

	for PROJ_CONF in ${!PROJ_CONF*}; do
		sed -i 's/CCMAC_CONF_INITIAL_TBEACON.*/CCMAC_CONF_INITIAL_TBEACON '$TB_MULT'/' ${!PROJ_CONF}
	done

	cd $SOURCE_DIRECTORY/blademac
	make TARGET=sky
	cd $SOURCE_DIRECTORY/ccmac
	make TARGET=sky
	cd $SOURCE_DIRECTORY/cpccmac
	make TARGET=sky

	cd $SINK_DIRECTORY/blademac
	make TARGET=sky
	cd $SINK_DIRECTORY/ccmac
	make TARGET=sky
	cd $SINK_DIRECTORY/cpccmac
	make TARGET=sky

	for ((i=0;i<NUMRUNS;i++)); do
		SEED=$RANDOM
		
		java -mx1024m -jar $COOJA_JAR_PATH -nogui=$BLADEMAC_CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
		java -mx1024m -jar $COOJA_JAR_PATH -nogui=$CCMAC_CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
		java -mx1024m -jar $COOJA_JAR_PATH -nogui=$CPCCMAC_CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
	done
done

find $RESULTS_DIRECTORY -type f -exec curl -1 -v --disable-epsv --ftp-skip-pasv-ip -u mlwymore@iastate.edu:jolly\ jolly\ roger\ 1 -ftp-ssl -T {} ftp://ftp.box.com/Research/Projects/Wind\ Farm\ Monitoring/Paper\ -\ BladeMAC/cooja-results/$REMOTE_DIRECTORY/ --ftp-create-dirs \;

exit 0
