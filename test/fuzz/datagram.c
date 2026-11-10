#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/auth.h>
#include <cerver/connection.h>
#include <cerver/packets.h>
#include <cerver/sessions.h>
#include <cerver/udp.h>

// the datagrams of an UDP cerver or client: the check of their single packet and their token,
// the packet that is created from them, and the auth data of an auth packet
// the first byte selects how the rest is taken: as it came, or with the size of its packet
// fixed to the datagram, without or with a token after it, so the valid paths are reached too

#define FUZZ_DATAGRAM_SELECTOR_SIZE		1
#define FUZZ_DATAGRAM_FIX_SIZE			(1 << 0)
#define FUZZ_DATAGRAM_WITH_TOKEN		(1 << 1)
#define FUZZ_DATAGRAM_MAX_PACKET_SIZE	(1 << 14)

static const u8 fuzz_datagram_token[CONNECTION_DATAGRAM_TOKEN_SIZE] = { 0 };

// the size of the packet is the size of the datagram, without its token
static void fuzz_datagram_fix_size (char *datagram, const size_t size, const bool with_token) {

	const size_t token_size = with_token ? CONNECTION_DATAGRAM_TOKEN_SIZE : 0;

	PacketHeader header;
	if (
		(size >= (PACKET_HEADER_SIZE + token_size))
		&& (packet_header_decode ((const u8 *) datagram, &header) == PACKET_HEADER_RESULT_NONE)
	) {
		header.packet_size = size - token_size;
		packet_header_encode (&header, (u8 *) datagram);
	}

}

// the kind that udp_datagram_check () must find
static UdpDatagram fuzz_datagram_expected (const char *datagram, const size_t size) {

	UdpDatagram expected = UDP_DATAGRAM_INVALID;

	PacketHeader header;
	if (
		(size >= PACKET_HEADER_SIZE)
		&& (packet_header_decode ((const u8 *) datagram, &header) == PACKET_HEADER_RESULT_NONE)
	) {
		const bool valid_size = (header.packet_size >= PACKET_HEADER_SIZE)
			&& (header.packet_size <= FUZZ_DATAGRAM_MAX_PACKET_SIZE)
			&& (header.packet_size <= size);

		if (valid_size && (header.packet_size == size)) expected = UDP_DATAGRAM_PLAIN;
		else if (valid_size && ((size - header.packet_size) == CONNECTION_DATAGRAM_TOKEN_SIZE)) expected = UDP_DATAGRAM_TOKEN;
	}

	return expected;

}

static void fuzz_datagram_packet (const char *datagram, const PacketHeader *header) {

	Packet *packet = udp_datagram_packet_create (datagram, header);
	if (packet) {
		if (packet->packet_size != header->packet_size) abort ();
		if (packet->data_size != (header->packet_size - PACKET_HEADER_SIZE)) abort ();

		if (packet->data_size && memcmp (packet->data, datagram + PACKET_HEADER_SIZE, packet->data_size)) {
			abort ();
		}

		// an auth packet from the network, a token without its NUL is read up to its size
		AuthData *auth_data = auth_data_get (packet);
		if (auth_data && auth_data->token && (auth_data->token->len >= TOKEN_SIZE)) abort ();
		auth_data_release (auth_data);

		packet_delete (packet);
	}

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < FUZZ_DATAGRAM_SELECTOR_SIZE) return 0;

	const u8 selector = data[0];
	const size_t datagram_size = size - FUZZ_DATAGRAM_SELECTOR_SIZE;

	// a copy of its exact size, so a read past the datagram is found
	char *datagram = (char *) malloc (datagram_size ? datagram_size : FUZZ_DATAGRAM_SELECTOR_SIZE);
	if (!datagram) return 0;

	if (datagram_size) (void) memcpy (datagram, data + FUZZ_DATAGRAM_SELECTOR_SIZE, datagram_size);

	if (selector & FUZZ_DATAGRAM_FIX_SIZE) {
		fuzz_datagram_fix_size (datagram, datagram_size, selector & FUZZ_DATAGRAM_WITH_TOKEN);
	}

	PacketHeader header;
	const UdpDatagram kind = udp_datagram_check (
		datagram, datagram_size, FUZZ_DATAGRAM_MAX_PACKET_SIZE, &header
	);

	if (kind != fuzz_datagram_expected (datagram, datagram_size)) abort ();

	if (kind == UDP_DATAGRAM_TOKEN) {
		const bool matches = !memcmp (datagram + header.packet_size, fuzz_datagram_token, CONNECTION_DATAGRAM_TOKEN_SIZE);
		if (udp_datagram_token_matches (datagram, &header, fuzz_datagram_token) != matches) abort ();
	}

	if (kind != UDP_DATAGRAM_INVALID) fuzz_datagram_packet (datagram, &header);

	free (datagram);

	return 0;

}
