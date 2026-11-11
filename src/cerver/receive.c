#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/socket.h>

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/errors.h"
#include "cerver/handler.h"
#include "cerver/packets.h"
#include "cerver/receive.h"
#include "cerver/socket.h"

#include "cerver/game/lobby.h"

#include "cerver/utils/log.h"

static const char *receive_bad_packet_size = { "The packet is bigger than the maximum size" };

#define RECEIVE_BAD_HEADER_MESSAGE_SIZE			128
#define RECEIVE_BAD_HEADER_MESSAGE				"%s, the cerver expects the header format %d"

const char *receive_error_to_string (
	const ReceiveError error
) {

	switch (error) {
		#define XX(num, name, string) case RECEIVE_ERROR_##name: return #string;
		RECEIVE_ERROR_MAP(XX)
		#undef XX
	}

	return receive_error_to_string (RECEIVE_ERROR_NONE);

}

const char *receive_type_to_string (
	const ReceiveType type
) {

	switch (type) {
		#define XX(num, name, string) case RECEIVE_TYPE_##name: return #string;
		RECEIVE_TYPE_MAP(XX)
		#undef XX
	}

	return receive_type_to_string (RECEIVE_TYPE_NONE);

}

const char *receive_handle_state_to_string (
	const ReceiveHandleState state
) {

	switch (state) {
		#define XX(num, name, string) case RECEIVE_HANDLE_STATE_##name: return #string;
		RECEIVE_HANDLE_STATE_MAP(XX)
		#undef XX
	}

	return receive_handle_state_to_string (RECEIVE_HANDLE_STATE_NONE);

}

ReceiveHandle *receive_handle_new (void) {

	ReceiveHandle *receive_handle =
		(ReceiveHandle *) malloc (sizeof (ReceiveHandle));

	if (receive_handle) {
		receive_handle->type = RECEIVE_TYPE_NONE;

		receive_handle->cerver = NULL;

		receive_handle->socket = NULL;
		receive_handle->connection = NULL;
		receive_handle->client = NULL;
		receive_handle->admin = NULL;

		receive_handle->lobby = NULL;

		receive_handle->buffer = NULL;
		receive_handle->buffer_size = 0;

		receive_handle->state = RECEIVE_HANDLE_STATE_NONE;

		(void) memset (receive_handle->header, 0, PACKET_HEADER_SIZE);
		receive_handle->header_end = NULL;
		receive_handle->remaining_header = 0;

		receive_handle->spare_packet = NULL;

		receive_handle->trailing = NULL;
		receive_handle->trailing_size = 0;
		receive_handle->trailing_taken = 0;
	}

	return receive_handle;

}

void receive_handle_delete (void *receive_ptr) {

	if (receive_ptr) free (receive_ptr);

}

#pragma region buffer

// the bytes of the current buffer that have not been handled yet
typedef struct ReceiveCursor {

	const char *data;
	size_t remaining;

	size_t max_packet_size;
	u8 (*dispatch) (ReceiveHandle *receive_handle, Packet *packet);

} ReceiveCursor;

static void receive_cursor_advance (ReceiveCursor *cursor, const size_t length) {

	cursor->data += length;
	cursor->remaining -= length;

}

static int receive_handle_sock_fd (const ReceiveHandle *receive_handle) {

	int sock_fd = -1;

	if (receive_handle->socket) sock_fd = receive_handle->socket->sock_fd;
	else if (receive_handle->connection && receive_handle->connection->socket) {
		sock_fd = receive_handle->connection->socket->sock_fd;
	}

	return sock_fd;

}

// a packet with an invalid size or header means that the stream is corrupted,
// or that the peer speaks another protocol, so the rest of the connection
// can not be parsed and it is closed: recv () returns 0 and the receive loop drops it as usual
// it is a bad packet, the cerver tells its sender why
static void receive_handle_lost_report (
	ReceiveHandle *receive_handle, const char *message
) {

	if (receive_handle->connection) receive_handle->connection->bad_packets += 1;

	if (receive_handle->cerver && receive_handle->connection) {
		CERVER_STATS_ADD (receive_handle->cerver->stats->received_packets->n_bad_packets, 1);

		(void) error_packet_generate_and_send (
			CERVER_ERROR_PACKET_ERROR, message,
			receive_handle->cerver, receive_handle->client, receive_handle->connection
		);
	}

	else if (receive_handle->client) {
		CERVER_STATS_ADD (receive_handle->client->stats->received_packets->n_bad_packets, 1);
	}

}

static u8 receive_handle_close (ReceiveHandle *receive_handle, const int sock_fd) {

	receive_handle->state = RECEIVE_HANDLE_STATE_LOST;

	if (sock_fd > -1) (void) shutdown (sock_fd, SHUT_RDWR);

	return 1;

}

