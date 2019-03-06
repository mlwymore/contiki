#undef NETSTACK_CONF_WITH_IPV6
#define NETSTACK_CONF_WITH_IPV6 0

#undef NETSTACK_CONF_NETWORK
#define NETSTACK_CONF_NETWORK rime_driver
#undef NETSTACK_CONF_LLSEC
#define NETSTACK_CONF_LLSEC nullsec_driver
#undef NETSTACK_CONF_MAC
#undef NETSTACK_CONF_FRAMER
#undef NETSTACK_CONF_RDC
//#define NETSTACK_CONF_MAC rimac_packetqueue_driver
//#define NETSTACK_CONF_FRAMER framer_rimac
//#define NETSTACK_CONF_RDC rimac_driver
#define NETSTACK_CONF_MAC csma_driver
#define NETSTACK_CONF_FRAMER framer_802154
//#define NETSTACK_CONF_FRAMER contikimac_framer
#define NETSTACK_CONF_RDC lpprdc_driver
//#define NETSTACK_CONF_RDC contikimac_driver

#undef CC2420_CONF_AUTOACK
#define CC2420_CONF_AUTOACK 1


