#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"

#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/client/connect.h"
#include "cerver/client/events.h"
#include "cerver/client/internal.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/handler/receive.h"
#include "cerver/network/network.h"
#include "cerver/network/udp.h"
#include "cerver/packets/packets.h"

#include "cerver/game/game.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region events

u8 client_event_unregister (Client *client, ClientEventType event_type);

// an event type indexes the events of the client
static inline bool client_event_type_is_valid (const ClientEventType event_type) {

	return (unsigned int) event_type < CLIENT_MAX_EVENTS;

}

// get the description for the current event type
const char *client_event_type_description (const ClientEventType type) {

	switch (type) {
		#define XX(num, name, description) case CLIENT_EVENT_##name: return #description;
		CLIENT_EVENT_MAP(XX)
		#undef XX
	}

	return client_event_type_description (CLIENT_EVENT_UNKNOWN);

}

static ClientEventData *client_event_data_new (void) {

	ClientEventData *event_data = (ClientEventData *) malloc (sizeof (ClientEventData));
	if (event_data) {
		event_data->client = NULL;
		event_data->connection = NULL;

		event_data->response_data = NULL;
		event_data->delete_response_data = NULL;

		event_data->owns_response_data = false;

		event_data->action_args = NULL;
		event_data->delete_action_args = NULL;
		event_data->owns_action_args = false;
	}

	return event_data;

}

void client_event_data_delete (ClientEventData *event_data) {

	if (event_data) {
		if (event_data->owns_response_data && event_data->response_data) {
			if (event_data->delete_response_data) event_data->delete_response_data (event_data->response_data);
			else free (event_data->response_data);
		}

		if (event_data->owns_action_args && event_data->action_args && event_data->delete_action_args) {
			event_data->delete_action_args (event_data->action_args);
		}

		free (event_data);
	}

}

// the event is dropped while its action can still run in its thread,
// so the arguments and the response that the event releases go with the data of this trigger
static void client_event_data_take (ClientEventData *event_data, ClientEvent *event) {

	event_data->owns_action_args = true;
	event->work_args = NULL;

	if (!event_data->owns_response_data && event->response_data && event->delete_response_data) {
		event_data->owns_response_data = true;
		event->response_data = NULL;
	}

}

static ClientEventData *client_event_data_create (
	const Client *client, const Connection *connection,
	ClientEvent *event
) {

	ClientEventData *event_data = client_event_data_new ();
	if (event_data) {
		event_data->client = client;
		event_data->connection = connection;

		event_data->response_data = event->response_data;
		event_data->delete_response_data = event->delete_response_data;

		event_data->action_args = event->work_args;
		event_data->delete_action_args = event->delete_action_args;
	}

	return event_data;

}

static ClientEvent *client_event_new (void) {

	ClientEvent *event = (ClientEvent *) malloc (sizeof (ClientEvent));
	if (event) {
		event->type = CLIENT_EVENT_NONE;

		event->create_thread = false;
		event->drop_after_trigger = false;

		event->request_type = 0;
		event->response_data = NULL;
		event->delete_response_data = NULL;

		event->work = NULL;
		event->work_args = NULL;
		event->delete_action_args = NULL;
	}

	return event;

}

void client_event_delete (void *ptr) {

	if (ptr) {
		ClientEvent *event = (ClientEvent *) ptr;

		if (event->response_data) {
			if (event->delete_response_data)
				event->delete_response_data (event->response_data);
		}

		if (event->work_args) {
			if (event->delete_action_args)
				event->delete_action_args (event->work_args);
		}

		free (event);
	}

}

