#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/parser/head.h>
#include <cerver/http/parser/parser.h>
#include <cerver/http/requests/method.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

// the origin server of cerver for the HTTP Garden (test/garden/run.sh): each request is answered with what the
// parser read, the format of the origins of the Garden: a JSON with the method, the target, the version, every
// field in the order of the request and the body, each one in base64

#define GARDEN_PORT						80
#define GARDEN_CONNECTIONS				(1 << 6)
#define GARDEN_THREADS					4

// the bytes of a value in base64: 4 for each 3 bytes or part of them
#define GARDEN_BASE64_SIZE(bytes)		((((bytes) + 2) / 3) * 4)
#define GARDEN_BASE64_GROUP				3
#define GARDEN_BASE64_SEXTET			0x3F
#define GARDEN_BASE64_SHIFT_HIGH		18
#define GARDEN_BASE64_SHIFT_MIDDLE		12
#define GARDEN_BASE64_SHIFT_LOW			6
#define GARDEN_BASE64_BYTE_BITS			8

// the bytes of the JSON around the values
#define GARDEN_JSON_SLACK				(1 << 8)
#define GARDEN_FIELD_SLACK				8
#define GARDEN_VERSION_SIZE				8

static const char garden_base64[] = { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" };

static Cerver *garden_cerver = NULL;

static void garden_end (int signal_number) {

	(void) signal_number;

	if (garden_cerver) (void) cerver_shutdown (garden_cerver);

}

static unsigned int garden_byte (const char *bytes, const size_t length, const size_t i) {

	return (i < length) ? (unsigned char) bytes[i] : 0;

}

// writes the bytes in base64 (the alphabet of RFC 4648 section 4, with its padding), returns the end of what it wrote
static char *garden_encode (char *out, const char *bytes, const size_t length) {

	for (size_t i = 0; i < length; i += GARDEN_BASE64_GROUP) {
		const unsigned int group = (garden_byte (bytes, length, i) << (GARDEN_BASE64_BYTE_BITS * 2))
			| (garden_byte (bytes, length, i + 1) << GARDEN_BASE64_BYTE_BITS)
			| garden_byte (bytes, length, i + 2);

		*out++ = garden_base64[(group >> GARDEN_BASE64_SHIFT_HIGH) & GARDEN_BASE64_SEXTET];
		*out++ = garden_base64[(group >> GARDEN_BASE64_SHIFT_MIDDLE) & GARDEN_BASE64_SEXTET];
		*out++ = ((i + 1) < length) ? garden_base64[(group >> GARDEN_BASE64_SHIFT_LOW) & GARDEN_BASE64_SEXTET] : '=';
		*out++ = ((i + 2) < length) ? garden_base64[group & GARDEN_BASE64_SEXTET] : '=';
	}

	return out;

}

static char *garden_text (char *out, const char *text) {

	const size_t length = strlen (text);
	(void) memcpy (out, text, length);

	return out + length;

}

// a value of the JSON: its quotes and its bytes in base64
static char *garden_value (char *out, const char *bytes, const size_t length) {

	out = garden_text (out, "\"");
	out = garden_encode (out, bytes, length);

	return garden_text (out, "\"");

}

// the bytes that the JSON of a request takes
static size_t garden_size (const CerverHttpHead *head, const String *body) {

	size_t size = GARDEN_JSON_SLACK + GARDEN_BASE64_SIZE (head->length) + GARDEN_BASE64_SIZE (body ? body->len : 0);

	for (unsigned int i = 0; i < head->fields_count; i++) {
		size += GARDEN_FIELD_SLACK + GARDEN_BASE64_SIZE (head->fields[i].name.length)
			+ GARDEN_BASE64_SIZE (head->fields[i].value.length);
	}

	return size;

}

// the target as it arrived: the copy of the head in the arena has a '\0' where its query starts
// (a target never has a NUL, the parser rejects it)
static char *garden_target (char *out, const CerverHttpHead *head) {

	const char *target = cerver_http_head_at (head, head->target);

	char *bytes = (char *) malloc (head->target.length + 1);
	if (!bytes) return out;

	for (size_t i = 0; i < head->target.length; i++) bytes[i] = target[i] ? target[i] : '?';

	out = garden_value (out, bytes, head->target.length);
	free (bytes);

	return out;

}

static size_t garden_json (char *json, const CerverHttpHead *head, const HttpRequest *request) {

	const String *body = http_request_get_body (request);

	const char *method = http_request_method_str ((RequestMethod) head->method);

	char version[GARDEN_VERSION_SIZE];
	(void) snprintf (version, GARDEN_VERSION_SIZE, "%u.%u", head->major, head->minor);

	char *out = json;
	out = garden_text (out, "{\"method\":");
	out = garden_value (out, method, strlen (method));
	out = garden_text (out, ",\"uri\":");
	out = garden_target (out, head);
	out = garden_text (out, ",\"version\":");
	out = garden_value (out, version, strlen (version));
	out = garden_text (out, ",\"headers\":[");

	for (unsigned int i = 0; i < head->fields_count; i++) {
		const CerverHttpField *field = &head->fields[i];

		out = garden_text (out, i ? ",[" : "[");
		out = garden_value (out, cerver_http_head_at (head, field->name), field->name.length);
		out = garden_text (out, ",");
		out = garden_value (out, cerver_http_head_at (head, field->value), field->value.length);
		out = garden_text (out, "]");
	}

	out = garden_text (out, "],\"body\":");
	out = garden_value (out, body ? body->str : "", body ? body->len : 0);
	out = garden_text (out, "}");

	return (size_t) (out - json);

}

// the head is the one that the parser read, rebased to the copy in the arena of the request
static void garden_handler (const HttpReceive *http_receive, const HttpRequest *request) {

	const CerverHttpHead *head = &http_receive->parser.head;

	char *json = (char *) malloc (garden_size (head, http_request_get_body (request)));
	if (!json) return;

	const size_t length = garden_json (json, head, request);

	// with its length, so the connection is kept for the next request of a pipeline
	HttpResponse *response = http_response_create (HTTP_STATUS_OK, json, length);
	if (response) {
		(void) http_response_add_content_length_header (response, length);
		(void) http_response_send (response, http_receive);
		http_response_delete (response);
	}

	free (json);

}

int main (void) {

	(void) signal (SIGINT, garden_end);
	(void) signal (SIGTERM, garden_end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	garden_cerver = cerver_create (CERVER_TYPE_WEB, "garden", GARDEN_PORT, PROTOCOL_TCP, false, GARDEN_CONNECTIONS);
	if (!garden_cerver) return 1;

	cerver_set_thpool_n_threads (garden_cerver, GARDEN_THREADS);
	cerver_set_handler_type (garden_cerver, CERVER_HANDLER_TYPE_EVENTS);
	cerver_set_reusable_address_flags (garden_cerver, true);

	HttpCerver *http_cerver = (HttpCerver *) garden_cerver->cerver_data;

	// every path with every method, so the body of each request is saved; the catch all takes the rest (the
	// asterisk of an OPTIONS)
	HttpGroup *group = http_cerver_group_create (http_cerver, "/");
	for (unsigned int method = 0; method < REQUEST_METHOD_SIZE; method++) {
		if (!http_request_method_is_valid ((RequestMethod) method)) continue;

		(void) http_group_root_add (group, (RequestMethod) method, garden_handler);
		(void) http_group_route_add (group, (RequestMethod) method, "*path", garden_handler);
	}

	http_cerver_set_catch_all_route (http_cerver, garden_handler);

	const int failed = cerver_start (garden_cerver);

	(void) cerver_teardown (garden_cerver);
	cerver_end ();

	return failed ? 1 : 0;

}
