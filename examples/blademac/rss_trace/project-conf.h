#undef CCMAC_CONF_IS_SINK
#define CCMAC_CONF_IS_SINK 0

#undef NETSTACK_CONF_WITH_IPV6
#define NETSTACK_CONF_WITH_IPV6 0

#undef NETSTACK_CONF_NETWORK
#define NETSTACK_CONF_NETWORK rime_driver
#undef NETSTACK_CONF_LLSEC
#define NETSTACK_CONF_LLSEC nullsec_driver
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC nullmac_driver
#undef NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER framer_802154
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC nullrdc_driver

//#undef CC2420_CONF_AUTOACK
//#define CC2420_CONF_AUTOACK 0


