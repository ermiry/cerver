#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include "cerver/cerver.h"
#include "cerver/game/game.h"
#include "cerver/game/gametype.h"
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

void my_app_handle_recieved_buffer (void *rcvd_buffer_data) {

    // method to handle the receive buffer from the main cerver socket

}

int main (int argc, char **argv) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	web_cerver = cerver_create (WEB_CERVER, "web-cerver", 7010, PROTOCOL_TCP, false, 2, 2000);
	if (web_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (web_cerver, 2048);
		cerver_set_thpool_n_threads (web_cerver, 4);

		cerver_set_handle_recieved_buffer (web_cerver, my_app_handle_recieved_buffer);

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