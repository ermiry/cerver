#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/events/events.h>

#include <cerver/http/clients/client.h>
#include "cerver/http/clients/internal.h"
#include <cerver/http/core/context.h>
#include <cerver/http/requests/method.h>

// the bytes of a response read by a request of the HTTP client, like the ones that its connection receives:
// the first byte selects the method (its lowest bit, GET or HEAD) and the size of the pieces (its other bits,
// 0 feeds the response whole); the size of each piece is taken from the byte that starts it, so the splits of
// the response are fuzzed with its bytes. The views of the response are checked after it ends

#define RESPONSE_HEADER_SIZE			1

#define RESPONSE_METHOD_MASK			0x01
#define RESPONSE_STEP_SHIFT				1

// a small limit, so the bodies that pass it are found too
#define RESPONSE_MAX_SIZE				(1 << 16)

#define RESPONSE_URL					"http://localhost/"

static const char *response_header_names[] = {
	"Content-Type", "Content-Length", "Transfer-Encoding", "Connection"
};

#define RESPONSE_HEADER_NAMES			(sizeof (response_header_names) / sizeof (const char *))

// one client alone for every input: its loop and its thread are never used, its requests are not sent
static CerverHttpClient *response_client (void) {

	static CerverHttpClient *client = NULL;

	if (!client) {
		client = cerver_http_client_create_with_backend (CERVER_EVENTS_BACKEND_EPOLL);
		if (!client) abort ();

		cerver_http_client_set_max_response_size (client, RESPONSE_MAX_SIZE);
	}

	return client;

}

// a view with data ends with a '\0' after its bytes
static void response_view_check (const CerverHttpView view) {

	if (view.data && (view.data[view.length] != '\0')) abort ();

}

static void response_check (const CerverHttpClientRequest *request, const unsigned int result) {

	const CerverHttpClientResponse *response = &request->response;

	const CerverHttpClientError error = cerver_http_client_response_error (response);
	const unsigned int status = cerver_http_client_response_status (response);

	// a response that is complete or that needs more has no error, and one that failed has one
	if ((result != HTTP_CLIENT_READ_FAILED) && (error != CERVER_HTTP_CLIENT_ERROR_NONE)) abort ();
	if ((result == HTTP_CLIENT_READ_FAILED) && (error == CERVER_HTTP_CLIENT_ERROR_NONE)) abort ();

	// a complete response has its head
	if ((result == HTTP_CLIENT_READ_COMPLETE) && !status) abort ();

	for (size_t idx = 0; idx < RESPONSE_HEADER_NAMES; idx++) {
		response_view_check (cerver_http_client_response_header (response, response_header_names[idx]));
	}

	const CerverHttpView body = cerver_http_client_response_body (response);
	response_view_check (body);

	// the response of a HEAD has no body, whatever its head says
	if ((request->method == REQUEST_METHOD_HEAD) && body.length) abort ();

}

// a buffer of the exact size of each piece, so a read past it is found
static unsigned int response_read_piece (
	CerverHttpClientRequest *request, const uint8_t *piece, const size_t length
) {

	char *buffer = (char *) malloc (length);
	if (!buffer) abort ();

	(void) memcpy (buffer, piece, length);

	const unsigned int result = http_client_response_read (request, buffer, length);

	free (buffer);

	return result;

}

// the size of the piece that starts at a byte: from 1 to the step, the rest of the response without a step
static size_t response_piece_length (const uint8_t first, const size_t step, const size_t left) {

	const size_t length = step ? (1 + ((size_t) first % step)) : left;

	return (length < left) ? length : left;

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < RESPONSE_HEADER_SIZE) return 0;

	const CerverHttpMethod method = (data[0] & RESPONSE_METHOD_MASK) ? REQUEST_METHOD_HEAD : REQUEST_METHOD_GET;
	const size_t step = (size_t) (data[0] >> RESPONSE_STEP_SHIFT);

	CerverHttpClientRequest *request = cerver_http_client_request_create (response_client (), method, RESPONSE_URL);
	if (!request) abort ();

	http_client_response_start (request);

	const uint8_t *bytes = data + RESPONSE_HEADER_SIZE;
	const size_t length = size - RESPONSE_HEADER_SIZE;

	unsigned int result = HTTP_CLIENT_READ_MORE;
	for (size_t offset = 0; (offset < length) && (result == HTTP_CLIENT_READ_MORE);) {
		const size_t piece = response_piece_length (bytes[offset], step, length - offset);

		result = response_read_piece (request, bytes + offset, piece);

		offset += piece;
	}

	response_check (request, result);

	cerver_http_client_request_delete (request);

	return 0;

}
