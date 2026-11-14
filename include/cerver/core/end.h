#ifndef _CERVER_CORE_END_H_
#define _CERVER_CORE_END_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region end

// stops accepting new connections and wakes up cerver_start ()
// it is safe to call it from another thread, from a signal handler
// and more than once, an interrupted accept () or poll ()
// (a signal without SA_RESTART) also stops the cerver
// the connections in progress are drained by cerver_teardown ()
CERVER_PRIVATE void cerver_stop_request (Cerver *cerver);

// stops accepting new connections and wakes up cerver_start ()
// it can be called after the cerver was stopped by a signal
// returns 0 on success, 1 on error
CERVER_EXPORT u8 cerver_shutdown (Cerver *cerver);

// sleeps the seconds or until the cerver is stopped
CERVER_PRIVATE void cerver_sleep (Cerver *cerver, u32 seconds);

// sleeps the microseconds or until the cerver is stopped, used to limit the fps of an update
CERVER_PRIVATE void cerver_sleep_microseconds (Cerver *cerver, i64 microseconds);

// teardown a server -> stop the server, drain the connections
// using the drain policy and timeout, and clean all of its data
// if some connections did not finish before the drain timeout,
// the data they use is not released, to avoid using it after a free
// returns 0 on success, 1 on error
CERVER_EXPORT u8 cerver_teardown (Cerver *cerver);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
