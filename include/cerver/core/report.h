/**
 * @file cerver/core/report.h
 * @brief What a client learns of a cerver when it connects to it.
 */

#ifndef _CERVER_CORE_REPORT_H_
#define _CERVER_CORE_REPORT_H_

#include <stdbool.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/types.h"
#include "cerver/network/network.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Client;
struct _Connection;

/**
 * @defgroup core_report Report
 * @ingroup core
 * @brief The info that a client gets from a cerver when it connects to it.
 * @{
 */

/**
 * @brief The info of a cerver that a client got when it connected to it.
 */
struct _CerverReport {

	/** @brief The type of the cerver. */
	CerverType type;

	/** @brief The name of the cerver. */
	String *name;
	/** @brief The welcome message of the cerver, NULL if it does not have one. */
	String *welcome;

	/** @brief The cerver uses IPv6. */
	bool use_ipv6;
	/** @brief The protocol of the cerver. */
	Protocol protocol;
	/** @brief The port of the cerver. */
	u16 port;

	/** @brief The cerver requires authentication. */
	bool auth_required;
	/** @brief The cerver uses sessions. */
	bool uses_sessions;

};

/** @brief The info of a cerver, see struct _CerverReport. */
typedef struct _CerverReport CerverReport;

/**
 * @brief Releases a report.
 *
 * @param[in] ptr the report, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_report_delete (void *ptr);

/**
 * @brief Keeps the report of the cerver of a connection, logs it and handles its authentication.
 *
 * @param[in] cerver_report the report, the connection owns it from now on and releases the one it had
 * @param[in] client the client of the connection
 * @param[in,out] connection the connection
 * @retval 0 the report was handled
 * @retval 1 a NULL report or connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_report_check_info (
	CerverReport *cerver_report,
	struct _Client *client, struct _Connection *connection
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
