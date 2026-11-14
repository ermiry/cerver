#ifndef _CERVER_CLIENT_EVENTS_H_
#define _CERVER_CLIENT_EVENTS_H_

#include "cerver/config.h"

#include "cerver/client/client.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region events

#define CLIENT_EVENT_MAP(XX)																													\
	XX(0,	NONE, 				No event)																										\
	XX(1,	CONNECTED, 			Connected to cerver)																							\
	XX(2,	DISCONNECTED, 		Disconnected from the cerver; either by the cerver or by losing connection)										\
	XX(3,	CONNECTION_FAILED, 	Failed to connect to cerver)																					\
	XX(4,	CONNECTION_CLOSE, 	The connection was clossed directly by client. This happens when a call to a recv () methods returns <= 0)		\
	XX(5,	CONNECTION_DATA, 	Data has been received; only triggered from client request methods)												\
	XX(6,	CERVER_INFO, 		Received cerver info from the cerver)																			\
	XX(7,	CERVER_TEARDOWN, 	The cerver is going to teardown & the client will disconnect)													\
	XX(8,	CERVER_STATS, 		Received cerver stats)																							\
	XX(9,	CERVER_GAME_STATS, 	Received cerver game stats)																						\
	XX(10,	AUTH_SENT, 			Auth data has been sent to the cerver)																			\
	XX(11,	SUCCESS_AUTH, 		Auth with cerver has been successfull)																			\
	XX(12,	MAX_AUTH_TRIES, 	Maxed out attempts to authenticate to cerver; need to try again)												\
	XX(13,	LOBBY_CREATE, 		A new lobby was successfully created)																			\
	XX(14,	LOBBY_JOIN, 		Correctly joined a new lobby)																					\
	XX(15,	LOBBY_LEAVE, 		Successfully exited a lobby)																					\
	XX(16,	LOBBY_START, 		The game in the lobby has started)																				\
	XX(17,	LOBBY_UPDATE, 		Received the current state of a lobby)																			\
	XX(18,	LOBBY_DESTROY, 		The lobby was destroyed)																						\
	XX(19,	GAME_INIT, 			The game of the lobby was initialized)																			\
	XX(20,	UNKNOWN, 			Unknown event)

typedef enum ClientEventType {

	#define XX(num, name, description) CLIENT_EVENT_##name = num,
	CLIENT_EVENT_MAP (XX)
	#undef XX

} ClientEventType;

// get the description for the current error type
CERVER_EXPORT const char *client_event_type_description (
	const ClientEventType type
);

struct _ClientEvent {

	ClientEventType type;         // the event we are waiting to happen
	bool create_thread;                 // create a detachable thread to run action
	bool drop_after_trigger;            // if we only want to trigger the event once

	// the request that triggered the event
	// this is usefull for custom events
	u32 request_type;
	void *response_data;                // data that came with the response
	Action delete_response_data;

	Work work;                      	// the action to be triggered
	void *work_args;                  // the action arguments
	Action delete_action_args;          // how to get rid of the data

};

typedef struct _ClientEvent ClientEvent;

// registers an action to be triggered when the specified event occurs
// if there is an existing action registered to an event, it will be overrided
// a newly allocated ClientEventData structure will be passed to your method
// that should be free using the client_event_data_delete () method
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_event_register (
	struct _Client *client,
	const ClientEventType event_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
);

// unregister the action associated with an event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error or if event is NOT registered
CERVER_EXPORT u8 client_event_unregister (
	struct _Client *client, const ClientEventType event_type
);

CERVER_PRIVATE void client_event_set_response (
	struct _Client *client,
	const ClientEventType event_type,
	void *response_data, Action delete_response_data
);

// triggers all the actions that are registred to an event
CERVER_PRIVATE void client_event_trigger (
	const ClientEventType event_type,
	const struct _Client *client, const struct _Connection *connection
);

// triggers the event with a response that only this trigger owns:
// client_event_data_delete () releases it with delete_response (free () if NULL),
// and it is released right away when the event has no action
CERVER_PRIVATE void client_event_trigger_with_response (
	const ClientEventType event_type,
	const struct _Client *client, const struct _Connection *connection,
	void *response, Action delete_response
);

// structure that is passed to the user registered method
typedef struct ClientEventData {

	const struct _Client *client;
	const struct _Connection *connection;

	void *response_data;                // data that came with the response
	Action delete_response_data;

	// client_event_data_delete () releases the response of this event only,
	// like the LobbySummary of the lobby events
	bool owns_response_data;

	void *action_args;                  // the action arguments
	Action delete_action_args;

	// the event was dropped after this trigger, so client_event_data_delete ()
	// releases the action arguments with delete_action_args ()
	bool owns_action_args;

} ClientEventData;

CERVER_PUBLIC void client_event_data_delete (
	ClientEventData *event_data
);

#pragma endregion

#pragma region errors

#define CLIENT_ERROR_MAP(XX)													\
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
	XX(13,	DESTROY_LOBBY, 		The lobby could not be destroyed)				\
	XX(14,	UNKNOWN, 			Unknown error)

typedef enum ClientErrorType {

	#define XX(num, name, description) CLIENT_ERROR_##name = num,
	CLIENT_ERROR_MAP (XX)
	#undef XX

} ClientErrorType;

// get the description for the current error type
CERVER_EXPORT const char *client_error_type_description (
	const ClientErrorType type
);

struct _ClientError {

	ClientErrorType type;
	bool create_thread;                 // create a detachable thread to run action
	bool drop_after_trigger;            // if we only want to trigger the event once

	Work work;                      	// the action to be triggered
	void *work_args;                  // the action arguments
	Action delete_action_args;          // how to get rid of the data

};

typedef struct _ClientError ClientError;

// registers an action to be triggered when the specified error occurs
// if there is an existing action registered to an error, it will be overrided
// a newly allocated ClientErrorData structure will be passed to your method
// that should be free using the client_error_data_delete () method
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_error_register (
	struct _Client *client,
	const ClientErrorType error_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
);

// unregisters the action associated with the error types
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error or if error is NOT registered
CERVER_EXPORT u8 client_error_unregister (
	struct _Client *client, const ClientErrorType error_type
);

// triggers all the actions that are registred to an error
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 client_error_trigger (
	const ClientErrorType error_type,
	const struct _Client *client, const struct _Connection *connection,
	const char *error_message
);

// structure that is passed to the user registered method
typedef struct ClientErrorData {

	const struct _Client *client;
	const struct _Connection *connection;

	void *action_args;                  // the action arguments set by the user

	// the error was dropped after this trigger, so client_error_data_delete ()
	// releases the action arguments with delete_action_args ()
	Action delete_action_args;
	bool owns_action_args;

	String *error_message;

} ClientErrorData;

CERVER_PUBLIC void client_error_data_delete (
	ClientErrorData *error_data
);

// creates an error packet ready to be sent
CERVER_PUBLIC struct _Packet *client_error_packet_generate (
	const ClientErrorType type, const char *msg
);

// creates and send a new error packet
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 client_error_packet_generate_and_send (
	const ClientErrorType type, const char *msg,
	Client *client, struct _Connection *connection
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
