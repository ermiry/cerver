#ifndef _CERVER_ERRORS_H_
#define _CERVER_ERRORS_H_

#include <stdbool.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/config.h"

struct _Cerver;
struct _Client;
struct _Packet;
struct _Connection;

#pragma region types

#define CERVER_MAX_ERRORS				32

#define CERVER_ERROR_MAP(XX)													\
	XX(0,	NONE, 				No error)										\
	XX(1,	CERVER_ERROR, 		The cerver had an internal error)				\
	XX(2,	PACKET_ERROR, 		The cerver was unable to handle the packet)		\
	XX(3,	FAILED_AUTH, 		Client failed to authenticate)					\
	XX(4,	GET_FILE, 			Bad get file request)							\
	XX(5,	SEND_FILE, 			Bad upload file request)						\
	XX(6,	FILE_NOT_FOUND, 	The request file was not found)					\
	XX(7,	CREATE_LOBBY, 		Failed to create a new game lobby)				\
	XX(8,	JOIN_LOBBY, 		The player failed to join an existing lobby)	\
	XX(9,	LEAVE_LOBBY, 		The player failed to exit the lobby)			\
	XX(10,	FIND_LOBBY, 		Failed to find a suitable game lobby)			\
	XX(11,	GAME_INIT, 			The game failed to init)						\
	XX(12,	GAME_START, 		The game failed to start)						\
	XX(13,	UNKNOWN, 			Unknown error)

typedef enum CerverErrorType {

	#define XX(num, name, description) CERVER_ERROR_##name = num,
	CERVER_ERROR_MAP (XX)
	#undef XX

} CerverErrorType;

// get the description for the current error type
CERVER_EXPORT const char *cerver_error_type_description (CerverErrorType type);

#pragma endregion

#pragma region event

typedef struct CerverErrorEvent {

	CerverErrorType type;

	bool create_thread;                 // create a detachable thread to run action
	bool drop_after_trigger;            // if we only want to trigger the event once

	Action action;                      // the action to be triggered
	void *action_args;                  // the action arguments
	Action delete_action_args;          // how to get rid of the data when deleting the listeners

} CerverErrorEvent;

CERVER_PUBLIC void cerver_error_event_delete (void *event_ptr);

// registers an action to be triggered when the specified error event occurs
// if there is an existing action registered to an error event, it will be overrided
// a newly allocated CerverErrorEventData structure will be passed to your method
// that should be free using the cerver_error_event_data_delete () method
// returns 0 on success, 1 on error
CERVER_EXPORT u8 cerver_error_event_register (
	struct _Cerver *cerver,
	const CerverErrorType error_type,
	Action action, void *action_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
);

// unregister the action associated with an error event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error or if error is NOT registered
CERVER_EXPORT u8 cerver_error_event_unregister (struct _Cerver *cerver, const CerverErrorType error_type);

// triggers all the actions that are registred to an error
CERVER_PRIVATE void cerver_error_event_trigger (
	const CerverErrorType error_type,
	const struct _Cerver *cerver,
	const struct _Client *client, const struct _Connection *connection,
	const char *error_message
);

#pragma endregion

#pragma region data

// structure that is passed to the user registered method
typedef struct CerverErrorEventData {

	const struct _Cerver *cerver;

	const struct _Client *client;
	const struct _Connection *connection;

	void *action_args;                  // the action arguments

	String *error_message;

} CerverErrorEventData;

CERVER_PUBLIC void cerver_error_event_data_delete (CerverErrorEventData *error_event_data);

#pragma endregion

#pragma region handler

// handles error packets
CERVER_PRIVATE void cerver_error_packet_handler (struct _Packet *packet);

#pragma endregion

#pragma region packets

// creates an error packet ready to be sent
CERVER_PUBLIC struct _Packet *error_packet_generate (const CerverErrorType type, const char *msg);

// creates and send a new error packet
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 error_packet_generate_and_send (
	const CerverErrorType type, const char *msg,
	struct _Cerver *cerver, struct _Client *client, struct _Connection *connection
);

#pragma endregion

#pragma region serialization

#define ERROR_MESSAGE_LENGTH        128

// serialized error data
typedef struct SError {

	time_t timestamp;
	u32 error_type;
	char msg[ERROR_MESSAGE_LENGTH];

} SError;

#pragma endregion

#endif