#ifndef _CERVER_ADMIN_HANDLER_H_
#define _CERVER_ADMIN_HANDLER_H_

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region handler

#define ADMIN_CERVER_HANDLER_ERROR_MAP(XX)										\
	XX(0,	NONE,			None,				No handler error)				\
	XX(1,	PACKET,			Bad Packet,			Packet check failed)			\
	XX(2,	DROPPED,		Dropped Connection, The connection has been ended)

typedef enum AdminCerverHandlerError {

	#define XX(num, name, string, description) ADMIN_CERVER_HANDLER_ERROR_##name = num,
	ADMIN_CERVER_HANDLER_ERROR_MAP (XX)
	#undef XX

} AdminCerverHandlerError;

CERVER_PUBLIC const char *admin_cerver_handler_error_to_string (
	const AdminCerverHandlerError error
);

CERVER_PUBLIC const char *admin_cerver_handler_error_description (
	const AdminCerverHandlerError error
);

// handles a packet from an admin
// returns 0 if we can / need to handle more packets
// returns 1 if the connection has been ended
// or removed from admin poll
CERVER_PRIVATE u8 admin_packet_handler (
	struct _Packet *packet
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
