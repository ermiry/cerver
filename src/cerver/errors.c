#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/errors.h"
#include "cerver/packets.h"

#include "cerver/threads/thread.h"

u8 cerver_error_event_unregister (Cerver *cerver, const CerverErrorType error_type);

#pragma region types

// get the description for the current error type
const char *cerver_error_type_description (const CerverErrorType type) {

	switch (type) {
		#define XX(num, name, description) case CERVER_ERROR_##name: return #description;
		CERVER_ERROR_MAP(XX)
		#undef XX
	}

	return cerver_error_type_description (CERVER_ERROR_UNKNOWN);

}

#pragma endregion

#pragma region data

static CerverErrorEventData *cerver_error_event_data_new (void) {

	CerverErrorEventData *error_event_data = (CerverErrorEventData *) malloc (sizeof (CerverErrorEventData));
	if (error_event_data) {
		error_event_data->cerver = NULL;

		error_event_data->client = NULL;
		error_event_data->connection = NULL;

		error_event_data->action_args = NULL;
		error_event_data->delete_action_args = NULL;
		error_event_data->owns_action_args = false;

		error_event_data->error_message = NULL;
	}

	return error_event_data;

}

void cerver_error_event_data_delete (CerverErrorEventData *error_event_data) {

	if (error_event_data) {
		str_delete (error_event_data->error_message);

		if (
			error_event_data->owns_action_args
			&& error_event_data->action_args && error_event_data->delete_action_args
		) {
			error_event_data->delete_action_args (error_event_data->action_args);
		}

		free (error_event_data);
	}

}

static CerverErrorEventData *cerver_error_event_data_create (
	const Cerver *cerver,
	const Client *client, const Connection *connection,
	void *action_args,
	const char *error_message
) {

	CerverErrorEventData *error_event_data = cerver_error_event_data_new ();
	if (error_event_data) {
		error_event_data->cerver = cerver;

		error_event_data->client = client;
		error_event_data->connection = connection;

		error_event_data->action_args = action_args;

		error_event_data->error_message = error_message ? str_new (error_message) : NULL;
	}

	return error_event_data;

}

#pragma endregion

#pragma region event

static CerverErrorEvent *cerver_error_event_new (void) {

	CerverErrorEvent *cerver_error_event = (CerverErrorEvent *) malloc (sizeof (CerverErrorEvent));
	if (cerver_error_event) {
		cerver_error_event->type = CERVER_ERROR_NONE;

		cerver_error_event->create_thread = false;
		cerver_error_event->drop_after_trigger = false;

		cerver_error_event->work = NULL;
		cerver_error_event->work_args = NULL;
		cerver_error_event->delete_action_args = NULL;
	}

	return cerver_error_event;

}

void cerver_error_event_delete (void *event_ptr) {

	if (event_ptr) {
		CerverErrorEvent *event = (CerverErrorEvent *) event_ptr;

		if (event->work_args) {
			if (event->delete_action_args)
				event->delete_action_args (event->work_args);
		}

		free (event);
	}

}

// an error type indexes the error events of the cerver
static inline bool cerver_error_type_is_valid (const CerverErrorType error_type) {

	return (unsigned int) error_type < CERVER_MAX_ERRORS;

}

// registers an action to be triggered when the specified error event occurs
// if there is an existing action registered to an error event, it will be overrided
// a newly allocated CerverErrorEventData structure will be passed to your method
// that should be free using the cerver_error_event_data_delete () method
// returns 0 on success, 1 on error
u8 cerver_error_event_register (
	Cerver *cerver,
	const CerverErrorType error_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
) {

	u8 retval = 1;

	if (cerver && cerver_error_type_is_valid (error_type)) {
		CerverErrorEvent *error = cerver_error_event_new ();
		if (error) {
			error->type = error_type;

			error->create_thread = create_thread;
			error->drop_after_trigger = drop_after_trigger;

			error->work = work;
			error->work_args = work_args;
			error->delete_action_args = delete_action_args;

			// search if there is an action already registred for that error and remove it
			(void) cerver_error_event_unregister (cerver, error_type);

			cerver->errors[error_type] = error;

			retval = 0;
		}
	}

	return retval;

}

// unregister the action associated with an error event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error or if error is NOT registered
u8 cerver_error_event_unregister (
	Cerver *cerver, const CerverErrorType error_type
) {

	u8 retval = 1;

	if (cerver && cerver_error_type_is_valid (error_type)) {
		if (cerver->errors[error_type]) {
			cerver_error_event_delete (cerver->errors[error_type]);
			cerver->errors[error_type] = NULL;

			retval = 0;
		}
	}

	return retval;

}

