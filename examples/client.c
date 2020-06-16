#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>

#include <cerver/threads/thread.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

typedef enum AppRequest {

	TEST_MSG		= 0,

	GET_MSG			= 1

} AppRequest;

// message from the cerver
typedef struct AppMessage {

	unsigned int len;
	char message[128];

} AppMessage;

static Cerver *client_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {
	
	if (client_cerver) {
		cerver_stats_print (client_cerver);
		cerver_teardown (client_cerver);
	} 

	exit (0);

}

#pragma endregion

#pragma region handler

static void handle_test_request (Packet *packet) {

	if (packet) {
		cerver_log_debug ("Got a test message from client. Sending another one back...");
		
		Packet *test_packet = packet_generate_request (APP_PACKET, TEST_MSG, NULL, 0);
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

static void cerver_app_handler (void *data) {

	if (data) {
		Packet *packet = (Packet *) data;
		if (packet->data_size >= sizeof (RequestData)) {
			RequestData *req = (RequestData *) (packet->data);

			switch (req->type) {
				case TEST_MSG: handle_test_request (packet); break;

				default: 
					cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, "Got an unknown app request.");
					break;
			}
		}
	}

}

#pragma endregion

#pragma region client

static void client_app_handler (void *packet_ptr) {

	if (packet_ptr) {
        Packet *packet = (Packet *) packet_ptr;
        if (packet) {
            if (packet->data_size >= sizeof (RequestData)) {
                RequestData *req = (RequestData *) (packet->data);

                switch (req->type) {
                    case TEST_MSG: cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Got a test message from cerver!"); break;

                    case GET_MSG: {
                        char *end = (char *) packet->data;
                        end += sizeof (RequestData);

                        AppMessage *app_message = (AppMessage *) end;
                        printf ("%s - %d\n", app_message->message, app_message->len);
                    } break;

                    default: 
                        cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, "Got an unknown app request.");
                        break;
                }
            }
        }
    }

}

static Connection *cerver_client_connect (Client *client) {

    Connection *connection = NULL;

    if (client) {
        connection = client_connection_create (client, "127.0.0.1", 8007, PROTOCOL_TCP, false);
        if (connection) {
            connection_set_max_sleep (connection, 30);

            if (!client_connect_to_cerver (client, connection)) {
                cerver_log_msg (stdout, LOG_SUCCESS, LOG_NO_TYPE, "Connected to cerver!");
            }

            else {
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, "Failed to connect to cerver!");
            }
        }
    }

    return connection;

}

static int request_message (Client *client, Connection *connection) {

    int retval = 1;

    // manually create a packet to send
    Packet *packet = packet_new ();
    if (packet) {
        size_t packet_len = sizeof (PacketHeader) + sizeof (RequestData);
        packet->packet = malloc (packet_len);
        packet->packet_size = packet_len;

        char *end = (char *) packet->packet;
        PacketHeader *header = (PacketHeader *) end;
        header->protocol_id = packets_get_protocol_id ();
        header->protocol_version = packets_get_protocol_version ();
        header->packet_type = APP_PACKET;
        header->packet_size = packet_len;
        header->handler_id = 0;

        end += sizeof (PacketHeader);
        RequestData *req_data = (RequestData *) end;
        req_data->type = GET_MSG;

        printf ("Requesting to cerver...\n");
        if (client_request_to_cerver (client, connection, packet)) {
            cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, "Failed to send message request to cerver");
        }
        printf ("Request has ended\n");

        packet_delete (packet);
    }

    return retval;

}

// create a new client that will connect to a cerver & make a test request
static void *cerver_client_request (void *args) {

    Client *client = client_create ();
    if (client) {
        client_set_name (client, "test-client");

        Handler *app_handler = handler_create (client_app_handler);
		handler_set_direct_handle (app_handler, true);
        client_set_app_handlers (client, app_handler, NULL);

        // wait 2 seconds before connecting to cerver
        sleep (2);
        Connection *connection = cerver_client_connect (client);
        
        // send 1 request message every second
        for (unsigned int i = 0; i < 10; i++) {
            request_message (client, connection);

            sleep (1);
        }

        // destroy the client and its connection
        if (!client_teardown (client)) {
            cerver_log_success ("client_teardown ()");
        }

        else {
            cerver_log_error ("client_teardown () has failed!");
        }
    }

    return NULL;

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

	cerver_log_debug ("Cerver Client Example");
	printf ("\n");
	cerver_log_debug ("Cerver creates a new client that will use to make requests to another cerver");
	printf ("\n");

	client_cerver = cerver_create (CUSTOM_CERVER, "client-cerver", 7001, PROTOCOL_TCP, false, 2, 2000);
	if (client_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (client_cerver, 4096);
		cerver_set_thpool_n_threads (client_cerver, 4);

		Handler *app_handler = handler_create (cerver_app_handler);
		handler_set_direct_handle (app_handler, true);
		cerver_set_app_handlers (client_cerver, app_handler, NULL);

        pthread_t client_thread = 0;
        thread_create_detachable (&client_thread, cerver_client_request, NULL);

		if (cerver_start (client_cerver)) {
			char *s = c_string_create ("Failed to start %s!",
				client_cerver->info->name->str);
			if (s) {
				cerver_log_error (s);
				free (s);
			}

			cerver_delete (client_cerver);
		}
	}

	else {
		char *s = c_string_create ("Failed to create %s!",
			client_cerver->info->name->str);
		if (s) {
			cerver_log_error (s);
			free (s);
		}

		cerver_delete (client_cerver);
	}

	return 0;

}

#pragma endregion