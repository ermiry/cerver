#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/auth.h>
#include <cerver/admin.h>
#include <cerver/events.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

typedef enum AppRequest {

	TEST_MSG		= 0

} AppRequest;

typedef struct Credentials {

	char username[64];
	char password[64];

} Credentials;

static Cerver *my_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {
	
	if (my_cerver) {
		cerver_stats_print (my_cerver, true, true);
		cerver_teardown (my_cerver);
	}

	cerver_end ();

	exit (0);

}

#pragma endregion

#pragma region handler

static void handle_test_request (Packet *packet) {

	if (packet) {
		cerver_log_debug ("Got a test message from ADMIN. Sending another one back...");
		
		Packet *test_packet = packet_generate_request (PACKET_TYPE_APP, TEST_MSG, NULL, 0);
		if (test_packet) {
			packet_set_network_values (test_packet, NULL, NULL, packet->connection, NULL);
			size_t sent = 0;
			if (packet_send (test_packet, 0, &sent, false)) 
				cerver_log_error ("Failed to send test packet to client!");

			else {
				// printf ("Response packet sent: %ld\n", sent);
			}
			
			packet_delete (test_packet);
		}
	}

}

static void admin_handler (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;
		switch (packet->header->request_type) {
			case TEST_MSG: handle_test_request (packet); break;

			default: 
				cerver_log (LOG_TYPE_WARNING, LOG_TYPE_PACKET, "Got an unknown app request.");
				break;
		}
	}

}

#pragma endregion

#pragma region auth

static u8 my_auth_method_username (AuthMethod *auth_method, const char *username) {

	u8 retval = 1;

	if (username) {
		if (strlen (username) > 0) {
			if (!strcmp (username, "ermiry")) {
				retval = 0;		// success
			}

			else {
				cerver_log_error ("my_auth_method () - Username does not exists!");
				auth_method->error_message = str_new ("Username does not exists!");
			}
		}

		else {
			cerver_log_error ("my_auth_method () - Username is required!");
			auth_method->error_message = str_new ("Username is required!");
		}
	}

	return retval;

}

static u8 my_auth_method_password (AuthMethod *auth_method, const char *password) {
	
	u8 retval = 1;

	if (password) {
		if (strlen (password) > 0) {
			if (!strcmp (password, "hola12")) {
				retval = 0;		// success auth
			}

			else {
				cerver_log_error ("my_auth_method () - Password is incorrect!");
				auth_method->error_message = str_new ("Password is incorrect!");
			}
		}

		else {
			cerver_log_error ("my_auth_method () - Password is required!");
			auth_method->error_message = str_new ("Password is required!");
		}
	}

	return retval;

}

static u8 my_auth_method (void *auth_method_ptr)  {

	u8 retval = 1;

	if (auth_method_ptr) {
		AuthMethod *auth_method = (AuthMethod *) auth_method_ptr;
		if (auth_method->auth_data) {
			if (auth_method->auth_data->auth_data_size >= sizeof (Credentials)) {
				Credentials *credentials = (Credentials *) auth_method->auth_data->auth_data;

				printf ("\nReceived credentials: \n");
				printf ("\tUsername: %s\n", credentials->username);
				printf ("\tPassword: %s\n", credentials->password);

				if (!my_auth_method_username (auth_method, credentials->username)) {
					if (!my_auth_method_password (auth_method, credentials->password)) {
						retval = 0;		// success
					}
				}
			}

			else {
				cerver_log_error ("my_auth_method () - auth data is of wrong size!");
				auth_method->error_message = str_new ("Missing auth data!");
			}
		}

		else {
			cerver_log_error ("my_auth_method () - auth packet does not have auth data!");
		}
	}

	else {
		cerver_log_error ("my_auth_method () - NULL auth_method_ptr!");
	}

	return retval;

}

#pragma endregion

#pragma region events

static void *on_hold_connected (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_CERVER,
			"Conenction %d is on hold in cerver %s!\n",
			event_data->connection->socket->sock_fd,
			event_data->cerver->info->name->str
		);
	}

	return NULL;

}

static void *on_hold_disconnected (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_NONE,
			"An on hold connection disconnected in cerver %s!\n", 
			event_data->cerver->info->name->str
		);
	}

	return NULL;

}

static void *on_hold_drop (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_NONE,
			"An on hold connection was dropped in cerver %s!\n", 
			event_data->cerver->info->name->str
		);
	}

	return NULL;

}

static void *on_admin_failed_auth (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_ADMIN,
			"Admin failed to authenticate connection with sock fd %d to cerver %s!\n",
			event_data->connection->socket->sock_fd, 
			event_data->cerver->info->name->str
		);
	}

	return NULL;

}

