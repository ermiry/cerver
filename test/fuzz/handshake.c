#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/http/websockets/client.h>
#include <cerver/http/websockets/deflate.h>
#include <cerver/http/websockets/handshake.h>
#include "cerver/http/websockets/internal.h"

// the text of a handshake: the first byte selects what parses the rest,
// the response of a server for the client, an offer or a response of permessage-deflate,
// or the values of the request that the server checks

#define FUZZ_HANDSHAKE_HEADER_SIZE			1

#define FUZZ_HANDSHAKE_RESPONSE				0
#define FUZZ_HANDSHAKE_OFFER				1
#define FUZZ_HANDSHAKE_ACCEPT				2
#define FUZZ_HANDSHAKE_VALUES				3
#define FUZZ_HANDSHAKE_MODES				4

static const char *fuzz_handshake_key = { "dGhlIHNhbXBsZSBub25jZQ==" };

static char *fuzz_handshake_protocols[] = { (char *) "chat", (char *) "superchat" };
#define FUZZ_HANDSHAKE_N_PROTOCOLS			2

static HttpWebSocketDeflateOptions fuzz_handshake_options (void) {

	HttpWebSocketDeflateOptions options;
	http_web_socket_deflate_options_init (&options);
	options.enabled = true;

	return options;

}

static void fuzz_handshake_response (const char *text, const size_t size) {

	HttpWebSocketClientResponse response;
	if (!http_web_socket_client_response_parse (text, size, &response)) {
		const HttpWebSocketDeflateOptions options = fuzz_handshake_options ();

		const char *protocol = NULL;
		bool compression = false;
		HttpWebSocketDeflateParameters parameters;
		(void) http_web_socket_client_response_check (
			&response, fuzz_handshake_key,
			fuzz_handshake_protocols, FUZZ_HANDSHAKE_N_PROTOCOLS,
			&options, &protocol, &compression, &parameters
		);
	}

}

static void fuzz_handshake_offer (const char *text, const size_t size) {

	const HttpWebSocketDeflateOptions options = fuzz_handshake_options ();

	HttpWebSocketDeflateParameters parameters;
	if (http_web_socket_deflate_negotiate (text, size, &options, &parameters)) {
		char value[HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE];
		(void) http_web_socket_deflate_response (&parameters, value, HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE);
	}

}

static void fuzz_handshake_accept (const char *text, const size_t size) {

	const HttpWebSocketDeflateOptions options = fuzz_handshake_options ();

	HttpWebSocketDeflateParameters parameters;
	(void) http_web_socket_deflate_accept (text, size, &options, &parameters);

}

static void fuzz_handshake_values (const char *text, const size_t size) {

	(void) http_web_socket_header_has_token (text, size, HTTP_WEB_SOCKET_UPGRADE_TOKEN);
	(void) http_web_socket_header_has_token (text, size, HTTP_WEB_SOCKET_CONNECTION_TOKEN);

	(void) http_web_socket_protocol_select (
		text, size, (const char * const *) fuzz_handshake_protocols, FUZZ_HANDSHAKE_N_PROTOCOLS
	);

	char accept[HTTP_WEB_SOCKET_ACCEPT_SIZE];
	if (http_web_socket_key_is_valid (text, size)) (void) http_web_socket_accept_compute (text, size, accept);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < FUZZ_HANDSHAKE_HEADER_SIZE) return 0;

	// a buffer of the exact size, so a read past the text is caught
	const size_t length = size - FUZZ_HANDSHAKE_HEADER_SIZE;
	char *text = (char *) malloc (length ? length : 1);
	if (!text) return 0;
	(void) memcpy (text, data + FUZZ_HANDSHAKE_HEADER_SIZE, length);

	switch (data[0] % FUZZ_HANDSHAKE_MODES) {
		case FUZZ_HANDSHAKE_RESPONSE: fuzz_handshake_response (text, length); break;
		case FUZZ_HANDSHAKE_OFFER: fuzz_handshake_offer (text, length); break;
		case FUZZ_HANDSHAKE_ACCEPT: fuzz_handshake_accept (text, length); break;
		default: fuzz_handshake_values (text, length); break;
	}

	free (text);

	return 0;

}
