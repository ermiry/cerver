#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>

#include "cerver/connection.h"
#include "cerver/handler.h"
#include "cerver/packets.h"
#include "cerver/receive.h"
#include "cerver/socket.h"

#include "cerver/utils/log.h"

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

		(void) memset (&receive_handle->header, 0, sizeof (PacketHeader));
		receive_handle->header_end = NULL;
		receive_handle->remaining_header = 0;

		receive_handle->spare_packet = NULL;
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

// a packet with an invalid size means that the stream is corrupted,
// so the rest of the connection can not be parsed and it is closed:
// recv () returns 0 and the receive loop drops it as usual
static u8 receive_handle_lost (
	ReceiveHandle *receive_handle, const size_t packet_size
) {

	const int sock_fd = receive_handle_sock_fd (receive_handle);

	cerver_log_with_date (
		LOG_TYPE_WARNING, LOG_TYPE_PACKET,
		"receive_handle_buffer () - invalid packet size %zu from sock fd <%d> - closing the connection",
		packet_size, sock_fd
	);

	receive_handle->state = RECEIVE_HANDLE_STATE_LOST;

	if (sock_fd > -1) (void) shutdown (sock_fd, SHUT_RDWR);

	return 1;

}

static bool receive_packet_size_is_valid (
	const ReceiveCursor *cursor, const size_t packet_size
) {

	return (
		(packet_size >= sizeof (PacketHeader))
		&& (packet_size <= cursor->max_packet_size)
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

			stop = cursor->dispatch (receive_handle, packet);
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
	packet->cerver = receive_handle->cerver;
	packet->client = receive_handle->client;
	packet->connection = receive_handle->connection;
	packet->lobby = receive_handle->lobby;

	packet->packet_size = packet->header.packet_size;

}

// creates the packet of a complete header
// a packet without data is dispatched right away
static u8 receive_handle_packet_start (
	ReceiveHandle *receive_handle, const PacketHeader *header,
	ReceiveCursor *cursor
) {

	u8 stop = 0;

	const size_t packet_size = header->packet_size;

	Packet *packet = receive_packet_size_is_valid (cursor, packet_size) ?
		packet_create_with_data (packet_size - sizeof (PacketHeader)) : NULL;

	if (packet) {
		receive_packet_set_values (receive_handle, packet, header);

		if (packet->data_size == 0) {
			receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;
			stop = cursor->dispatch (receive_handle, packet);
		}

		else {
			receive_handle->spare_packet = packet;
			stop = receive_handle_packet_data (receive_handle, cursor);
		}
	}

	else {
		stop = receive_handle_lost (receive_handle, packet_size);
	}

	return stop;

}

// a new packet starts at the cursor
// the header can be split between many buffers
static u8 receive_handle_header (
	ReceiveHandle *receive_handle, ReceiveCursor *cursor
) {

	u8 stop = 0;

	if (cursor->remaining >= sizeof (PacketHeader)) {
		// the cursor may not be aligned for a PacketHeader
		PacketHeader header;
		(void) memcpy (&header, cursor->data, sizeof (PacketHeader));
		receive_cursor_advance (cursor, sizeof (PacketHeader));

		stop = receive_handle_packet_start (receive_handle, &header, cursor);
	}

	else {
		(void) memset (&receive_handle->header, 0, sizeof (PacketHeader));
		(void) memcpy (&receive_handle->header, cursor->data, cursor->remaining);

		receive_handle->remaining_header = (unsigned int) (sizeof (PacketHeader) - cursor->remaining);
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

	const size_t received_header = sizeof (PacketHeader) - receive_handle->remaining_header;
	const size_t length = (receive_handle->remaining_header < cursor->remaining) ?
		receive_handle->remaining_header : cursor->remaining;

	(void) memcpy (((char *) &receive_handle->header) + received_header, cursor->data, length);
	receive_cursor_advance (cursor, length);

	receive_handle->remaining_header -= (unsigned int) length;

	if (!receive_handle->remaining_header) {
		PacketHeader header;
		(void) memcpy (&header, &receive_handle->header, sizeof (PacketHeader));

		receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;

		stop = receive_handle_packet_start (receive_handle, &header, cursor);
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