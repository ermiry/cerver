#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/network/network.h>

// an address from a config or a peer: an IPv4, an IPv6 with its zone or anything else,
// a valid one is written back to text and parsed again into the same address

// the two first bytes are the port, the rest is the text of the address
#define FUZZ_ADDRESS_PORT_SIZE			2
#define FUZZ_ADDRESS_MAX_TEXT			(NETWORK_ADDRESS_STRING_SIZE << 1)

static in_port_t fuzz_address_port (const uint8_t *data) {

	return (in_port_t) ((data[0] << 8) | data[1]);

}

static void fuzz_address_round_trip (
	const struct sockaddr_storage *address, const in_port_t port
) {

	char text[NETWORK_ADDRESS_STRING_SIZE];
	(void) memset (text, 0, NETWORK_ADDRESS_STRING_SIZE);

	if (sock_ip_to_string_actual ((const struct sockaddr *) address, text)) abort ();
	if (strnlen (text, NETWORK_ADDRESS_STRING_SIZE) == NETWORK_ADDRESS_STRING_SIZE) abort ();

	struct sockaddr_storage parsed;
	if (network_parse_address (text, port, &parsed)) abort ();

	if (!sock_ip_equal ((const struct sockaddr *) address, (const struct sockaddr *) &parsed)) abort ();
	if (sock_ip_port ((const struct sockaddr *) &parsed) != port) abort ();

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < FUZZ_ADDRESS_PORT_SIZE) return 0;

	const in_port_t port = fuzz_address_port (data);

	// the text gets its NUL in a buffer of its exact size, so a read past it is found
	const size_t length = size - FUZZ_ADDRESS_PORT_SIZE;
	if (length > FUZZ_ADDRESS_MAX_TEXT) return 0;

	char *text = (char *) malloc (length + 1);
	if (!text) return 0;

	(void) memcpy (text, data + FUZZ_ADDRESS_PORT_SIZE, length);
	text[length] = '\0';

	struct sockaddr_storage address;
	if (!network_parse_address (text, port, &address)) {
		fuzz_address_round_trip (&address, port);
	}

	free (text);

	return 0;

}
