#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/events/events.h"
#include "cerver/events/internal.h"

// what an application sets of the event loops of its cerver

const char *cerver_events_backend_to_string (CerverEventsBackend backend) {

	switch (backend) {
		#define XX(num, name, string, description) case CERVER_EVENTS_BACKEND_##name: return #string;
		CERVER_EVENTS_BACKEND_MAP(XX)
		#undef XX
	}

	return cerver_events_backend_to_string (CERVER_EVENTS_BACKEND_AUTO);

}

const char *cerver_events_backend_description (CerverEventsBackend backend) {

	switch (backend) {
		#define XX(num, name, string, description) case CERVER_EVENTS_BACKEND_##name: return #description;
		CERVER_EVENTS_BACKEND_MAP(XX)
		#undef XX
	}

	return cerver_events_backend_description (CERVER_EVENTS_BACKEND_AUTO);

}

static bool cerver_events_backend_is_valid (const CerverEventsBackend backend) {

	switch (backend) {
		#define XX(num, name, string, description) case CERVER_EVENTS_BACKEND_##name: return true;
		CERVER_EVENTS_BACKEND_MAP(XX)
		#undef XX
	}

	return false;

}

void cerver_set_events_backend (
	struct _Cerver *cerver, CerverEventsBackend backend
) {

	if (cerver && cerver_events_backend_is_valid (backend)) {
		cerver->events_backend = (unsigned int) backend;
	}

}

// the loops of a cerver use the same backend: the one of the first tells
CerverEventsBackend cerver_get_events_backend (
	const struct _Cerver *cerver
) {

	CerverEventsBackend backend = CERVER_EVENTS_BACKEND_AUTO;

	if (cerver) {
		const EventLoopGroup *group = cerver->event_loops;

		backend = (group && group->n_loops && group->loops[0] && group->loops[0]->backend) ?
			group->loops[0]->backend->type : (CerverEventsBackend) cerver->events_backend;
	}

	return backend;

}

void cerver_set_io_threads_affinity (
	struct _Cerver *cerver, bool affinity
) {

	if (cerver) cerver->io_threads_affinity = affinity;

}
