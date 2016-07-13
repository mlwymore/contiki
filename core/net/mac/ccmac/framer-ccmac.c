/*
 * Copyright (c) 2009, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/**
 * \file
 *         MAC framer for ccmac
 * \author
 *         Mat Wymore <mlwymore@iastate.edu>
 */

#include "net/mac/ccmac/framer-ccmac.h"
#include "net/mac/framer-802154.h"
#include "net/packetbuf.h"

#define DEBUG 0

#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINTADDR(addr) PRINTF(" %02x%02x:%02x%02x:%02x%02x:%02x%02x ", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7])
#else
#define PRINTF(...)
#define PRINTADDR(addr)
#endif

#define DECORATED_FRAMER framer_802154

struct ccmac_hdr {
  uint8_t packet_type;
};

/*---------------------------------------------------------------------------*/
static int
hdr_length(void)
{
  return DECORATED_FRAMER.length() + sizeof(struct ccmac_hdr);
}
/*---------------------------------------------------------------------------*/
static int
create(void)
{
  struct ccmac_hdr *hdr;
  int deco_hdr_len;

  if(packetbuf_hdralloc(sizeof(struct ccmac_hdr))) {
    hdr = packetbuf_hdrptr();
    hdr->packet_type = packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE);
    deco_hdr_len = DECORATED_FRAMER.create();
    if (deco_hdr_len < 0) {
      PRINTF("framer-ccmac: decorated framer failed\n");
      return FRAMER_FAILED;
    }
    packetbuf_compact();
    return deco_hdr_len + sizeof(struct ccmac_hdr);
  }
  PRINTF("framer-ccmac: too large header: %u\n", sizeof(struct ccmac_hdr));
  return FRAMER_FAILED;
}
/*---------------------------------------------------------------------------*/
static int
parse(void)
{
  struct ccmac_hdr *hdr;
  int deco_hdr_len;

  deco_hdr_len = DECORATED_FRAMER.parse();
  if (deco_hdr_len < 0) {
    PRINTF("framer-ccmac: decorated parse failed\n");
    return FRAMER_FAILED;
  }

  hdr = packetbuf_dataptr();
  if(packetbuf_hdrreduce(sizeof(struct ccmac_hdr))) {
    packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, hdr->packet_type);
    PRINTF("framer-ccmac: ");
    PRINTF(" Type %d ", packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE));
    PRINTF("%u (%u)\n", packetbuf_datalen(), sizeof(struct ccmac_hdr));

    return deco_hdr_len + sizeof(struct ccmac_hdr);
  }
  PRINTF("framer-ccmac: parse failed\n");
  return FRAMER_FAILED;
}
/*---------------------------------------------------------------------------*/
const struct framer framer_ccmac = {
  hdr_length,
  create,
  parse
};
