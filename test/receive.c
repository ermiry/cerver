#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>

#include <sys/socket.h>

#include <cerver/packets.h>
#include <cerver/receive.h>
#include <cerver/socket.h>

#include "test.h"

static void test_receive_error_to_string (void) {

	test_check_str_eq (receive_error_to_string (RECEIVE_ERROR_NONE), "None", NULL);
	test_check_str_eq (receive_error_to_string (RECEIVE_ERROR_TIMEOUT), "Timeoout", NULL);
	test_check_str_eq (receive_error_to_string (RECEIVE_ERROR_EMPTY), "Empty", NULL);
	test_check_str_eq (receive_error_to_string (RECEIVE_ERROR_FAILED), "Failed", NULL);

	test_check_str_eq (receive_error_to_string ((ReceiveError) 10), "None", NULL);

}

static void test_receive_type_to_string (void) {
	
	test_check_str_eq (receive_type_to_string (RECEIVE_TYPE_NONE), "None", NULL);
	test_check_str_eq (receive_type_to_string (RECEIVE_TYPE_NORMAL), "Normal", NULL);
	test_check_str_eq (receive_type_to_string (RECEIVE_TYPE_ON_HOLD), "On-Hold", NULL);
	test_check_str_eq (receive_type_to_string (RECEIVE_TYPE_ADMIN), "Admin", NULL);

	test_check_str_eq (receive_type_to_string ((ReceiveType) 10), "None", NULL);

}

static void test_receive_handle_state_to_string (void) {
	
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_NONE), "None", NULL);
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_NORMAL), "Normal", NULL);
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_SPLIT_HEADER), "Split-Header", NULL);
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_SPLIT_PACKET), "Split-Packet", NULL);
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_COMP_HEADER), "Complete-Header", NULL);
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_LOST), "Lost", NULL);

	test_check_str_eq (receive_handle_state_to_string ((ReceiveHandleState) 10), "None", NULL);

}

static void test_receive_handle_create (void) {

	ReceiveHandle *receive = receive_handle_new ();

	test_check_ptr (receive);

	test_check_unsigned_eq (receive->type, RECEIVE_TYPE_NONE, NULL);

	test_check_null_ptr (receive->cerver);

	test_check_null_ptr (receive->socket);
	test_check_null_ptr (receive->connection);
	test_check_null_ptr (receive->client);
	test_check_null_ptr (receive->admin);

	test_check_null_ptr (receive->lobby);

	test_check_null_ptr (receive->buffer);
	test_check_unsigned_eq (receive->buffer_size, 0, NULL);

	test_check_unsigned_eq (receive->state, RECEIVE_HANDLE_STATE_NONE, NULL);

	test_check_null_ptr (receive->header_end);
	test_check_unsigned_eq (receive->remaining_header, 0, NULL);

	test_check_null_ptr (receive->spare_packet);

	receive_handle_delete (receive);

}

#pragma region buffer

// feeds receive_handle_buffer () with a stream of packets split between recv () calls
// at every byte offset, one byte at a time, in random pieces and whole

// bugs that this suite pinned:
// - a header that was split, followed by a buffer shorter than the missing bytes, read old bytes
// - a header that was completed with the exact missing bytes was dispatched one recv () later
// - the data of a split packet moved the buffer by the whole packet size
// - a packet with an invalid size left the connection in a LOST state forever
// - the spare packet was kept after it was dispatched
// coverage holes:
// - the dispatch of a real cerver (see test/handler)

#define RECEIVE_TEST_PACKETS				16
#define RECEIVE_TEST_MAX_PACKET_SIZE		(1 << 12)
#define RECEIVE_TEST_RANDOM_ROUNDS			64
#define RECEIVE_TEST_RANDOM_PIECE			32

// the data sizes of the packets in the stream
static const size_t receive_test_data_sizes[RECEIVE_TEST_PACKETS] = {
	0, 1, 7, 64, 0, 1000, 2, 0, 31, 33, 0, 512, 3, 0, 100, 1
};

typedef struct ReceiveTestDispatched {

	unsigned int n_packets;
	u32 request_types[RECEIVE_TEST_PACKETS];
	size_t data_sizes[RECEIVE_TEST_PACKETS];
	bool data_valid[RECEIVE_TEST_PACKETS];

	// the dispatch returns 1 after this number of packets, 0 never stops
	unsigned int stop_after;

} ReceiveTestDispatched;

static ReceiveTestDispatched dispatched;

static char receive_test_data_byte (const u32 request_type, const size_t idx) {

	return (char) ((request_type * 31) + idx);

}

static bool receive_test_data_is_valid (const Packet *packet) {

	bool valid = true;

	const char *data = (const char *) packet->data;
	for (size_t i = 0; i < packet->data_size; i++) {
		if (data[i] != receive_test_data_byte (packet->header.request_type, i)) {
			valid = false;
			break;
		}
	}

	return valid;

}

