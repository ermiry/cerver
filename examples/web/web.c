#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/handler.h>

#include <cerver/http/http.h>
#include <cerver/http/route.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

Cerver *web_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	if (web_cerver) {
		cerver_stats_print (web_cerver, false, false);
		printf ("\nHTTP Cerver stats:\n");
		http_cerver_all_stats_print ((HttpCerver *) web_cerver->cerver_data);
		printf ("\n");
		cerver_teardown (web_cerver);
	}

	cerver_end ();

	exit (0);

}

#pragma endregion

#pragma region routes

// GET /
void main_handler (CerverReceive *cr, HttpRequest *request) {

	if (http_response_render_file (cr, "./examples/web/public/index.html")) {
		cerver_log_error ("Failed to send ./examples/web/public/index.html");
	}

}

// GET /test
void test_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = http_response_json_msg ((http_status) 200, "Test route works!");
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

}

// GET /text
void text_handler (CerverReceive *cr, HttpRequest *request) {

	char const *text = "<!DOCTYPE html><html><head><meta charset=\"utf-8\" /><title>Cerver</title></head><body><h2>hola_handler () works!</h2></body></html>";
	size_t text_len = strlen (text);

	if (http_response_render_text (cr, text, text_len)) {
		cerver_log_error ("text_handler () has failed!");
	}

}

// GET /json
void json_handler (CerverReceive *cr, HttpRequest *request) {

	char const *json = "{\"msg\": \"okay\"}";
	size_t json_len = strlen (json);

	if (http_response_render_json (cr, json, json_len)) {
		cerver_log_error ("json_handler () has failed!");
	}

}

// GET /hola
void hola_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = http_response_json_msg ((http_status) 200, "Hola route works!");
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

}

// GET /adios
void adios_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = http_response_json_msg ((http_status) 200, "Adios route works!");
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

}

// GET /key
void key_handler (CerverReceive *cr, HttpRequest *request) {

	(void) http_response_json_key_value_send (
		cr,
		(http_status) 200, "key", "value"
	);

}

// GET /custom
void custom_handler (CerverReceive *cr, HttpRequest *request) {

	(void) http_response_json_custom_send (
		cr,
		(http_status) 200, "{\"oki\": \"doki\"}"
	);

}

#pragma endregion

#pragma region start

int main (int argc, char **argv) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	cerver_init ();

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Simple Web Cerver Example");
	printf ("\n");

	web_cerver = cerver_create (CERVER_TYPE_WEB, "web-cerver", 8080, PROTOCOL_TCP, false, 2, 1000);
	if (web_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (web_cerver, 4096);
		cerver_set_thpool_n_threads (web_cerver, 4);
		cerver_set_handler_type (web_cerver, CERVER_HANDLER_TYPE_THREADS);

		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) web_cerver->cerver_data;

		http_cerver_static_path_add (http_cerver, "./examples/web/public");

		// GET /
		HttpRoute *main_route = http_route_create (REQUEST_METHOD_GET, "/", main_handler);
		http_cerver_route_register (http_cerver, main_route);

		// GET /test
		HttpRoute *test_route = http_route_create (REQUEST_METHOD_GET, "test", test_handler);
		http_cerver_route_register (http_cerver, test_route);

		// GET /text
		HttpRoute *text_route = http_route_create (REQUEST_METHOD_GET, "text", text_handler);
		http_cerver_route_register (http_cerver, text_route);

		// GET /json
		HttpRoute *json_route = http_route_create (REQUEST_METHOD_GET, "json", json_handler);
		http_cerver_route_register (http_cerver, json_route);

		// GET /hola
		HttpRoute *hola_route = http_route_create (REQUEST_METHOD_GET, "hola", hola_handler);
		http_cerver_route_register (http_cerver, hola_route);

		// GET /adios
		HttpRoute *adios_route = http_route_create (REQUEST_METHOD_GET, "adios", adios_handler);
		http_cerver_route_register (http_cerver, adios_route);

		// GET /key
		HttpRoute *key_route = http_route_create (REQUEST_METHOD_GET, "key", key_handler);
		http_cerver_route_register (http_cerver, key_route);

		// GET /custom
		HttpRoute *custom_route = http_route_create (REQUEST_METHOD_GET, "custom", custom_handler);
		http_cerver_route_register (http_cerver, custom_route);

		if (cerver_start (web_cerver)) {
			cerver_log_error (
				"Failed to start %s!",
				web_cerver->info->name->str
			);

			cerver_delete (web_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (web_cerver);
	}

	cerver_end ();

	return 0;

}

#pragma endregion