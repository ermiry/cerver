#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#include "cerver/events/internal.h"

#include "cerver/http/core/connection.h"

#include "cerver/threads/thpool.h"

#pragma region thpool

// the queue of the thpool takes the job without waiting; a job that the thpool drops
// (a drain that rejects, a destroy) goes back to its loop instead of running
static unsigned int event_dispatcher_thpool_submit (
	EventDispatcher *dispatcher, struct HttpConnection *connection
) {

	return thpool_add_work_with_reject (
		(Thpool *) dispatcher->data,
		http_connection_work, http_connection_work_dropped,
		connection
	) ? 1 : 0;

}

static bool event_dispatcher_thpool_is_full (const EventDispatcher *dispatcher) {

	return thpool_is_full ((Thpool *) dispatcher->data);

}

void event_dispatcher_thpool (EventDispatcher *dispatcher, Thpool *thpool) {

	if (dispatcher) {
		dispatcher->submit = event_dispatcher_thpool_submit;
		dispatcher->is_full = event_dispatcher_thpool_is_full;
		dispatcher->data = thpool;
		dispatcher->in_loop = false;
	}

}

#pragma endregion

#pragma region inline

static unsigned int event_dispatcher_inline_submit (
	EventDispatcher *dispatcher, struct HttpConnection *connection
) {

	(void) dispatcher;

	http_connection_work (connection);

	return 0;

}

static bool event_dispatcher_inline_is_full (const EventDispatcher *dispatcher) {

	(void) dispatcher;

	return false;

}

void event_dispatcher_inline (EventDispatcher *dispatcher) {

	if (dispatcher) {
		dispatcher->submit = event_dispatcher_inline_submit;
		dispatcher->is_full = event_dispatcher_inline_is_full;
		dispatcher->data = NULL;
		dispatcher->in_loop = true;
	}

}

#pragma endregion