static u8 receive_test_dispatch (ReceiveHandle *receive_handle, Packet *packet) {

	(void) receive_handle;

	const unsigned int idx = dispatched.n_packets;
	if (idx < RECEIVE_TEST_PACKETS) {
		dispatched.request_types[idx] = packet->header.request_type;
		dispatched.data_sizes[idx] = packet->data_size;
		dispatched.data_valid[idx] = receive_test_data_is_valid (packet);
	}

	dispatched.n_packets += 1;

	packet_delete (packet);

	return (dispatched.stop_after && (dispatched.n_packets >= dispatched.stop_after)) ? 1 : 0;

}

// appends a packet with its header and its data to the stream
static size_t receive_test_append_packet (
	char *stream, size_t position,
	const u32 request_type, const size_t data_size, const size_t packet_size
) {

	PacketHeader header;
	(void) memset (&header, 0, sizeof (PacketHeader));
	header.packet_type = PACKET_TYPE_APP;
	header.packet_size = packet_size;
	header.request_type = request_type;

	(void) memcpy (stream + position, &header, sizeof (PacketHeader));
	position += sizeof (PacketHeader);

	for (size_t i = 0; i < data_size; i++) {
		stream[position + i] = receive_test_data_byte (request_type, i);
	}

	return position + data_size;

}

static char *receive_test_stream_create (size_t *stream_size) {

	size_t total = 0;
	for (unsigned int i = 0; i < RECEIVE_TEST_PACKETS; i++) {
		total += sizeof (PacketHeader) + receive_test_data_sizes[i];
	}

	char *stream = (char *) malloc (total);
	test_check_ptr (stream);

	size_t position = 0;
	for (unsigned int i = 0; i < RECEIVE_TEST_PACKETS; i++) {
		position = receive_test_append_packet (
			stream, position, i, receive_test_data_sizes[i],
			sizeof (PacketHeader) + receive_test_data_sizes[i]
		);
	}

	*stream_size = total;

	return stream;

}

// every piece is copied to a buffer of its exact size, so the sanitizers catch any read past it
static void receive_test_feed_piece (
	ReceiveHandle *receive_handle, const char *piece, const size_t piece_size
) {

	char *buffer = (char *) malloc (piece_size);
	test_check_ptr (buffer);
	(void) memcpy (buffer, piece, piece_size);

	receive_handle->buffer = buffer;
	receive_handle->buffer_size = piece_size;
	receive_handle->received_size = piece_size;

	receive_handle_buffer (receive_handle, RECEIVE_TEST_MAX_PACKET_SIZE, receive_test_dispatch);

	free (buffer);

}

// feeds the stream in pieces that end at each cut
static void receive_test_feed (
	const char *stream, const size_t stream_size,
	const size_t *cuts, const size_t n_cuts
) {

	(void) memset (&dispatched, 0, sizeof (ReceiveTestDispatched));

	ReceiveHandle *receive_handle = receive_handle_new ();
	test_check_ptr (receive_handle);
	receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;

	size_t start = 0;
	for (size_t i = 0; i <= n_cuts; i++) {
		const size_t end = (i < n_cuts) ? cuts[i] : stream_size;
		if (end > start) receive_test_feed_piece (receive_handle, stream + start, end - start);
		start = end;
	}

	test_check_null_ptr (receive_handle->spare_packet);
	test_check (receive_handle->state == RECEIVE_HANDLE_STATE_NORMAL, "the stream did not end in a packet boundary");

	receive_handle_end (receive_handle);
	receive_handle_delete (receive_handle);

}

static void receive_test_check_dispatched (void) {

	test_check_unsigned_eq (dispatched.n_packets, RECEIVE_TEST_PACKETS, NULL);

	for (unsigned int i = 0; i < RECEIVE_TEST_PACKETS; i++) {
		test_check_unsigned_eq (dispatched.request_types[i], i, NULL);
		test_check (dispatched.data_sizes[i] == receive_test_data_sizes[i], "wrong packet data size");
		test_check (dispatched.data_valid[i], "wrong packet data");
	}

}

static void test_receive_buffer_whole (const char *stream, const size_t stream_size) {

	receive_test_feed (stream, stream_size, NULL, 0);
	receive_test_check_dispatched ();

}

static void test_receive_buffer_every_split (const char *stream, const size_t stream_size) {

	for (size_t offset = 1; offset < stream_size; offset++) {
		receive_test_feed (stream, stream_size, &offset, 1);
		receive_test_check_dispatched ();
	}

}

static void test_receive_buffer_bytes (const char *stream, const size_t stream_size) {

	size_t *cuts = (size_t *) calloc (stream_size, sizeof (size_t));
	test_check_ptr (cuts);

	for (size_t i = 0; i < stream_size; i++) cuts[i] = i + 1;

	receive_test_feed (stream, stream_size, cuts, stream_size);
	receive_test_check_dispatched ();

	free (cuts);

}