// registers an action to be triggered when the specified event occurs
// if there is an existing action registered to an event, it will be overrided
// a newly allocated ClientEventData structure will be passed to your method
// that should be free using the client_event_data_delete () method
// returns 0 on success, 1 on error
u8 client_event_register (
	Client *client,
	const ClientEventType event_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
) {

	u8 retval = 1;

	if (client && client_event_type_is_valid (event_type)) {
		ClientEvent *event = client_event_new ();
		if (event) {
			event->type = event_type;

			event->create_thread = create_thread;
			event->drop_after_trigger = drop_after_trigger;

			event->work = work;
			event->work_args = work_args;
			event->delete_action_args = delete_action_args;

			// search if there is an action already registred for that event and remove it
			(void) client_event_unregister (client, event_type);

			client->events[event_type] = event;

			retval = 0;
		}
	}

	return retval;

}

// unregister the action associated with an event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error or if event is NOT registered
u8 client_event_unregister (
	Client *client, const ClientEventType event_type
) {

	u8 retval = 1;

	if (client && client_event_type_is_valid (event_type)) {
		if (client->events[event_type]) {
			client_event_delete (client->events[event_type]);
			client->events[event_type] = NULL;

			retval = 0;
		}
	}

	return retval;

}

void client_event_set_response (
	Client *client,
	const ClientEventType event_type,
	void *response_data, Action delete_response_data
) {

	if (client && client_event_type_is_valid (event_type)) {
		ClientEvent *event = client->events[event_type];
		if (event) {
			event->response_data = response_data;
			event->delete_response_data = delete_response_data;
		}
	}

}

// triggers all the actions that are registred to an event
void client_event_trigger (
	const ClientEventType event_type,
	const Client *client, const Connection *connection
) {

	if (client && client_event_type_is_valid (event_type)) {
		ClientEvent *event = client->events[event_type];
		if (event) {
			// trigger the action
			if (event->work) {
				ClientEventData *event_data = client_event_data_create (client, connection, event);
				if (event_data && event->drop_after_trigger) {
					client_event_data_take (event_data, event);
				}

				// the thread is counted, so client_teardown () waits for it
				if (event->create_thread) {
					(void) thread_group_create (
						(ThreadGroup *) &client->event_threads,
						event->work, event_data
					);
				}

				else {
					(void) event->work (event_data);
				}

				if (event->drop_after_trigger) {
					(void) client_event_unregister ((Client *) client, event_type);
				}
			}
		}
	}

}

static void client_event_response_delete (void *response, Action delete_response) {

	if (response) {
		if (delete_response) delete_response (response);
		else free (response);
	}

}

// triggers the event with a response that only this trigger owns,
// the event data releases it, or it is released here without an action
void client_event_trigger_with_response (
	const ClientEventType event_type,
	const Client *client, const Connection *connection,
	void *response, Action delete_response
) {

	ClientEvent *event = (client && client_event_type_is_valid (event_type)) ?
		client->events[event_type] : NULL;
	ClientEventData *event_data = (event && event->work) ?
		client_event_data_create (client, connection, event) : NULL;

	if (event_data) {
		event_data->response_data = response;
		event_data->delete_response_data = delete_response;
		event_data->owns_response_data = true;

		if (event->drop_after_trigger) {
			client_event_data_take (event_data, event);
		}

		if (event->create_thread) {
			(void) thread_group_create (
				(ThreadGroup *) &client->event_threads,
				event->work, event_data
			);
		}

		else {
			(void) event->work (event_data);
		}

		if (event->drop_after_trigger) {
			(void) client_event_unregister ((Client *) client, event_type);
		}
	}

	else {
		client_event_response_delete (response, delete_response);
	}

}

#pragma endregion

#pragma region errors

u8 client_error_unregister (Client *client, const ClientErrorType error_type);

// an error type indexes the errors of the client
static inline bool client_error_type_is_valid (const ClientErrorType error_type) {

	return (unsigned int) error_type < CLIENT_MAX_ERRORS;

}

// get the description for the current error type
const char *client_error_type_description (const ClientErrorType type) {

	switch (type) {
		#define XX(num, name, description) case CLIENT_ERROR_##name: return #description;
		CLIENT_ERROR_MAP(XX)
		#undef XX
	}

	return client_error_type_description (CLIENT_ERROR_UNKNOWN);

}

