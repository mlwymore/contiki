CONTIKI = ../../../..
APPS+=powertrace
all: example-broadcast

CONTIKI_WITH_RIME = 1
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\" -DDATA_ARRIVAL_INTERVAL=1
include $(CONTIKI)/Makefile.include
