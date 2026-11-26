/**
 * @file cerver/core/info.h
 * @brief The info of a cerver: its name, its alias, its welcome message and its uptime.
 */

#ifndef _CERVER_CORE_INFO_H_
#define _CERVER_CORE_INFO_H_

#include <stddef.h>
#include <time.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;
struct _Packet;

/**
 * @defgroup core_info Info
 * @ingroup core
 * @brief The info of a cerver, and the packet that sends it to its clients.
 * @{
 */

/** @brief The size of the name of a cerver, with its terminator. */
#define CERVER_INFO_NAME_SIZE				128

/** @brief The size of the alias of a cerver, with its terminator. */
#define CERVER_INFO_ALIAS_SIZE				32

/** @brief The size of the welcome message of a cerver, with its terminator. */
#define CERVER_INFO_WELCOME_SIZE			512

/**
 * @brief The info of a cerver.
 */
typedef struct CerverInfo {

	/** @brief The length of the name. */
	size_t name_len;
	/** @brief The name of the cerver. */
	char name[CERVER_INFO_NAME_SIZE];

	/** @brief The length of the alias. */
	size_t alias_len;
	/** @brief The alias of the cerver, that names its threads. */
	char alias[CERVER_INFO_ALIAS_SIZE];

	/** @brief The length of the welcome message. */
	size_t welcome_len;
	/** @brief The message that is sent to a client when it first connects. */
	char welcome[CERVER_INFO_WELCOME_SIZE];
	/** @brief The packet with the summary of the cerver that its clients get. */
	struct _Packet *cerver_info_packet;

	/** @brief When the cerver was started. */
	time_t time_started;
	/**
	 * @brief The seconds the cerver had been up when cerver_update_uptime () last wrote them.
	 * @warning Nothing in cerver updates it: the seconds that a running cerver has been up are
	 * time (NULL) - time_started.
	 */
	u64 uptime;

} CerverInfo;

/**
 * @brief Sets the alias of the cerver, used mainly to name its threads, as their names have a short limit.
 *
 * @param[in,out] cerver the cerver
 * @param[in] alias the alias, truncated to CERVER_INFO_ALIAS_SIZE - 1 characters; NULL keeps the current one
 */
CERVER_EXPORT void cerver_set_alias (
	struct _Cerver *cerver, const char *alias
);

/**
 * @brief Sets the message that is sent to a client when it connects.
 *
 * @param[in,out] cerver the cerver
 * @param[in] msg the message, truncated to CERVER_INFO_WELCOME_SIZE - 1 characters
 * @retval 0 the message was set
 * @retval 1 a NULL argument or a cerver without its info
 */
CERVER_EXPORT u8 cerver_set_welcome_msg (
	struct _Cerver *cerver, const char *msg
);

/**
 * @brief Updates the seconds that the cerver has been up.
 *
 * @param[in,out] cerver the cerver
 * @note For the internal methods of cerver.
 * @deprecated Nothing in cerver calls it: writing the shared CerverInfo::uptime from several threads was a race,
 * each reader computes time (NULL) - CerverInfo::time_started.
 */
DEPRECATED (CERVER_PRIVATE void cerver_update_uptime (
	struct _Cerver *cerver
));

/**
 * @brief Sends the info packet of the cerver to a connection.
 *
 * @param[in] cerver the cerver
 * @param[in] client the client of the connection
 * @param[in] connection the connection
 * @retval 0 the packet was sent
 * @retval 1 a NULL cerver or connection, or the packet was not sent
 */
CERVER_PUBLIC u8 cerver_info_send_info_packet (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
