#undef NETSTACK_CONF_NETWORK
#define NETSTACK_CONF_NETWORK rime_driver
#undef NETSTACK_CONF_LLSEC
#define NETSTACK_CONF_LLSEC nullsec_driver
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC csma_driver
#undef NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER contikimac_framer 
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC contikimac_driver
#ifndef CCMAC_CONF_IS_SINK
#define CCMAC_CONF_IS_SINK 0
#endif