static void *on_admin_connected (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_ADMIN,
			"Admin with client %ld authenticated connection with sock fd %d to cerver %s!\n",
			event_data->client->id,
			event_data->connection->socket->sock_fd, 
			event_data->cerver->info->name->str
		);
	}

	return NULL;

}

static void *on_admin_new_connection (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_ADMIN,
			"Admin with client %ld new connection with sock fd %d to cerver %s!\n",
			event_data->client->id,
			event_data->connection->socket->sock_fd, 
			event_data->cerver->info->name->str
		);
	}

	return NULL;

}

static void *on_admin_close_connection (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_ADMIN,
			"An admin closed a connection to cerver %s!\n",
			event_data->cerver->info->name->str
		);
	}

	return NULL;

}

static void *on_admin_disconnected (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_ADMIN,
			"An admin disconnected from cerver %s!\n",
			event_data->cerver->info->name->str
		);
	}

	return NULL;

}

static void *on_admin_dropped (void *event_data_ptr) {

	if (event_data_ptr) {
		CerverEventData *event_data = (CerverEventData *) event_data_ptr;

		printf ("\n");
		cerver_log (
			LOG_TYPE_EVENT, LOG_TYPE_ADMIN,
			"An admin was dropped from cerver %s!\n",
			event_data->cerver->info->name->str
		);
	}

	return NULL;

}

#pragma endregion

#pragma region update

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

void admin_update_thread (void *data) {

	cerver_log_msg ("Updating multiple times x second!\n");

}

void admin_update_internal_thread (void *data) {
	
	cerver_log_msg ("Updating every second!\n");

}

#pragma GCC diagnostic pop

#pragma endregion

#pragma region main

int main (void) {

	srand ((unsigned int) time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGKILL, end);

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_log_line_break ();
	cerver_version_print_full ();
	cerver_log_line_break ();

	cerver_log_debug ("Admin Example");
	cerver_log_line_break ();
	cerver_log_debug ("Cerver with admin capabilities");
	cerver_log_line_break ();

	my_cerver = cerver_create (
		CERVER_TYPE_CUSTOM,
		"my-cerver",
		7000,
		PROTOCOL_TCP,
		false,
		2
	);

	if (my_cerver) {
		cerver_set_welcome_msg (my_cerver, "Welcome - Admin Example");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, 4096);
		cerver_set_thpool_n_threads (my_cerver, 4);

		cerver_set_reusable_address_flags (my_cerver, true);

		cerver_set_handler_type (my_cerver, CERVER_HANDLER_TYPE_POLL);
		cerver_set_poll_time_out (my_cerver, 2000);

		/*** cerver auth configuration ***/
		// authentication needs to be enabled to handle admins
		cerver_set_auth (my_cerver, 2, NULL);

		/*** admin configuration ***/
		cerver_set_admin_enable (my_cerver);
		admin_cerver_set_authenticate (my_cerver->admin, my_auth_method);

		// admin_cerver_set_update (my_cerver->admin, admin_update_thread, NULL, NULL, 10);
		// admin_cerver_set_update_interval (my_cerver->admin, admin_update_internal_thread, NULL, NULL, 1);

		Handler *admin_app_handler = handler_create (admin_handler);
		handler_set_direct_handle (admin_app_handler, true);
		admin_cerver_set_app_handlers (my_cerver->admin, admin_app_handler, NULL);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_ON_HOLD_CONNECTED,
			on_hold_connected, NULL, NULL,
			false, false
		);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_ON_HOLD_DISCONNECTED,
			on_hold_disconnected, NULL, NULL,
			false, false
		);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_ON_HOLD_DROPPED,
			on_hold_drop, NULL, NULL,
			false, false
		);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_ADMIN_FAILED_AUTH,
			on_admin_failed_auth, NULL, NULL,
			false, false
		);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_ADMIN_CONNECTED,
			on_admin_connected, NULL, NULL,
			false, false
		);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_ADMIN_NEW_CONNECTION,
			on_admin_new_connection, NULL, NULL,
			false, false
		);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_ADMIN_CLOSE_CONNECTION,
			on_admin_close_connection, NULL, NULL,
			false, false
		);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_ADMIN_DISCONNECTED,
			on_admin_disconnected, NULL, NULL,
			false, false
		);

		cerver_event_register (
			my_cerver, 
			CERVER_EVENT_ADMIN_DROPPED,
			on_admin_dropped, NULL, NULL,
			false, false
		);

		if (cerver_start (my_cerver)) {
			cerver_log_error (
				"Failed to start %s!",
				my_cerver->info->name->str
			);

			cerver_delete (my_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (my_cerver);
	}

	cerver_end ();

	return 0;

}

#pragma endregion