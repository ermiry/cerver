#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <bson/bson.h>

#include <cerver/types/types.h>
#include <cerver/types/estring.h>
#include <cerver/collections/dlist.h>

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