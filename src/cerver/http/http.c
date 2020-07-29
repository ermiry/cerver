#include <stdlib.h>

#include "cerver/types/types.h"

#include "cerver/handler.h"
#include "cerver/packets.h"

#include "cerver/http/http_parser.h"
#include "cerver/http/parser.h"
#include "cerver/http/json.h"
#include "cerver/http/request.h"
#include "cerver/http/response.h"

#include "cerver/utils/utils.h"

static int http_receive_handle_url (http_parser *parser, const char *at, size_t length) {

	printf ("\n%s\n\n", at);
	printf ("%.*s\n", (int) length, at);

	return 0;

}

static inline int http_receive_handle_header_field_handle (const char *header) {

	if (!strcmp ("Accept", header)) return REQUEST_HEADER_ACCEPT;
	if (!strcmp ("Accept-Charset", header)) return REQUEST_HEADER_ACCEPT_CHARSET;
	if (!strcmp ("Accept-Encoding", header)) return REQUEST_HEADER_ACCEPT_ENCODING;
	if (!strcmp ("Accept-Language", header)) return REQUEST_HEADER_ACCEPT_LANGUAGE;

	if (!strcmp ("Access-Control-Request-Headers", header)) return REQUEST_HEADER_ACCESS_CONTROL_REQUEST_HEADERS;

	if (!strcmp ("Authorization", header)) return REQUEST_HEADER_AUTHORIZATION;

	if (!strcmp ("Cache-Control", header)) return REQUEST_HEADER_CACHE_CONTROL;

	if (!strcmp ("Connection", header)) return REQUEST_HEADER_CONNECTION;

	if (!strcmp ("Content-Length", header)) return REQUEST_HEADER_CONTENT_LENGTH;
	if (!strcmp ("Content-Type", header)) return REQUEST_HEADER_CONTENT_TYPE;

	if (!strcmp ("Cookie", header)) return REQUEST_HEADER_COOKIE;

	if (!strcmp ("Date", header)) return REQUEST_HEADER_DATE;

	if (!strcmp ("Expect", header)) return REQUEST_HEADER_EXPECT;

	if (!strcmp ("Host", header)) return REQUEST_HEADER_HOST;

	if (!strcmp ("Proxy-Authorization", header)) return REQUEST_HEADER_PROXY_AUTHORIZATION;

	if (!strcmp ("User-Agent", header)) return REQUEST_HEADER_USER_AGENT;

	return REQUEST_HEADER_INVALID;		// no known header

}

static int http_receive_handle_header_field (http_parser *parser, const char *at, size_t length) {

	char header[32] = { 0 };
	snprintf (header, 32, "%.*s", (int) length, at);
	// printf ("\nheader field: /%s/\n", header);

	((HttpRequest *) parser->data)->next_header = http_receive_handle_header_field_handle (header);

	return 0;

}

static int http_receive_handle_header_value (http_parser *parser, const char *at, size_t length) {

	// printf ("\nheader value: %s\n", at);

	HttpRequest *request = (HttpRequest *) parser->data;
	if (request->next_header != REQUEST_HEADER_INVALID) {
		request->headers[request->next_header] = estring_new (NULL);
		request->headers[request->next_header]->str = c_string_create ("%.*s", (int) length, at);
		request->headers[request->next_header]->len = length;
	}

	// request->next_header = REQUEST_HEADER_INVALID;

	return 0;

}

static int http_receive_handle_body (http_parser *parser, const char *at, size_t length) {

	// printf ("Body: %.*s", (int) length, at);

	HttpRequest *request = (HttpRequest *) parser->data;
	request->body = estring_new (NULL);
	request->body->str = c_string_create ("%.*s", (int) length, at);
	request->body->len = length;

	return 0;

}

void http_receive_handle (CerverReceive *cr, ssize_t rc, char *packet_buffer) {

	http_parser *parser = malloc (sizeof (http_parser));
	http_parser_init (parser, HTTP_REQUEST);

	http_parser_settings settings = { 0 };
	settings.on_message_begin = NULL;
	settings.on_url = http_receive_handle_url;
	settings.on_status = NULL;
	settings.on_header_field = http_receive_handle_header_field;
	settings.on_header_value = http_receive_handle_header_value;
	settings.on_headers_complete = NULL;
	settings.on_body = http_receive_handle_body;
	settings.on_message_complete = NULL;
	settings.on_chunk_header = NULL;
	settings.on_chunk_complete = NULL;

	HttpRequest *request = http_request_new ();
	parser->data = request;

	size_t n_parsed = http_parser_execute (parser, &settings, packet_buffer, rc);

	printf ("\nn parsed %ld / received %ld\n", n_parsed, rc);

	// printf ("Method: %s\n", http_method_str (parser->method));

	request->method = parser->method;

	// http_request_headers_print (request);

	// TODO: select method handler

	http_request_delete (request);

	HttpResponse *res = NULL;

	estring *test = estring_new ("Web cerver works!");
	JsonKeyValue *jkvp = json_key_value_create ("msg", test, VALUE_TYPE_STRING);
	size_t json_len;
	char *json = json_create_with_one_pair (jkvp, &json_len);
	// json_key_value_delete (jkvp);
	res = http_response_create (200, NULL, 0, json, json_len);

	if (res) {
		// send the response to the client
		http_response_compile (res);
		printf ("Response: %s\n", res->res);
		http_response_send_to_socket (res, cr->socket->sock_fd);
		http_respponse_delete (res);
	}

	// Packet *packet = packet_new ();
	// packet_set_data (packet, response->str, response->len);

	// if (!packet_send_to_socket (packet, cr->socket, 0, NULL, true)) {
	// 	printf ("\nsent response!\n");
	// }

	connection_end (cr->connection);

	// packet_delete (packet);
	// estring_delete (response);

	free (parser);

	printf ("http_receive_handle () - end!\n");

}