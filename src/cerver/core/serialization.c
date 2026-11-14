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
#include "cerver/core/report.h"
#include "cerver/core/serialization.h"
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

#pragma region serialization

SERIALIZER_DEFINE (CerverSummary, cerver_summary, CERVER_SUMMARY_VERSION, CERVER_SUMMARY_FIELDS)

// the summary of the cerver that its clients get, the texts are cut to their fields
static void cerver_summary_set (CerverSummary *summary, const Cerver *cerver) {

	serializer_object_init (&cerver_summary_schema, summary);

	summary->type = (i32) cerver->type;

	(void) snprintf (
		summary->name, CERVER_SUMMARY_NAME_SIZE,
		"%s", cerver->info->name
	);

	if (cerver->info->welcome_len) {
		(void) snprintf (
			summary->welcome, CERVER_SUMMARY_WELCOME_SIZE,
			"%s", cerver->info->welcome
		);
	}

	summary->use_ipv6 = cerver->use_ipv6;
	summary->protocol = (i32) cerver->protocol;
	summary->port = cerver->port;

	summary->auth_required = cerver->auth_required;
	summary->uses_sessions = cerver->use_sessions;

}

// the summary comes from the network already validated by its schema
CerverReport *cerver_deserialize (const CerverSummary *summary) {

	CerverReport *cerver_report = NULL;

	if (summary) {
		cerver_report = cerver_report_new ();
		if (cerver_report) {
			cerver_report->type = (CerverType) summary->type;

			cerver_report->name = str_new (summary->name);

			if (summary->welcome[0]) {
				cerver_report->welcome = str_new (summary->welcome);
			}

			cerver_report->use_ipv6 = summary->use_ipv6;
			cerver_report->protocol = (Protocol) summary->protocol;
			cerver_report->port = summary->port;

			cerver_report->auth_required = summary->auth_required;
			cerver_report->uses_sessions = summary->uses_sessions;
		}
	}

	return cerver_report;

}

// creates a cerver info packet ready to be sent
Packet *cerver_packet_generate (Cerver *cerver) {

	Packet *packet = NULL;

	if (cerver) {
		CerverSummary summary;
		cerver_summary_set (&summary, cerver);

		packet = packet_create_serialized (
			PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_INFO,
			&cerver_summary_schema, &summary
		);
	}

	return packet;

}

#pragma endregion
