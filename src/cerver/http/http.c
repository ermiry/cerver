#include <stdlib.h>

#include "cerver/types/types.h"

#include "cerver/handler.h"
#include "cerver/packets.h"

#include "cerver/http/http_parser.h"
#include "cerver/http/parser.h"
#include "cerver/http/json.h"
#include "cerver/http/response.h"

static int http_receive_handle_url (http_parser *parser, const char *at, size_t length) {

	// printf ("\nurl: %s\n", at);
	return 0;

}

static int http_receive_handle_header_field (http_parser *parser, const char *at, size_t length) {

	// printf ("\nheader field: %s\n", at);
	return 0;

}

static int http_receive_handle_header_value (http_parser *parser, const char *at, size_t length) {

	// printf ("\nheader value: %s\n", at);
	return 0;

}

static int http_receive_handle_body (http_parser *parser, const char *at, size_t length) {

	// printf ("\nbody: %s\n", at);
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

	size_t n_parsed = http_parser_execute (parser, &settings, packet_buffer, rc);

	printf ("\nn parsed %ld / received %ld\n", n_parsed, rc);

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