static ClientErrorData *client_error_data_new (void) {

	ClientErrorData *error_data = (ClientErrorData *) malloc (sizeof (ClientErrorData));
	if (error_data) {
		error_data->client = NULL;
		error_data->connection = NULL;

		error_data->action_args = NULL;
		error_data->delete_action_args = NULL;
		error_data->owns_action_args = false;

		error_data->error_message = NULL;
	}

	return error_data;

}

void client_error_data_delete (ClientErrorData *error_data) {

	if (error_data) {
		str_delete (error_data->error_message);

		if (error_data->owns_action_args && error_data->action_args && error_data->delete_action_args) {
			error_data->delete_action_args (error_data->action_args);
		}

		free (error_data);
	}

}

static ClientErrorData *client_error_data_create (
	const Client *client, const Connection *connection,
	void *args,
	const char *error_message
) {

	ClientErrorData *error_data = client_error_data_new ();
	if (error_data) {
		error_data->client = client;
		error_data->connection = connection;

		error_data->action_args = args;

		error_data->error_message = error_message ? str_new (error_message) : NULL;
	}

	return error_data;

}

static ClientError *client_error_new (void) {

	ClientError *client_error = (ClientError *) malloc (sizeof (ClientError));
	if (client_error) {
		client_error->type = CLIENT_ERROR_NONE;

		client_error->create_thread = false;
		client_error->drop_after_trigger = false;

		client_error->work = NULL;
		client_error->work_args = NULL;
		client_error->delete_action_args = NULL;
	}

	return client_error;

}

void client_error_delete (void *client_error_ptr) {

	if (client_error_ptr) {
		ClientError *client_error = (ClientError *) client_error_ptr;

		if (client_error->work_args) {
			if (client_error->delete_action_args)
				client_error->delete_action_args (client_error->work_args);
		}

		free (client_error_ptr);
	}

}

// registers an action to be triggered when the specified error occurs
// if there is an existing action registered to an error, it will be overrided
// a newly allocated ClientErrorData structure will be passed to your method
// that should be free using the client_error_data_delete () method
// returns 0 on success, 1 on error
u8 client_error_register (
	Client *client,
	const ClientErrorType error_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
) {

	u8 retval = 1;

	if (client && client_error_type_is_valid (error_type)) {
		ClientError *error = client_error_new ();
		if (error) {
			error->type = error_type;

			error->create_thread = create_thread;
			error->drop_after_trigger = drop_after_trigger;

			error->work = work;
			error->work_args = work_args;
			error->delete_action_args = delete_action_args;

			// search if there is an action already registred for that error and remove it
			(void) client_error_unregister (client, error_type);

			client->errors[error_type] = error;

			retval = 0;
		}
	}

	return retval;

}

// unregisters the action associated with the error types
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error or if error is NOT registered
u8 client_error_unregister (Client *client, const ClientErrorType error_type) {

	u8 retval = 1;

	if (client && client_error_type_is_valid (error_type)) {
		if (client->errors[error_type]) {
			client_error_delete (client->errors[error_type]);
			client->errors[error_type] = NULL;

			retval = 0;
		}
	}

	return retval;

}

// triggers all the actions that are registred to an error
// returns 0 on success, 1 on error
u8 client_error_trigger (
	const ClientErrorType error_type,
	const Client *client, const Connection *connection,
	const char *error_message
) {

	u8 retval = 1;

	if (client && client_error_type_is_valid (error_type)) {
		ClientError *error = client->errors[error_type];
		if (error) {
			// trigger the action
			if (error->work) {
				ClientErrorData *error_data = client_error_data_create (
					client, connection, error->work_args, error_message
				);

				// the error is dropped while its action can still run in its thread,
				// so the arguments go with the data of this trigger
				if (error_data && error->drop_after_trigger) {
					error_data->delete_action_args = error->delete_action_args;
					error_data->owns_action_args = true;
					error->work_args = NULL;
				}

				// the thread is counted, so client_teardown () waits for it
				if (error->create_thread) {
					retval = thread_group_create (
						(ThreadGroup *) &client->event_threads,
						error->work, error_data
					);
				}

				else {
					(void) error->work (error_data);

					retval = 0;
				}

				if (error->drop_after_trigger) {
					(void) client_error_unregister ((Client *) client, error_type);
				}
			}
		}
	}

	return retval;

}