static void test_receive_buffer_random (const char *stream, const size_t stream_size) {

	size_t *cuts = (size_t *) calloc (stream_size, sizeof (size_t));
	test_check_ptr (cuts);

	for (unsigned int round = 0; round < RECEIVE_TEST_RANDOM_ROUNDS; round++) {
		size_t n_cuts = 0;
		size_t position = 0;
		while (position < stream_size) {
			position += 1 + ((size_t) rand () % RECEIVE_TEST_RANDOM_PIECE);
			if (position < stream_size) cuts[n_cuts++] = position;
		}

		receive_test_feed (stream, stream_size, cuts, n_cuts);
		receive_test_check_dispatched ();
	}

	free (cuts);

}

// the dispatch can stop the buffer, then nothing else is handled
static void test_receive_buffer_stop (const char *stream, const size_t stream_size) {

	(void) memset (&dispatched, 0, sizeof (ReceiveTestDispatched));
	dispatched.stop_after = 1;

	ReceiveHandle *receive_handle = receive_handle_new ();
	receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;

	receive_test_feed_piece (receive_handle, stream, stream_size);
	test_check_unsigned_eq (dispatched.n_packets, 1, NULL);

	receive_handle_end (receive_handle);
	receive_handle_delete (receive_handle);

}

// a partial packet is released when the connection ends (checked with the sanitizers)
static void test_receive_buffer_partial (const char *stream) {

	(void) memset (&dispatched, 0, sizeof (ReceiveTestDispatched));

	ReceiveHandle *receive_handle = receive_handle_new ();
	receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;

	// the header and a part of the data of the second packet (1 byte)
	receive_test_feed_piece (receive_handle, stream, (2 * sizeof (PacketHeader)));
	test_check_unsigned_eq (dispatched.n_packets, 1, NULL);
	test_check_ptr (receive_handle->spare_packet);
	test_check (receive_handle->state == RECEIVE_HANDLE_STATE_SPLIT_PACKET, NULL);

	receive_handle_end (receive_handle);
	test_check_null_ptr (receive_handle->spare_packet);

	receive_handle_delete (receive_handle);

}

static void receive_test_invalid_case (const size_t packet_size) {

	(void) memset (&dispatched, 0, sizeof (ReceiveTestDispatched));

	int fds[2] = { -1, -1 };
	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), 0, NULL);

	// a valid packet, an invalid one and another valid one
	char stream[(3 * sizeof (PacketHeader))];
	size_t position = receive_test_append_packet (stream, 0, 0, 0, sizeof (PacketHeader));
	position = receive_test_append_packet (stream, position, 1, 0, packet_size);
	position = receive_test_append_packet (stream, position, 2, 0, sizeof (PacketHeader));

	Socket *socket = socket_create (fds[0]);
	ReceiveHandle *receive_handle = receive_handle_new ();
	receive_handle->socket = socket;
	receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;

	receive_test_feed_piece (receive_handle, stream, position);

	// the stream is corrupted after the invalid packet
	test_check_unsigned_eq (dispatched.n_packets, 1, NULL);
	test_check (receive_handle->state == RECEIVE_HANDLE_STATE_LOST, NULL);

	// the connection is closed, so the receive loop drops it
	char byte = 0;
	test_check (recv (fds[1], &byte, 1, 0) == 0, "the lost connection was not closed");

	// more data is ignored
	receive_test_feed_piece (receive_handle, stream, position);
	test_check_unsigned_eq (dispatched.n_packets, 1, NULL);

	receive_handle_end (receive_handle);
	receive_handle_delete (receive_handle);

	(void) close (fds[0]);
	(void) close (fds[1]);
	socket_delete (socket);

}

static void test_receive_buffer_invalid (void) {

	receive_test_invalid_case (0);
	receive_test_invalid_case (sizeof (PacketHeader) - 1);
	receive_test_invalid_case (RECEIVE_TEST_MAX_PACKET_SIZE + 1);

}

static void test_receive_buffer (void) {

	(void) printf ("Testing RECEIVE HANDLE buffer...\n");

	size_t stream_size = 0;
	char *stream = receive_test_stream_create (&stream_size);

	test_receive_buffer_whole (stream, stream_size);
	test_receive_buffer_every_split (stream, stream_size);
	test_receive_buffer_bytes (stream, stream_size);
	test_receive_buffer_random (stream, stream_size);
	test_receive_buffer_stop (stream, stream_size);
	test_receive_buffer_partial (stream);
	test_receive_buffer_invalid ();

	free (stream);

	(void) printf ("Done!\n");

}

#pragma endregion

int main (int argc, char **argv) {

	(void) printf ("Testing RECEIVE HANDLE...\n");

	test_receive_error_to_string ();
	test_receive_type_to_string ();
	test_receive_handle_state_to_string ();

	test_receive_handle_create ();

	test_receive_buffer ();

	(void) printf ("\nDone with RECEIVE HANDLE tests!\n\n");

	return 0;

}
