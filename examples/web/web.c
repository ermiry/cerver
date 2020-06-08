#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <bson/bson.h>

#include <cerver/types/types.h>
#include <cerver/types/estring.h>
#include <cerver/collections/dllist.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/handler.h>

#include <cerver/http/parser.h>
#include <cerver/http/json.h>
#include <cerver/http/response.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

Cerver *web_cerver = NULL;

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {
	
	if (web_cerver) {
		cerver_stats_print (web_cerver);
		cerver_teardown (web_cerver);
	} 

	exit (0);

}

static void app_main_handler (ReceiveHandle *receive, DoubleList *pairs) {

	if (pairs) {
		HttpResponse *res = NULL;
		KeyValuePair *kvp = NULL;

		const char *action = NULL;
		for (ListElement *le = dlist_start (pairs); le; le = le->next) {
			kvp = (KeyValuePair *) le->data;
			if (!strcmp (kvp->key, "action")) {
				action = kvp->value;
				break;
			}
		}

		// handle the action
		if (!strcmp (action, "test")) {
			estring *test = estring_new ("Web cerver works!");
			JsonKeyValue *jkvp = json_key_value_create ("msg", test, VALUE_TYPE_STRING);
			size_t json_len;
			char *json = json_create_with_one_pair (jkvp, &json_len);
			// json_key_value_delete (jkvp);
			res = http_response_create (200, NULL, 0, json, json_len);
			bson_free (json);        // we copy the data into the response
		}
		
		else {
			char *status = c_string_create ("Got unkown action %s", action);
			if (status) {
				cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, status);
				free (status);
			}
		}

		if (res) {
			// send the response to the client
			http_response_compile (res);
			printf ("Response: %s\n", res->res);
			http_response_send_to_socket (res, receive->socket->sock_fd);
			http_respponse_delete (res);
		}

		// after the performed action, close the client socket
		Client *client = client_get_by_sock_fd (receive->cerver, receive->socket->sock_fd);
		client_remove_connection_by_sock_fd (receive->cerver, 
			client, receive->socket->sock_fd);
	}

}

void app_handle_received_buffer (void *rcvd_buffer_data) {

	if (rcvd_buffer_data) {
		ReceiveHandle *receive = (ReceiveHandle *) rcvd_buffer_data;

		pthread_mutex_lock (receive->socket->mutex);

		if (receive->buffer && (receive->buffer_size > 0)) {
			char *method, *path;
			int pret, minor_version;
			struct phr_header headers[128];
			size_t buflen = 0, prevbuflen = 0, method_len = 0, path_len = 0, num_headers = 0;
			// ssize_t rret;

			prevbuflen = buflen;
			// buflen += rret;
			/* parse the request */
			num_headers = sizeof (headers) / sizeof (headers[0]);
			pret = phr_parse_request (receive->buffer, receive->buffer_size, (const char **) &method, &method_len, (const char **) &path, &path_len,
				&minor_version, headers, &num_headers, prevbuflen);
			if (pret > 0) {
				char *str = c_string_create ("%.*s", (int) path_len, path);
				if (str) {
					// printf ("%s\n", str);
					char *query = http_strip_path_from_query (str);
					if (query) {
						// printf ("%s\n", query);
						// int count = 0;
						const char *first = query;
						const char *last = first + strlen (query);
						DoubleList *pairs = http_parse_query_into_pairs (query, last);
						// KeyValuePair *kvp = NULL;
						// for (ListElement *le = dlist_start (pairs); le; le = le->next) {
						// 	kvp = (KeyValuePair *) le->data;
						// 	printf ("key: %s - value: %s\n", kvp->key, kvp->value);
						// }

						// now we can handle the action and its values
						app_main_handler (receive, pairs);

						dlist_delete (pairs);

						free (query);
					}

					free (str);
				}
			}
		}

		pthread_mutex_unlock (receive->socket->mutex);

		// In latest cerver 1.4 you need to call this here!
		receive_handle_delete (receive);
	}

}

int main (int argc, char **argv) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Simple Web Cerver Example");
	printf ("\n");

	web_cerver = cerver_create (WEB_CERVER, "web-cerver", 7010, PROTOCOL_TCP, false, 2, 1000);
	if (web_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (web_cerver, 4096);
		cerver_set_thpool_n_threads (web_cerver, 4);

		cerver_set_handle_recieved_buffer (web_cerver, app_handle_received_buffer);

		if (!cerver_start (web_cerver)) {
			cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE,
				"Failed to start cerver!");
		}
	}

	else {
		cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
			"Failed to create cerver!");
	}

	return 0;

}