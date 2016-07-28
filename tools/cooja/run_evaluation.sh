#!/bin/bash

COOJA_JAR_PATH="/home/mlwymore/contiki/tools/cooja/dist/cooja.jar"
RESULTS_DIRECTORY="/home/mlwymore/results"
SOURCE_DIRECTORY="/home/mlwymore/contiki/examples/blademac/evaluation/source"
SINK_DIRECTORY="/home/mlwymore/contiki/examples/blademac/evaluation/sink"
APP_FILE_PATH="$SOURCE_DIRECTORY/example-broadcast.c"
BLADEMAC_CSC="

if [ ! -d "$RESULTS_DIRECTORY" ]; then
	mkdir results
fi

rm $RESULTS_DIRECTORY/*.txt

NUMRUNS=1

for i in {0..$NUMRUNS..1}; do
	cp
	java -mx512m -jar $COOJA_JAR_PATH -nogui='/home/mlwymore/evaluation.csc' -contiki='/home/mlwymore/contiki' -random-seed=$RANDOM
done
exit 0
