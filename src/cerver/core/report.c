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
#include "cerver/client/events.h"
#include "cerver/connection/connection.h"
#include "cerver/connection/data.h"
#include "cerver/core/cerver.h"
#include "cerver/core/internal.h"
#include "cerver/core/report.h"
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

#pragma region report

CerverReport *cerver_report_new (void) {

	CerverReport *cerver_report = (CerverReport *) malloc (sizeof (CerverReport));
	if (cerver_report) {
		memset (cerver_report, 0, sizeof (CerverReport));

		cerver_report->name = NULL;
	}

	return cerver_report;

}

void cerver_report_delete (void *ptr) {

	if (ptr) {
		CerverReport *cerver_report = (CerverReport *) ptr;

		str_delete (cerver_report->name);

		str_delete (cerver_report->welcome);

		free (cerver_report);
	}

}

static void cerver_report_check_info_handle_auth (
	CerverReport *cerver_report,
	Client *client, Connection *connection
) {

	// an UDP connection sends its auth when it connects, the cerver can not ask for it (udp.c)
	if (cerver_report && connection && (connection->protocol == PROTOCOL_UDP)) {
		return;
	}

	if (cerver_report && connection) {
		if (cerver_report->auth_required) {
			// #ifdef CLIENT_DEBUG
			cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver requires authentication.");
			// #endif
			if (connection->auth_data) {
				#ifdef CLIENT_DEBUG
				cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Sending auth data to cerver...");
				#endif

				if (!connection->auth_packet) {
					if (!connection_generate_auth_packet (connection)) {
						cerver_log_success (
							"cerver_check_info () - Generated connection %s auth packet!",
							connection->name
						);
					}

					else {
						cerver_log_error (
							"cerver_check_info () - Failed to generate connection %s auth packet!",
							connection->name
						);
					}
				}

				if (connection->auth_packet) {
					packet_set_network_values (
						connection->auth_packet,
						NULL,
						NULL,
						connection,
						NULL
					);

					if (!packet_send (connection->auth_packet, 0, NULL, false)) {
						cerver_log_success (
							"cerver_check_info () - Sent connection %s auth packet!",
							connection->name
						);

						client_event_trigger (CLIENT_EVENT_AUTH_SENT, client, connection);
					}

					else {
						cerver_log_error (
							"cerver_check_info () - Failed to send connection %s auth packet!",
							connection->name
						);
					}
				}

				if (cerver_report->uses_sessions) {
					cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver supports sessions.");
				}
			}

			else {
				cerver_log_error (
					"Connection %s does NOT have an auth packet!",
					connection->name
				);
			}
		}

		else {
			#ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_NONE,
				"Cerver does NOT require authentication"
			);
			#endif
		}
	}

}

// the names come from CERVER_TYPE_MAP, so a new type is never logged as unknown
static void cerver_report_log_type (const CerverType type) {

	const char *name = cerver_type_to_string (type);

	if ((type != CERVER_TYPE_NONE) && (name != cerver_type_to_string (CERVER_TYPE_NONE))) {
		cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver type: %s", name);
	}

	else {
		cerver_log (LOG_TYPE_ERROR, LOG_TYPE_NONE, "Cerver type: UNKNOWN");
	}

}

u8 cerver_report_check_info (
	CerverReport *cerver_report,
	Client *client, Connection *connection
) {

	u8 retval = 1;

	if (cerver_report && connection) {
		// the update thread sets it while the app can be ending the connection
		CerverReport *previous = __atomic_exchange_n (
			&connection->cerver_report, cerver_report, __ATOMIC_ACQ_REL
		);

		cerver_report_delete (previous);

		// #ifdef CLIENT_DEBUG
		cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Connected to cerver %s.", cerver_report->name->str);

		if (cerver_report->welcome) {
			cerver_log_msg ("%s", cerver_report->welcome->str);
		}

		switch (cerver_report->protocol) {
			case PROTOCOL_TCP:
				cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver using TCP protocol.");
				break;
			case PROTOCOL_UDP:
				cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver using UDP protocol.");
				break;

			default:
				cerver_log (LOG_TYPE_WARNING, LOG_TYPE_NONE, "Cerver using unknown protocol.");
				break;
		}
		// #endif

		if (cerver_report->use_ipv6) {
			// #ifdef CLIENT_DEBUG
			cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver is configured to use ipv6");
			// #endif
		}

		cerver_report_log_type (cerver_report->type);

		cerver_report_check_info_handle_auth (cerver_report, client, connection);

		retval = 0;
	}

	return retval;

}

#pragma endregion
