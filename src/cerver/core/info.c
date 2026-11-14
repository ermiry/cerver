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

#pragma region info

CerverInfo *cerver_info_new (void) {

	CerverInfo *cerver_info = (CerverInfo *) malloc (sizeof (CerverInfo));
	if (cerver_info) {
		(void) memset (cerver_info, 0, sizeof (CerverInfo));

		(void) strncpy (
			cerver_info->alias,
			CERVER_DEFAULT_ALIAS,
			CERVER_INFO_ALIAS_SIZE - 1
		);

		cerver_info->alias_len = strlen (cerver_info->alias);

		cerver_info->cerver_info_packet = NULL;
	}

	return cerver_info;

}

void cerver_info_delete (CerverInfo *cerver_info) {

	if (cerver_info) {
		packet_delete (cerver_info->cerver_info_packet);

		free (cerver_info);
	}

}

void cerver_set_name (Cerver *cerver, const char *name) {

	(void) strncpy (
		cerver->info->name, name, CERVER_INFO_NAME_SIZE - 1
	);

	cerver->info->name_len = strlen (cerver->info->name);

}

// sets the cerver's alias
// to be used primarily to handle cerver's related threads names
// as they must not exceed a certain size
void cerver_set_alias (Cerver *cerver, const char *alias) {

	if (cerver && alias) {
		(void) strncpy (
			cerver->info->alias, alias, CERVER_INFO_ALIAS_SIZE - 1
		);

		cerver->info->alias_len = strlen (cerver->info->alias);
	}

}

// sets the cerver msg to be sent when a client connects
// retuns 0 on success, 1 on error
u8 cerver_set_welcome_msg (Cerver *cerver, const char *msg) {

	u8 retval = 1;

	if (cerver && msg) {
		if (cerver->info) {
			(void) strncpy (
				cerver->info->welcome, msg, CERVER_INFO_WELCOME_SIZE - 1
			);

			cerver->info->welcome_len = strlen (cerver->info->welcome);

			retval = 0;
		}
	}

	return retval;

}

void cerver_update_uptime (Cerver *cerver) {

	if (cerver) {
		cerver->info->uptime = time (NULL) - cerver->info->time_started;
	}

}

// sends the cerver info packet
// retuns 0 on success, 1 on error
u8 cerver_info_send_info_packet (
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

	if (cerver && connection) {
		packet_set_network_values (
			cerver->info->cerver_info_packet,
			cerver,
			client,
			connection,
			NULL
		);

		if (!packet_send (
			cerver->info->cerver_info_packet, 0, NULL, false
		)) {
			retval = 0;
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_PACKET,
				"Failed to send cerver %s info packet!",
				cerver->info->name
			);
		}
	}

	return retval;

}

#pragma endregion