// triggers all the actions that are registred to an error
void cerver_error_event_trigger (
	const CerverErrorType error_type,
	const Cerver *cerver,
	const Client *client, const Connection *connection,
	const char *error_message
) {

	if (cerver && cerver_error_type_is_valid (error_type)) {
		CerverErrorEvent *error = cerver->errors[error_type];
		if (error) {
			// trigger the action
			if (error->work) {
				CerverErrorEventData *error_event_data = cerver_error_event_data_create (
					cerver, client, connection, error->work_args, error_message
				);

				// the error is dropped while its action can still run in its thread,
				// so the arguments go with the data of this trigger
				if (error_event_data && error->drop_after_trigger) {
					error_event_data->delete_action_args = error->delete_action_args;
					error_event_data->owns_action_args = true;
					error->work_args = NULL;
				}

				// the thread is counted, so the cerver teardown waits for it
				if (error->create_thread) {
					(void) thread_group_create (
						(ThreadGroup *) &cerver->event_threads,
						error->work, error_event_data
					);
				}

				else {
					(void) error->work (error_event_data);
				}

				if (error->drop_after_trigger) {
					(void) cerver_error_event_unregister ((Cerver *) cerver, error_type);
				}
			}
		}
	}

}

#pragma endregion

#pragma region handler

// handles the error packets that come from the clients,
// the errors of the cerver itself are not packets, they trigger their events where they happen
void cerver_error_packet_handler (Packet *packet) {

	ErrorMessage message;
	if (packet_get_object (packet, &error_message_schema, NULL, &message) == SERIALIZER_RESULT_NONE) {
		const ErrorMessage *s_error = &message;

		switch (s_error->error_type) {
			case CERVER_ERROR_NONE: break;

			case CERVER_ERROR_PACKET_ERROR:
				cerver_error_event_trigger (
					CERVER_ERROR_PACKET_ERROR,
					packet->cerver, packet->client, packet->connection,
					s_error->msg
				);
				break;

			case CERVER_ERROR_GET_FILE:
				cerver_error_event_trigger (
					CERVER_ERROR_GET_FILE,
					packet->cerver, packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CERVER_ERROR_SEND_FILE:
				cerver_error_event_trigger (
					CERVER_ERROR_SEND_FILE,
					packet->cerver, packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CERVER_ERROR_FILE_NOT_FOUND:
				cerver_error_event_trigger (
					CERVER_ERROR_FILE_NOT_FOUND,
					packet->cerver, packet->client, packet->connection,
					s_error->msg
				);
				break;

			default: {
				cerver_error_event_trigger (
					CERVER_ERROR_UNKNOWN,
					packet->cerver, packet->client, packet->connection,
					s_error->msg
				);
			} break;
		}
	}

}

#pragma endregion

#pragma region packets

SERIALIZER_DEFINE (ErrorMessage, error_message, ERROR_MESSAGE_VERSION, ERROR_MESSAGE_FIELDS)

// a byte that continues a UTF-8 character
#define ERROR_MESSAGE_CONTINUATION_MASK		0xc0
#define ERROR_MESSAGE_CONTINUATION			0x80

// the message is cut to its field before a character that does not fit
static void error_message_set_text (ErrorMessage *message, const char *msg) {

	size_t length = strnlen (msg, ERROR_MESSAGE_LENGTH);

	if (length == ERROR_MESSAGE_LENGTH) {
		length -= 1;
		while (length && ((msg[length] & ERROR_MESSAGE_CONTINUATION_MASK) == ERROR_MESSAGE_CONTINUATION)) {
			length -= 1;
		}
	}

	(void) memcpy (message->msg, msg, length);
	message->msg[length] = '\0';

}

// creates an error packet ready to be sent with the type and the message,
// a message longer than ERROR_MESSAGE_LENGTH is cut without splitting a character
Packet *error_message_packet_create (const u32 error_type, const char *msg) {

	ErrorMessage message;
	serializer_object_init (&error_message_schema, &message);

	message.timestamp = (i64) time (NULL);
	message.error_type = error_type;
	if (msg) error_message_set_text (&message, msg);

	return packet_create_serialized (
		PACKET_TYPE_ERROR, REQUEST_PACKET_TYPE_NONE,
		&error_message_schema, &message
	);

}

// creates an error packet ready to be sent
Packet *error_packet_generate (const CerverErrorType type, const char *msg) {

	return error_message_packet_create ((u32) type, msg);

}

// creates and send a new error packet
// returns 0 on success, 1 on error
u8 error_packet_generate_and_send (
	const CerverErrorType type, const char *msg,
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

	Packet *error_packet = error_packet_generate (type, msg);
	if (error_packet) {
		packet_set_network_values (error_packet, cerver, client, connection, NULL);
		retval = packet_send (error_packet, 0, NULL, false);
		packet_delete (error_packet);
	}

	return retval;

}

#pragma endregion
