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
void main_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	if (http_response_render_file (http_receive, "./examples/web/public/echo.html")) {
		cerver_log_error ("Failed to send ./examples/web/public/echo.html");
	}

}

void test_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		(http_status) 200, "Test route works!"
	);

	if (res) {
		http_response_print (res);
		http_response_send (res, http_receive);
		http_respponse_delete (res);
	}

}

void echo_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		(http_status) 200, "Echo route works!"
	);

	if (res) {
		http_response_print (res);
		http_response_send (res, http_receive);
		http_respponse_delete (res);
	}

}

void echo_handler_on_open (const HttpReceive *http_receive) {

	printf ("echo_handler_on_open ()\n");

}

void echo_handler_on_close (
	const HttpReceive *http_receive, const char *reason
) {

	printf ("echo_handler_on_close ()\n");

}

void echo_handler_on_message (
	const HttpReceive *http_receive,
	const char *msg, const size_t msg_len
) {

	printf ("echo_handler_on_message ()\n");

	printf ("message[%ld]: %.*s\n", msg_len, (int) msg_len, msg);

	http_web_sockets_send (
		http_receive,
		msg, msg_len
	);

	sleep (5);

	char *message = { "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Porttitor eget dolor morbi non arcu risus quis. Mauris rhoncus aenean vel elit scelerisque mauris pellentesque pulvinar. Aliquet risus feugiat in ante metus dictum at. Massa enim nec dui nunc mattis. Massa eget egestas purus viverra. Euismod quis viverra nibh cras pulvinar mattis. Curabitur gravida arcu ac tortor dignissim convallis. Sit amet aliquam id diam maecenas ultricies mi eget. Mi quis hendrerit dolor magna eget. Nibh mauris cursus mattis molestie a. Purus in massa tempor nec feugiat nisl pretium fusce. Turpis massa sed elementum tempus egestas sed sed risus pretium. Etiam sit amet nisl purus in. Ac ut consequat semper viverra nam libero. Quam quisque id diam vel. Mattis vulputate enim nulla aliquet porttitor lacus luctus. Volutpat diam ut venenatis tellus in metus vulputate eu scelerisque. Vitae nunc sed velit dignissim sodales ut eu" };

	http_web_sockets_send (
		http_receive,
		message, strlen (message)
	);

	sleep (5);

	char *test = { "Diam vel quam elementum pulvinar etiam non. Auctor elit sed vulputate mi sit amet mauris commodo. Neque ornare aenean euismod elementum. In pellentesque massa placerat duis ultricies lacus sed. Velit aliquet sagittis id consectetur purus ut faucibus. Vitae ultricies leo integer malesuada nunc vel. Metus dictum at tempor commodo ullamcorper a. Amet luctus venenatis lectus magna. Eget nulla facilisi etiam dignissim diam quis enim. Ultricies mi quis hendrerit dolor magna eget est lorem. Sem et tortor consequat id porta nibh venenatis cras. Aliquam faucibus purus in massa tempor nec feugiat. Id interdum velit laoreet id. Volutpat sed cras ornare arcu dui vivamus arcu. Consectetur a erat nam at lectus. In nulla posuere sollicitudin aliquam. Malesuada fames ac turpis egestas. Malesuada fames ac turpis egestas maecenas pharetra convallis. Sapien faucibus et molestie ac." };

	http_web_sockets_send (
		http_receive,
		test, strlen (test)
	);

}

void chat_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		(http_status) 200, "Chat route works!"
	);
	if (res) {
		http_response_print (res);
		http_response_send (res, http_receive);
		http_respponse_delete (res);
	}

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

	web_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"web-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);

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

		// /test
		HttpRoute *test_route = http_route_create (REQUEST_METHOD_GET, "test", test_handler);
		http_cerver_route_register (http_cerver, test_route);

		// /echo
		HttpRoute *echo_route = http_route_create (REQUEST_METHOD_GET, "echo", echo_handler);
		http_route_set_modifier (echo_route, HTTP_ROUTE_MODIFIER_WEB_SOCKET);
		http_route_set_ws_on_open (echo_route, echo_handler_on_open);
		http_route_set_ws_on_message (echo_route, echo_handler_on_message);
		http_route_set_ws_on_close (echo_route, echo_handler_on_close);
		http_cerver_route_register (http_cerver, echo_route);

		// /chat
		HttpRoute *chat_route = http_route_create (REQUEST_METHOD_GET, "chat", chat_handler);
		http_route_set_modifier (chat_route, HTTP_ROUTE_MODIFIER_WEB_SOCKET);
		http_cerver_route_register (http_cerver, chat_route);

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