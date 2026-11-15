/**
 * @file cerver/core/serialization.h
 * @brief The summary of a cerver that it sends to its clients, with its schema.
 */

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

/**
 * @defgroup core_serialization Serialization
 * @ingroup core
 * @brief The summary of a cerver, encoded with the serializer.
 * @{
 */

/** @brief The size of the name of a summary, like #CERVER_INFO_NAME_SIZE. */
#define CERVER_SUMMARY_NAME_SIZE			128

/** @brief The size of the welcome message of a summary, like #CERVER_INFO_WELCOME_SIZE. */
#define CERVER_SUMMARY_WELCOME_SIZE			512

/** @brief The bytes of the map, the version, the keys, the lengths and the values that are not texts. */
#define CERVER_SUMMARY_ENCODING_OVERHEAD	64

/** @brief The biggest encoded summary. */
#define CERVER_SUMMARY_MAX_ENCODED_SIZE		\
	(CERVER_SUMMARY_NAME_SIZE + CERVER_SUMMARY_WELCOME_SIZE + CERVER_SUMMARY_ENCODING_OVERHEAD)

/** @brief The version of the schema of the summary. */
#define CERVER_SUMMARY_VERSION				1

/**
 * @brief The X-macro table of the fields of the summary: the info of a cerver that it sends to its clients.
 *
 * SERIALIZER_DECLARE () makes the struct CerverSummary and the schema cerver_summary_schema from it.
 */
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

/**
 * @brief Creates the report of a summary.
 *
 * @param[in] summary the summary, that comes from the network already validated by its schema
 * @return the new report, released with cerver_report_delete (); NULL for a NULL summary or if it could not be
 * allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE CerverReport *cerver_deserialize (
	const CerverSummary *summary
);

/**
 * @brief Creates the info packet of a cerver, with its summary, ready to be sent.
 *
 * @param[in] cerver the cerver
 * @return the new packet, NULL for a NULL cerver or if it could not be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE struct _Packet *cerver_packet_generate (
	Cerver *cerver
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
