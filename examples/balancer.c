#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/balancer.h>
#include <cerver/cerver.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

static Balancer *load_balancer = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {
	
	if (load_balancer) {
		// TODO:
        // balancer_stats_print (client_cerver, true, true);
		// cerver_teardown (client_cerver);
	} 

	exit (0);

}

#pragma endregion

#pragma region start

int main (void) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, end);

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Load Balancer Example");
	printf ("\n");
	cerver_log_debug ("Simple Round Robin Load Balancer");
	printf ("\n");

	load_balancer = balancer_create (BALANCER_TYPE_ROUND_ROBIN, 7000, 10, 2);
	if (load_balancer) {

	}

	else {
        cerver_log_error ("Failed to create load balancer!");
	}

	return 0;

}

#pragma endregion