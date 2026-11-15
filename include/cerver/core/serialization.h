#ifndef _CERVER_CORE_SERIALIZATION_H_
#define _CERVER_CORE_SERIALIZATION_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/core/report.h"
#include "cerver/packets/serializer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Packet;

#pragma region serialization

#define CERVER_SUMMARY_NAME_SIZE			128
#define CERVER_SUMMARY_WELCOME_SIZE			512

// the bytes of the map, the version, the keys, the lengths and the values that are not texts
#define CERVER_SUMMARY_ENCODING_OVERHEAD	64

// the biggest encoded cerver summary
#define CERVER_SUMMARY_MAX_ENCODED_SIZE		\
	(CERVER_SUMMARY_NAME_SIZE + CERVER_SUMMARY_WELCOME_SIZE + CERVER_SUMMARY_ENCODING_OVERHEAD)

#define CERVER_SUMMARY_VERSION				1

// the info of a cerver that it sends to its clients
#define CERVER_SUMMARY_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)								\
	ENUM(T, 1, type, SERIALIZER_REQUIRED, CERVER_TYPE_NONE, CERVER_TYPE_NONE, CERVER_TYPE_BALANCER)		\
	STRING(T, 2, TEXT, name, CERVER_SUMMARY_NAME_SIZE, SERIALIZER_REQUIRED)								\
	STRING(T, 3, TEXT, welcome, CERVER_SUMMARY_WELCOME_SIZE, SERIALIZER_OPTIONAL)							\
	SCALAR(T, 4, BOOL, use_ipv6, SERIALIZER_OPTIONAL, false)												\
	ENUM(T, 5, protocol, SERIALIZER_OPTIONAL, PROTOCOL_TCP, PROTOCOL_TCP, PROTOCOL_UDP)					\
	SCALAR(T, 6, U16, port, SERIALIZER_OPTIONAL, 0)														\
	SCALAR(T, 7, BOOL, auth_required, SERIALIZER_OPTIONAL, false)											\
	SCALAR(T, 8, BOOL, uses_sessions, SERIALIZER_OPTIONAL, false)

SERIALIZER_DECLARE (CerverSummary, cerver_summary, CERVER_SUMMARY_FIELDS)

// the summary comes from the network already validated by its schema
CERVER_PRIVATE CerverReport *cerver_deserialize (
	const CerverSummary *summary
);

// creates a cerver info packet ready to be sent
CERVER_PRIVATE struct _Packet *cerver_packet_generate (
	Cerver *cerver
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