// handles error packets
void client_error_packet_handler (Packet *packet) {

	ErrorMessage message;
	if (packet_get_object (packet, &error_message_schema, NULL, &message) == SERIALIZER_RESULT_NONE) {
		const ErrorMessage *s_error = &message;

		switch (s_error->error_type) {
			case CLIENT_ERROR_NONE: break;

			case CLIENT_ERROR_CERVER_ERROR:
				client_error_trigger (
					CLIENT_ERROR_CERVER_ERROR,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_PACKET_ERROR:
				client_error_trigger (
					CLIENT_ERROR_PACKET_ERROR,
					packet->client, packet->connection,
					s_error->msg
				);
				break;

			case CLIENT_ERROR_FAILED_AUTH: {
				if (client_error_trigger (
					CLIENT_ERROR_FAILED_AUTH,
					packet->client, packet->connection,
					s_error->msg
				)) {
					// not error action is registered to handle the error
					cerver_log_error ("Failed to authenticate - %s", s_error->msg);
				}
			} break;

			case CLIENT_ERROR_GET_FILE:
				client_error_trigger (
					CLIENT_ERROR_GET_FILE,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_SEND_FILE:
				client_error_trigger (
					CLIENT_ERROR_SEND_FILE,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_FILE_NOT_FOUND:
				client_error_trigger (
					CLIENT_ERROR_FILE_NOT_FOUND,
					packet->client, packet->connection,
					s_error->msg
				);
				break;

			case CLIENT_ERROR_CREATE_LOBBY:
				client_error_trigger (
					CLIENT_ERROR_CREATE_LOBBY,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_JOIN_LOBBY:
				client_error_trigger (
					CLIENT_ERROR_JOIN_LOBBY,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_LEAVE_LOBBY:
				client_error_trigger (
					CLIENT_ERROR_LEAVE_LOBBY,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_FIND_LOBBY:
				client_error_trigger (
					CLIENT_ERROR_FIND_LOBBY,
					packet->client, packet->connection,
					s_error->msg
				);
				break;

			case CLIENT_ERROR_GAME_INIT:
				client_error_trigger (
					CLIENT_ERROR_GAME_INIT,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_GAME_START:
				client_error_trigger (
					CLIENT_ERROR_GAME_START,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_DESTROY_LOBBY:
				client_error_trigger (
					CLIENT_ERROR_DESTROY_LOBBY,
					packet->client, packet->connection,
					s_error->msg
				);
				break;

			default: {
				client_error_trigger (
					CLIENT_ERROR_UNKNOWN,
					packet->client, packet->connection,
					s_error->msg
				);
			} break;
		}
	}

}

// creates an error packet ready to be sent
Packet *client_error_packet_generate (
	const ClientErrorType type, const char *msg
) {

	return error_message_packet_create ((u32) type, msg);

}

// creates and send a new error packet
// returns 0 on success, 1 on error
u8 client_error_packet_generate_and_send (
	const ClientErrorType type, const char *msg,
	Client *client, Connection *connection
) {

	u8 retval = 1;

	Packet *error_packet = client_error_packet_generate (type, msg);
	if (error_packet) {
		packet_set_network_values (error_packet, NULL, client, connection, NULL);
		retval = packet_send (error_packet, 0, NULL, false);
		packet_delete (error_packet);
	}

	return retval;

}

#pragma endregion
