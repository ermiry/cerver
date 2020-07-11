#ifndef _CERVER_ERRORS_H_
#define _CERVER_ERRORS_H_

#include <stdbool.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

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

extern void cerver_error_event_delete (void *event_ptr);

#pragma endregion

#pragma region data

// structure that is passed to the user registered method
typedef struct CerverErrorEventData {

	const struct _Cerver *cerver;

	const struct _Client *client;
	const struct _Connection *connection;

	void *action_args;                  // the action arguments
	Action delete_action_args;

} CerverErrorEventData;

extern void cerver_error_event_data_delete (CerverErrorEventData *error_event_data);

#pragma endregion

#pragma region error

// when a client error happens, it sets the appropaited msg (if any)
// and an event is triggered
typedef struct Error {

    time_t timestamp;
    u32 error_type;
    estring *msg;

} Error;

extern Error *error_new (u32 error_type, const char *msg);

extern void error_delete (void *ptr);

#pragma endregion

#pragma region packets

// creates an error packet ready to be sent
extern struct _Packet *error_packet_generate (u32 error_type, const char *msg);

// creates and send a new error packet
// returns 0 on success, 1 on error
extern u8 error_packet_generate_and_send (u32 error_type, const char *msg,
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