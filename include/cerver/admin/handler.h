/**
 * @file cerver/admin/handler.h
 * @brief The handler of the packets of the admins.
 */

#ifndef _CERVER_ADMIN_HANDLER_H_
#define _CERVER_ADMIN_HANDLER_H_

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup admin_handler Handler
 * @ingroup admin
 * @brief How the admin cerver handles a packet of an admin.
 * @{
 */

/**
 * @brief The X-macro table of the results of the admin handler: the number, the name, the string and the
 * description of each one.
 */
#define ADMIN_CERVER_HANDLER_ERROR_MAP(XX)										\
	XX(0,	NONE,			None,				No handler error)				\
	XX(1,	PACKET,			Bad Packet,			Packet check failed)			\
	XX(2,	DROPPED,		Dropped Connection, The connection has been ended)

/**
 * @brief The result of the admin handler, one `ADMIN_CERVER_HANDLER_ERROR_<name>` for each row of
 * #ADMIN_CERVER_HANDLER_ERROR_MAP.
 */
typedef enum AdminCerverHandlerError {

	#define XX(num, name, string, description) ADMIN_CERVER_HANDLER_ERROR_##name = num,
	ADMIN_CERVER_HANDLER_ERROR_MAP (XX)
	#undef XX

} AdminCerverHandlerError;

/**
 * @brief Returns the string of a result of the admin handler.
 *
 * @param[in] error the result
 * @return the string of its row in #ADMIN_CERVER_HANDLER_ERROR_MAP, or the one of ADMIN_CERVER_HANDLER_ERROR_NONE
 * for a value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *admin_cerver_handler_error_to_string (
	const AdminCerverHandlerError error
);

/**
 * @brief Returns the description of a result of the admin handler.
 *
 * @param[in] error the result
 * @return the description of its row in #ADMIN_CERVER_HANDLER_ERROR_MAP, or the one of
 * ADMIN_CERVER_HANDLER_ERROR_NONE for a value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *admin_cerver_handler_error_description (
	const AdminCerverHandlerError error
);

/**
 * @brief Handles a packet of an admin: it goes to the handlers of the admin cerver by its type, or it is a bad
 * packet.
 *
 * @param[in] packet the packet; the app, app error and custom packets go to their handlers, the others are deleted
 * @retval 0 more packets of the connection can be handled
 * @retval 1 the connection was ended or removed from the admin poll
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 admin_packet_handler (
	struct _Packet *packet
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