static u8 receive_handle_lost (
	ReceiveHandle *receive_handle, const size_t packet_size
) {

	receive_handle_lost_report (receive_handle, receive_bad_packet_size);

	const int sock_fd = receive_handle_sock_fd (receive_handle);

	cerver_log_with_date (
		LOG_TYPE_WARNING, LOG_TYPE_PACKET,
		"receive_handle_buffer () - invalid packet size %zu from sock fd <%d> - closing the connection",
		packet_size, sock_fd
	);

	return receive_handle_close (receive_handle, sock_fd);

}

// a header with another format, unknown flags or reserved bytes closes the connection,
// the error tells the peer the format that the cerver expects
static u8 receive_handle_bad_header (
	ReceiveHandle *receive_handle, const PacketHeaderResult result
) {

	char message[RECEIVE_BAD_HEADER_MESSAGE_SIZE] = { 0 };
	(void) snprintf (
		message, RECEIVE_BAD_HEADER_MESSAGE_SIZE,
		RECEIVE_BAD_HEADER_MESSAGE,
		packet_header_result_string (result), PACKET_HEADER_FORMAT
	);

	receive_handle_lost_report (receive_handle, message);

	const int sock_fd = receive_handle_sock_fd (receive_handle);

	cerver_log_with_date (
		LOG_TYPE_WARNING, LOG_TYPE_PACKET,
		"receive_handle_buffer () - %s from sock fd <%d> - closing the connection",
		packet_header_result_string (result), sock_fd
	);

	return receive_handle_close (receive_handle, sock_fd);

}

// the bytes after the packet are offered to its handler,
// the ones that it takes are skipped when it returns
static u8 receive_handle_dispatch (
	ReceiveHandle *receive_handle, ReceiveCursor *cursor, Packet *packet
) {

	const bool trailing = (cursor->remaining > 0);

	receive_handle->trailing = trailing ? cursor->data : NULL;
	receive_handle->trailing_size = cursor->remaining;
	receive_handle->trailing_taken = 0;

	const u8 stop = cursor->dispatch (receive_handle, packet);

	// a handler may have deleted the connection, so its receive handle is only used
	// when the buffer has more bytes and the handler did not stop, like before the trailing bytes
	if (!stop && trailing) {
		receive_cursor_advance (cursor, receive_handle->trailing_taken);

		receive_handle->trailing = NULL;
		receive_handle->trailing_size = 0;
		receive_handle->trailing_taken = 0;
	}

	return stop;

}

size_t receive_handle_take_trailing (
	ReceiveHandle *receive_handle,
	const char **data, const size_t max_size
) {

	size_t length = 0;

	if (receive_handle && data && receive_handle->trailing) {
		const size_t available = receive_handle->trailing_size - receive_handle->trailing_taken;
		length = (available < max_size) ? available : max_size;

		*data = receive_handle->trailing + receive_handle->trailing_taken;
		receive_handle->trailing_taken += length;
	}

	return length;

}

static bool receive_packet_size_is_valid (
	const ReceiveCursor *cursor, const u64 packet_size
) {

	return (
		(packet_size >= PACKET_HEADER_SIZE)
		&& (packet_size <= (u64) cursor->max_packet_size)
	);

}

// adds the bytes of the cursor that belong to the spare packet
// and dispatches it when it is complete
// returns 0 to keep handling the buffer, 1 to stop
// (a handler may have deleted the connection, so its receive handle is never used after it)
static u8 receive_handle_packet_data (
	ReceiveHandle *receive_handle, ReceiveCursor *cursor
) {

	u8 stop = 0;

	Packet *packet = receive_handle->spare_packet;

	const size_t length = (packet->remaining_data < cursor->remaining) ?
		packet->remaining_data : cursor->remaining;

	if (length && packet_add_data (packet, cursor->data, length)) {
		receive_handle->spare_packet = NULL;
		packet_delete (packet);

		stop = receive_handle_lost (receive_handle, length);
	}

	else {
		receive_cursor_advance (cursor, length);

		if (!packet->remaining_data) {
			receive_handle->spare_packet = NULL;
			receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;

			stop = receive_handle_dispatch (receive_handle, cursor, packet);
		}

		else {
			receive_handle->state = RECEIVE_HANDLE_STATE_SPLIT_PACKET;
		}
	}

	return stop;

}

static void receive_packet_set_values (
	const ReceiveHandle *receive_handle, Packet *packet,
	const PacketHeader *header
) {

	(void) memcpy (&packet->header, header, sizeof (PacketHeader));

	// the version of the sender, that packet_check () validates
	packet->version = header->version;

	packet->cerver = receive_handle->cerver;
	packet->client = receive_handle->client;
	packet->connection = receive_handle->connection;

	// the packet of a player keeps a reference to the lobby of its connection
	packet->lobby = NULL;
	lobby_packet_attach (packet);

	packet->packet_size = packet->header.packet_size;

}

