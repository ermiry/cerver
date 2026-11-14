#ifndef _CERVER_CORE_HANDLER_H_
#define _CERVER_CORE_HANDLER_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region handlers

// prints info about current handlers
CERVER_EXPORT void cerver_handlers_print_info (Cerver *cerver);

// adds a new handler, created with handler_create_with_id (), to the cerver handlers array,
// after cerver_set_multiple_handlers (); the cerver owns it from then on
// returns 0 on success, 1 on error, like an id that is not below n_handlers
// or that already has a handler, and then the handler still belongs to the caller
CERVER_EXPORT u8 cerver_handlers_add (
	Cerver *cerver, struct _Handler *handler
);

// returns the current number of app handlers (multiple handlers option)
CERVER_EXPORT unsigned int cerver_get_n_handlers (Cerver *cerver);

// returns the total number of handlers currently alive (ready to handle packets)
CERVER_EXPORT unsigned int cerver_get_n_handlers_alive (Cerver *cerver);

// returns the total number of handlers currently working (handling a packet)
CERVER_EXPORT unsigned int cerver_get_n_handlers_working (Cerver *cerver);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
