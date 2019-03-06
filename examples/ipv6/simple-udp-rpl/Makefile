all: broadcast-example unicast-sender unicast-receiver
APPS=servreg-hack
CONTIKI=../../..

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

ifeq ($(MAKE_WITH_NON_STORING),1)
CFLAGS += -DWITH_NON_STORING=1
endif

CONTIKI_WITH_IPV6 = 1
include $(CONTIKI)/Makefile.include
