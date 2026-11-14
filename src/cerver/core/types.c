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

#pragma region global

// initializes global cerver values
// should be called only once at the start of the program
void cerver_init (void) {

	cerver_log_init ();

}

// correctly disposes global values
// should be called only once at the very end of the program
void cerver_end (void) {

	cerver_log_end ();

}

#pragma endregion

#pragma region types

const char *cerver_type_to_string (CerverType type) {

	switch (type) {
		#define XX(num, name, string) case CERVER_TYPE_##name: return #string;
		CERVER_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_type_to_string (CERVER_TYPE_NONE);

}

const char *cerver_handler_type_to_string (CerverHandlerType type) {

	switch (type) {
		#define XX(num, name, string, description) case CERVER_HANDLER_TYPE_##name: return #string;
		CERVER_HANDLER_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_handler_type_to_string (CERVER_HANDLER_TYPE_NONE);

}

const char *cerver_handler_type_description (CerverHandlerType type) {

	switch (type) {
		#define XX(num, name, string, description) case CERVER_HANDLER_TYPE_##name: return #description;
		CERVER_HANDLER_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_handler_type_description (CERVER_HANDLER_TYPE_NONE);

}

const char *cerver_overload_policy_to_string (CerverOverloadPolicy policy) {

	switch (policy) {
		#define XX(num, name, string, description) case CERVER_OVERLOAD_POLICY_##name: return #string;
		CERVER_OVERLOAD_POLICY_MAP(XX)
		#undef XX

		default: break;
	}

	return cerver_overload_policy_to_string (CERVER_OVERLOAD_POLICY_NONE);

}

const char *cerver_overload_policy_description (CerverOverloadPolicy policy) {

	switch (policy) {
		#define XX(num, name, string, description) case CERVER_OVERLOAD_POLICY_##name: return #description;
		CERVER_OVERLOAD_POLICY_MAP(XX)
		#undef XX

		default: break;
	}

	return cerver_overload_policy_description (CERVER_OVERLOAD_POLICY_NONE);

}

const char *cerver_drain_policy_to_string (CerverDrainPolicy policy) {

	switch (policy) {
		#define XX(num, name, string, description) case CERVER_DRAIN_POLICY_##name: return #string;
		CERVER_DRAIN_POLICY_MAP(XX)
		#undef XX

		default: break;
	}

	return cerver_drain_policy_to_string (CERVER_DRAIN_POLICY_NONE);

}

const char *cerver_drain_policy_description (CerverDrainPolicy policy) {

	switch (policy) {
		#define XX(num, name, string, description) case CERVER_DRAIN_POLICY_##name: return #description;
		CERVER_DRAIN_POLICY_MAP(XX)
		#undef XX

		default: break;
	}

	return cerver_drain_policy_description (CERVER_DRAIN_POLICY_NONE);

}

#pragma endregion
