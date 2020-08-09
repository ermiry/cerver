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

typedef enum CerverErrorType {

    CERVER_ERROR_NONE                    = 0,

	CERVER_ERROR_CERVER_ERROR            = 1, // sent to the client when the cerver has an internal error

	CERVER_ERROR_FAILED_AUTH             = 2, // errors that is sent to the client when he failed to authenticate

	CERVER_ERROR_CREATE_LOBBY            = 3, // failed to create a new game lobby
	CERVER_ERROR_JOIN_LOBBY              = 4, // a client / player failed to join an existin lobby
	CERVER_ERROR_LEAVE_LOBBY             = 5, // a player failed to leave from a lobby
	CERVER_ERROR_FIND_LOBBY              = 6, // failed to find a game lobby for a player

	CERVER_ERROR_GAME_INIT               = 7, // the game failed to init properly
	CERVER_ERROR_GAME_START              = 8, // the game failed to start

} CerverErrorType;

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
CERVER_EXPORT u8 cerver_error_event_register (struct _Cerver *cerver, const CerverErrorType error_type, 
    Action action, void *action_args, Action delete_action_args, 
    bool create_thread, bool drop_after_trigger);

// unregister the action associated with an error event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error
CERVER_EXPORT u8 cerver_error_event_unregister (struct _Cerver *cerver, const CerverErrorType error_type);

// triggers all the actions that are registred to an error
CERVER_PRIVATE void cerver_error_event_trigger (const CerverErrorType error_type, 
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

#pragma region error

typedef struct CerverError {

	CerverErrorType type;
    time_t timestamp;
    String *msg;

} CerverError;

CERVER_PRIVATE CerverError *cerver_error_new (const CerverErrorType type, const char *msg);

CERVER_PRIVATE void cerver_error_delete (void *cerver_error_ptr);

#pragma endregion

#pragma region packets

// creates an error packet ready to be sent
CERVER_PUBLIC struct _Packet *error_packet_generate (const CerverErrorType type, const char *msg);

// creates and send a new error packet
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 error_packet_generate_and_send (const CerverErrorType type, const char *msg,
    struct _Cerver *cerver, struct _Client *client, struct _Connection *connection);

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