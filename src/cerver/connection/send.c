#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/admin/admin.h"
#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/internal.h"
#include "cerver/connection/send.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/handler/receive.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region send

// pushes a packet to the connection's send queue,
// the send thread sends it and deletes it
void connection_send_packet (
	Connection *connection, Packet *packet
) {

	if (connection && connection->send_queue && packet) {
		// the job comes from the pool of the queue
		Job *job = job_get (connection->send_queue);
		if (job) {
			job->args = packet;

			if (job_queue_push (connection->send_queue, job)) {
				job_return (connection->send_queue, job);
				packet_delete (packet);
			}
		}

		else {
			packet_delete (packet);
		}
	}

	// the packet belongs to the connection, a connection without a send queue deletes it
	else {
		packet_delete (packet);
	}

}

// sends a queued packet, returns 0 on success, 1 on error
static u8 connection_send_thread_send (Connection *connection, Job *job) {

	Packet *packet = (Packet *) job->args;

	struct timespec start = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &start);

	size_t sent = 0;
	const u8 failed = packet_send_actual (
		packet,
		connection->send_flags, &sent,
		connection->client, connection
	);

	packet_delete (packet);

	job_queue_record_run (connection->send_queue, &start);
	job_return (connection->send_queue, job);

	return failed;

}

// the send thread was asked to end, read with the lock of the send queue
static bool connection_send_thread_stopped (const void *connection_ptr) {

	const Connection *connection = (const Connection *) connection_ptr;

	return !__atomic_load_n (&connection->send_thread_running, __ATOMIC_SEQ_CST);

}

void *connection_send_thread (void *connection_ptr) {

	Connection *connection = (Connection *) connection_ptr;

	#ifdef CONNECTION_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
		"Connection %s connection_send () thread has started",
		connection->name
	);
	#endif

	// a failed send does not end the thread, the connection can be connected again
	while (!connection_send_thread_stopped (connection)) {
		Job *job = (Job *) job_queue_pull_interruptible (
			connection->send_queue, connection_send_thread_stopped, connection
		);

		if (job) (void) connection_send_thread_send (connection, job);
	}

	#ifdef CONNECTION_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
		"Connection %s connection_send () thread has ended",
		connection->name
	);
	#endif

	return NULL;

}

#pragma endregion
