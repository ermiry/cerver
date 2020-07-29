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
#include <cerver/http/json.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

Cerver *web_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {
	
	if (web_cerver) {
		cerver_stats_print (web_cerver);
		cerver_teardown (web_cerver);
	} 

	exit (0);

}

#pragma endregion

#pragma region routes

void test_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = NULL;

	estring *test = estring_new ("Test route works!");
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

}

void hola_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = NULL;

	estring *test = estring_new ("Hola route works!");
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

}

void adios_handler (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = NULL;

	estring *test = estring_new ("Adios route works!");
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

}

#pragma endregion

#pragma region start

int main (int argc, char **argv) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

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

		// /test
		HttpRoute *test_route = http_route_create ("test", test_handler);
		http_cerver_route_register (http_cerver, test_route);

		// /hola
		HttpRoute *hola_route = http_route_create ("hola", hola_handler);
		http_cerver_route_register (http_cerver, hola_route);

		// /adios
		HttpRoute *adios_route = http_route_create ("adios", adios_handler);
		http_cerver_route_register (http_cerver, adios_route);

		if (cerver_start (web_cerver)) {
			char *s = c_string_create ("Failed to start %s!",
				web_cerver->info->name->str);
			if (s) {
				cerver_log_error (s);
				free (s);
			}

			cerver_delete (web_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (web_cerver);
	}

	return 0;

}

#pragma endregion