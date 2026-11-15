#ifndef _CERVER_PACKETS_INTERNAL_H_
#define _CERVER_PACKETS_INTERNAL_H_

#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/packets/header.h"
#include "cerver/packets/packets.h"

#ifdef __cplusplus
extern "C" {
#endif

// the protocol of the packets of the process, set with packets_set_protocol_id () and its version
CERVER_HIDDEN ProtocolID protocol_id;
CERVER_HIDDEN ProtocolVersion protocol_version;

// it is zeroed so headers can be compared and copied as a whole
CERVER_HIDDEN void packet_header_init (
	PacketHeader *header,
	const PacketType packet_type,
	const size_t packet_size,
	const u32 req_type
);

#ifdef __cplusplus
}
#endif

#endif
