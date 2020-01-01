#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <bson/bson.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"
#include "cerver/collections/dllist.h"

#include "cerver/cerver.h"
#include "cerver/handler.h"

#include "cerver/http/parser.h"
#include "cerver/http/json.h"
#include "cerver/http/response.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

Cerver *web_cerver = NULL;

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {
	
	if (web_cerver) {
		cerver_stats_print (web_cerver);
		cerver_teardown (web_cerver);
	} 

	exit (0);

}

static void app_main_handler (ReceiveHandle *data, DoubleList *pairs) {

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
			estring *test = estring_new ("Magic I/O works!");
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
			http_response_send_to_socket (res, data->sock_fd);
			http_respponse_delete (res);
		}

		// after the performed action, close the client socket
		Client *client = client_get_by_sock_fd (data->cerver, data->sock_fd);
		client_disconnect (client);
		client_drop (data->cerver, client);
	}

}

void app_handle_recieved_buffer (void *rcvd_buffer_data) {

	if (rcvd_buffer_data) {
		ReceiveHandle *data = (ReceiveHandle *) rcvd_buffer_data;

		if (data->buffer && (data->buffer_size > 0)) {
			char *method, *path;
			int pret, minor_version;
			struct phr_header headers[100];
			size_t buflen = 0, prevbuflen = 0, method_len, path_len, num_headers;
			ssize_t rret;

			prevbuflen = buflen;
			buflen += rret;
			/* parse the request */
			num_headers = sizeof (headers) / sizeof (headers[0]);
			pret = phr_parse_request (data->buffer, data->buffer_size, (const char **) &method, &method_len, (const char **) &path, &path_len,
				&minor_version, headers, &num_headers, prevbuflen);
			if (pret > 0) {
				char str[50];
				snprintf (str, 50, "%.*s", (int) path_len, path);
				printf ("%s\n", str);
				char *query = http_strip_path_from_query (str);
				printf ("%s\n", query);
				
				int count = 0;
				const char *first = query;
				const char *last = first + strlen (query);
				DoubleList *pairs = http_parse_query_into_pairs (query, last);

				// now we can handle the action and its values
				magic_main_handler (data, pairs);

				dlist_delete (pairs);
			}
		}

		// DONT need to call! Cerver calls this method for you
		// receive_handle_delete (data);
	}

}

int main (int argc, char **argv) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	web_cerver = cerver_create (WEB_CERVER, "web-cerver", 7010, PROTOCOL_TCP, false, 2, 2000);
	if (web_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (web_cerver, 4096);
		cerver_set_thpool_n_threads (web_cerver, 4);

		cerver_set_handle_recieved_buffer (web_cerver, app_handle_recieved_buffer);

		if (!cerver_start (web_cerver)) {
			cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE,
				"Failed to start magic io cerver!");
		}
	}

	else {
		cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
			"Failed to create magic io cerver!");
	}

	return 0;

}