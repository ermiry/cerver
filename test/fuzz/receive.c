#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/handler/receive.h>
#include <cerver/packets/packets.h>

// the assembly of packets from the received buffers: the first byte selects the size of
// every piece, so a header or a packet is split at any offset, and the rest is the stream
// a packet with an invalid size stops the stream, like a closed connection

#define FUZZ_RECEIVE_MAX_PACKET_SIZE		(1 << 16)
#define FUZZ_RECEIVE_PIECE_SELECTOR_SIZE	1

static u8 fuzz_receive_dispatch (ReceiveHandle *receive_handle, Packet *packet) {

	(void) receive_handle;

	// the data of the packet has the size of its header
	if (packet->data_size != (packet->header.packet_size - PACKET_HEADER_SIZE)) abort ();

	packet_delete (packet);

	return 0;

}

// every piece is copied to a buffer of its exact size, so a read past it is found
static void fuzz_receive_piece (
	ReceiveHandle *receive_handle, const uint8_t *piece, const size_t piece_size
) {

	char *buffer = (char *) malloc (piece_size);
	if (!buffer) return;

	(void) memcpy (buffer, piece, piece_size);

	receive_handle->buffer = buffer;
	receive_handle->buffer_size = piece_size;
	receive_handle->received_size = piece_size;

	receive_handle_buffer (receive_handle, FUZZ_RECEIVE_MAX_PACKET_SIZE, fuzz_receive_dispatch);

	free (buffer);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size <= FUZZ_RECEIVE_PIECE_SELECTOR_SIZE) return 0;

	// a piece of 0 bytes feeds the stream whole
	const size_t piece_size = data[0] ? (size_t) data[0] : (size - FUZZ_RECEIVE_PIECE_SELECTOR_SIZE);

	const uint8_t *stream = data + FUZZ_RECEIVE_PIECE_SELECTOR_SIZE;
	const size_t stream_size = size - FUZZ_RECEIVE_PIECE_SELECTOR_SIZE;

	ReceiveHandle *receive_handle = receive_handle_new ();
	if (!receive_handle) return 0;

	receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;

	for (
		size_t start = 0;
		(start < stream_size) && (receive_handle->state != RECEIVE_HANDLE_STATE_LOST);
		start += piece_size
	) {
		const size_t left = stream_size - start;
		fuzz_receive_piece (receive_handle, stream + start, (left < piece_size) ? left : piece_size);
	}

	receive_handle_end (receive_handle);
	receive_handle_delete (receive_handle);

	return 0;

}
