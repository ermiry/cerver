/**
 * @file packets/internal.h
 * @internal
 * @brief The protocol of the process and the methods that the parts of the packets share, never exported by
 * libcerver.so.
 */

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

/** @internal @brief The protocol id of the packets of the process, set with packets_set_protocol_id (). */
CERVER_HIDDEN ProtocolID protocol_id;
/** @internal @brief The protocol version of the packets of the process, set with packets_set_protocol_version (). */
CERVER_HIDDEN ProtocolVersion protocol_version;

/**
 * @internal
 * @brief Initializes a header with its values and the local protocol.
 *
 * It is zeroed first, so headers can be compared and copied as a whole.
 *
 * @param[out] header the header, it must not be NULL
 * @param[in] packet_type the packet type
 * @param[in] packet_size the total size of the packet (header and data)
 * @param[in] req_type the request type
 */
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
