#!/bin/bash

CONTIKI_DIRECTORY="/home/mlwymore/contiki"
COOJA_JAR_PATH="$CONTIKI_DIRECTORY/tools/cooja/dist/cooja.jar"
RESULTS_DIRECTORY="/home/mlwymore/blademac/results"
SOURCE_DIRECTORY="$CONTIKI_DIRECTORY/examples/blademac/evaluation/source"
SINK_DIRECTORY="$CONTIKI_DIRECTORY/examples/blademac/evaluation/sink"
APP_FILE_PATH="$SOURCE_DIRECTORY/example-broadcast.c"
BLADEMAC_CSC="/home/mlwymore/blademac/blademac_eval.csc"
CCMAC_CSC="/home/mlwymore/blademac/ccmac_eval.csc"
CPCCMAC_CSC="/home/mlwymore/blademac/cpccmac_eval.csc"

if [ ! -d "$RESULTS_DIRECTORY" ]; then
	mkdir -p $RESULTS_DIRECTORY
fi

rm $RESULTS_DIRECTORY/*.txt

NUMRUNS=3
#TIMEOUT_MS=3660000

#sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $BLADEMAC_CSC
#sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CCMAC_CSC
#sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CPCCMAC_CSC

NUMPACKETS=50

sed -i 's/\#define NUM_PACKETS_TO_SEND.*/\#define NUM_PACKETS_TO_SEND '$NUMPACKETS'/' $APP_FILE_PATH

DAIS=(100)
for DAI in "${DAIS[@]}"; do
	sed -i 's/\#define DATA_ARRIVAL_INTERVAL.*/\#define DATA_ARRIVAL_INTERVAL '$DAI'/' $APP_FILE_PATH
	sed -i '0,/<id>/{s/<id>.*/<id>'$DAI'<\/id>/}' $BLADEMAC_CSC
	sed -i '0,/<id>/{s/<id>.*/<id>'$DAI'<\/id>/}' $CCMAC_CSC
	sed -i '0,/<id>/{s/<id>.*/<id>'$DAI'<\/id>/}' $CPCCMAC_CSC

	TIMEOUT_MS=$((NUMPACKETS * DAI * 1000 * 3))
	sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $BLADEMAC_CSC
	sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CCMAC_CSC
	sed -i 's/^TIMEOUT.*/TIMEOUT('$TIMEOUT_MS');/' $CPCCMAC_CSC

	cd $SOURCE_DIRECTORY/blademac
	make TARGET=sky
	cd $SOURCE_DIRECTORY/ccmac
	make TARGET=sky
	cd $SOURCE_DIRECTORY/cpccmac
	make TARGET=sky

	for ((i=0;i<NUMRUNS;i++)); do
		SEED=$RANDOM
		
		java -mx512m -jar $COOJA_JAR_PATH -nogui=$BLADEMAC_CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
		#java -mx512m -jar $COOJA_JAR_PATH -nogui=$CCMAC_CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
		#java -mx512m -jar $COOJA_JAR_PATH -nogui=$CPCCMAC_CSC -contiki=$CONTIKI_DIRECTORY -random-seed=$SEED
	done
done

exit 0
