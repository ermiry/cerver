#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"
#include "cerver/collections/pool.h"

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/core/handler.h"
#include "cerver/core/internal.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/network/udp.h"
#include "cerver/packets/packets.h"

#include "cerver/game/game.h"

#include "cerver/http/core/http.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"

#include "cerver/threads/thpool.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region handlers

// prints info about current handlers
void cerver_handlers_print_info (Cerver *cerver) {

	if (cerver) {
		cerver_log_debug (
			"%u - current ACTIVE handlers in cerver %s",
			cerver_get_n_handlers_alive (cerver), cerver->info->name
		);

		cerver_log_debug (
			"%u - current WORKING handlers in cerver %s",
			cerver_get_n_handlers_working (cerver), cerver->info->name
		);
	}

}

// the id of the handler is below n_handlers and nothing has it yet
static bool cerver_handlers_slot_is_free (const Cerver *cerver, const Handler *handler) {

	return cerver->handlers
		&& (handler->id >= 0) && ((unsigned int) handler->id < cerver->n_handlers)
		&& !cerver->handlers[handler->id];

}

// adds a new handler to the cerver handlers array
// in the slot of its id, that must be below n_handlers and free
// returns 0 on success, 1 on error
u8 cerver_handlers_add (Cerver *cerver, Handler *handler) {

	u8 retval = 1;

	if (cerver && handler) {
		if (cerver_handlers_slot_is_free (cerver, handler)) {
			cerver->handlers[handler->id] = handler;

			handler->type = HANDLER_TYPE_CERVER;
			handler->cerver = cerver;

			retval = 0;
		}
	}

	return retval;

}

// returns the current number of app handlers (multiple handlers option)
// it is set before the cerver starts, so it is read without the handlers lock,
// that the cerver only has after its handlers started
unsigned int cerver_get_n_handlers (Cerver *cerver) {

	return cerver ? cerver->n_handlers : 0;

}

// returns the total number of handlers currently alive (ready to handle packets)
unsigned int cerver_get_n_handlers_alive (Cerver *cerver) {

	unsigned int retval = 0;

	// a cerver that never started its handlers does not have the lock
	if (cerver && cerver->handlers_lock) {
		(void) pthread_mutex_lock (cerver->handlers_lock);
		retval = cerver->num_handlers_alive;
		(void) pthread_mutex_unlock (cerver->handlers_lock);
	}

	return retval;

}

// returns the total number of handlers currently working (handling a packet)
unsigned int cerver_get_n_handlers_working (Cerver *cerver) {

	unsigned int retval = 0;

	// the handlers count themselves with __atomic while they handle a packet
	if (cerver) {
		retval = __atomic_load_n (&cerver->num_handlers_working, __ATOMIC_SEQ_CST);
	}

	return retval;

}

// joins the multiple app handlers threads, if any
static void cerver_multiple_app_handlers_destroy (Cerver *cerver) {

	if (cerver->handlers) {
		#ifdef CERVER_DEBUG
		cerver_log_debug (
			"Stopping multiple app handlers in cerver %s...",
			cerver->info->name
		);

		cerver_handlers_print_info (cerver);
		#endif

		for (unsigned int i = 0; i < cerver->n_handlers; i++) {
			handler_stop (cerver->handlers[i]);
		}
	}

}

// 27/05/2020
// correctly destroy cerver's custom handlers,
// each thread is woken and joined, a handler that never started is skipped
u8 cerver_handlers_destroy (Cerver *cerver) {

	if (cerver) {
		#ifdef CERVER_DEBUG
		cerver_log_debug (
			"Stopping handlers in cerver %s...",
			cerver->info->name
		);
		#endif

		if (cerver->multiple_handlers) {
			cerver_multiple_app_handlers_destroy (cerver);
		}

		handler_stop (cerver->app_packet_handler);

		handler_stop (cerver->app_error_packet_handler);

		handler_stop (cerver->custom_packet_handler);

		cerver_request_handlers_stop (cerver);
	}

	return 0;

}

#pragma endregion
