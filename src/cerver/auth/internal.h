#ifndef _CERVER_AUTH_INTERNAL_H_
#define _CERVER_AUTH_INTERNAL_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/auth/auth.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

// the methods that the authentication and the connections on hold share, never exported

CERVER_HIDDEN u8 on_hold_connection_remove (const Cerver *cerver, Connection *connection);

CERVER_HIDDEN u8 on_hold_poll_register_connection (Cerver *cerver, Connection *connection);

CERVER_HIDDEN u8 on_hold_poll_unregister_connection (Cerver *cerver, Connection *connection);

CERVER_HIDDEN CerverAuthError auth_try (Packet *packet);

CERVER_HIDDEN CerverAuthError admin_auth_try (Packet *packet);

#ifdef __cplusplus
}
#endif

#endif