// creates the packet of a complete header
// a packet without data is dispatched right away
static u8 receive_handle_packet_start (
	ReceiveHandle *receive_handle, const PacketHeader *header,
	ReceiveCursor *cursor
) {

	u8 stop = 0;

	const u64 packet_size = header->packet_size;

	Packet *packet = receive_packet_size_is_valid (cursor, packet_size) ?
		packet_create_with_data ((size_t) packet_size - PACKET_HEADER_SIZE) : NULL;

	if (packet) {
		receive_packet_set_values (receive_handle, packet, header);

		if (packet->data_size == 0) {
			receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;
			stop = receive_handle_dispatch (receive_handle, cursor, packet);
		}

		else {
			receive_handle->spare_packet = packet;
			stop = receive_handle_packet_data (receive_handle, cursor);
		}
	}

	else {
		stop = receive_handle_lost (receive_handle, (size_t) packet_size);
	}

	return stop;

}

// decodes a complete header and starts its packet
static u8 receive_handle_encoded_header (
	ReceiveHandle *receive_handle, const u8 *encoded,
	ReceiveCursor *cursor
) {

	PacketHeader header;
	const PacketHeaderResult result = packet_header_decode (encoded, &header);

	return (result == PACKET_HEADER_RESULT_NONE)
		? receive_handle_packet_start (receive_handle, &header, cursor)
		: receive_handle_bad_header (receive_handle, result);

}

// a new packet starts at the cursor
// the header can be split between many buffers
static u8 receive_handle_header (
	ReceiveHandle *receive_handle, ReceiveCursor *cursor
) {

	u8 stop = 0;

	if (cursor->remaining >= PACKET_HEADER_SIZE) {
		const u8 *encoded = (const u8 *) cursor->data;
		receive_cursor_advance (cursor, PACKET_HEADER_SIZE);

		stop = receive_handle_encoded_header (receive_handle, encoded, cursor);
	}

	else {
		(void) memset (receive_handle->header, 0, PACKET_HEADER_SIZE);
		(void) memcpy (receive_handle->header, cursor->data, cursor->remaining);

		receive_handle->remaining_header = (unsigned int) (PACKET_HEADER_SIZE - cursor->remaining);
		receive_handle->state = RECEIVE_HANDLE_STATE_SPLIT_HEADER;

		receive_cursor_advance (cursor, cursor->remaining);
	}

	return stop;

}

// completes a header that was split between buffers,
// even if the new buffer is shorter than the missing bytes
static u8 receive_handle_split_header (
	ReceiveHandle *receive_handle, ReceiveCursor *cursor
) {

	u8 stop = 0;

	const size_t received_header = PACKET_HEADER_SIZE - receive_handle->remaining_header;
	const size_t length = (receive_handle->remaining_header < cursor->remaining) ?
		receive_handle->remaining_header : cursor->remaining;

	(void) memcpy (receive_handle->header + received_header, cursor->data, length);
	receive_cursor_advance (cursor, length);

	receive_handle->remaining_header -= (unsigned int) length;

	if (!receive_handle->remaining_header) {
		u8 encoded[PACKET_HEADER_SIZE];
		(void) memcpy (encoded, receive_handle->header, PACKET_HEADER_SIZE);

		receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;

		stop = receive_handle_encoded_header (receive_handle, encoded, cursor);
	}

	return stop;

}

// assembles the packets of the received buffer, no matter where the buffers are split
// every complete packet is passed to dispatch, that returns 0 to keep handling the buffer,
// or 1 to stop (when the connection was deleted, its receive handle is not used anymore)
// a packet with an invalid size closes the connection
void receive_handle_buffer (
	ReceiveHandle *receive_handle, const size_t max_packet_size,
	u8 (*dispatch) (ReceiveHandle *receive_handle, Packet *packet)
) {

	if (receive_handle && dispatch) {
		ReceiveCursor cursor = {
			receive_handle->buffer, receive_handle->received_size,
			max_packet_size, dispatch
		};

		u8 stop = 0;
		while (!stop && cursor.remaining) {
			switch (receive_handle->state) {
				case RECEIVE_HANDLE_STATE_SPLIT_HEADER:
					stop = receive_handle_split_header (receive_handle, &cursor);
					break;

				case RECEIVE_HANDLE_STATE_SPLIT_PACKET:
					stop = receive_handle_packet_data (receive_handle, &cursor);
					break;

				// the connection is being closed, its remaining data is discarded
				case RECEIVE_HANDLE_STATE_LOST:
					stop = 1;
					break;

				default:
					stop = receive_handle_header (receive_handle, &cursor);
					break;
			}
		}
	}

}

// releases the packet that was being assembled
void receive_handle_end (ReceiveHandle *receive_handle) {

	if (receive_handle) {
		packet_delete (receive_handle->spare_packet);
		receive_handle->spare_packet = NULL;

		receive_handle->state = RECEIVE_HANDLE_STATE_NONE;
	}

}

#pragma endregion
