#ifndef _CERVER_EVENTS_H_
#define _CERVER_EVENTS_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

struct _Cerver;
struct _Client;
struct _Connection;

#pragma region types

#define CERVER_EVENT_MAP(XX)																		\
	XX(0,	NONE, 						No event)													\
	XX(1,	STARTED, 					The cerver has started)										\
	XX(2,	TEARDOWN, 					The cerver is going to be teardown)							\
	XX(3,	ON_HOLD_CONNECTED, 			A new connection has been put on hold)						\
	XX(4,	ON_HOLD_DISCONNECTED, 		An on hold connection disconnected)							\
	XX(5,	ON_HOLD_DROPPED, 			An on hold connection was dropped)							\
	XX(6,	CLIENT_SUCCESS_AUTH, 		A client connection has successfully authenticated)			\
	XX(7,	CLIENT_FAILED_AUTH,			A client connection failed to authenticate)					\
	XX(8,	CLIENT_CONNECTED, 			A new client has connected to the cerver)					\
	XX(9,	CLIENT_NEW_CONNECTION, 		Added a connection to an existing client)					\
	XX(11,	CLIENT_CLOSE_CONNECTION,	A connection from an existing client was closed)			\
	XX(12,	CLIENT_DISCONNECTED, 		A client has disconnected from the cerver)					\
	XX(13,	CLIENT_DROPPED, 			A client has been dropped from the cerver)					\
	XX(14,	ADMIN_FAILED_AUTH, 			A possible admin connection failed to authenticate)			\
	XX(15,	ADMIN_CONNECTED, 			A new admin has connected & authenticated to the cerver)	\
	XX(16,	ADMIN_NEW_CONNECTION, 		Added a connection to an existing client)					\
	XX(17,	ADMIN_CLOSE_CONNECTION, 	A connection from an existing client was closed)			\
	XX(18,	ADMIN_DISCONNECTED, 		An admin disconnected from the cerver)						\
	XX(19,	ADMIN_DROPPED, 				An admin was dropped from the cerver)						\
	XX(20,	LOBBY_CREATE, 				A new lobby was successfully created)						\
	XX(21,	LOBBY_JOIN, 				Someone has joined a lobby)									\
	XX(22,	LOBBY_LEAVE, 				Someone has exited the lobby)								\
	XX(23,	LOBBY_START, 				The game in the lobby has started)							\
	XX(24,	UNKNOWN, 					Unknown event)

typedef enum CerverEventType {

	#define XX(num, name, description) CERVER_EVENT_##name = num,
	CERVER_EVENT_MAP (XX)
	#undef XX

} CerverEventType;

// get the description for the current event type
CERVER_EXPORT const char *cerver_event_type_description (CerverEventType type);

#pragma endregion

#pragma region event

typedef struct CerverEvent {

	CerverEventType type;               // the event we are waiting to happen

	bool create_thread;                 // create a detachable thread to run action
	bool drop_after_trigger;            // if we only want to trigger the event once

	Action action;                      // the action to be triggered
	void *action_args;                  // the action arguments
	Action delete_action_args;          // how to get rid of the data when deleting the events

} CerverEvent;

CERVER_PUBLIC void cerver_event_delete (void *event_ptr);

// registers an action to be triggered when the specified event occurs
// if there is an existing action registered to an event, it will be overrided
// a newly allocated CerverEventData structure will be passed to your method
// that should be free using the cerver_event_data_delete () method
// returns 0 on success, 1 on error
CERVER_EXPORT u8 cerver_event_register (
	struct _Cerver *cerver,
	const CerverEventType event_type,
	Action action, void *action_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
);

// unregister the action associated with an event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error
CERVER_EXPORT u8 cerver_event_unregister (struct _Cerver *cerver, const CerverEventType event_type);

// triggers all the actions that are registred to an event
CERVER_PRIVATE void cerver_event_trigger (
	const CerverEventType event_type,
	const struct _Cerver *cerver,
	const struct _Client *client, const struct _Connection *connection
);

#pragma endregion

#pragma region data

// structure that is passed to the user registered method
typedef struct CerverEventData {

	const struct _Cerver *cerver;

	const struct _Client *client;
	const struct _Connection *connection;

	void *action_args;                  // the action arguments
	Action delete_action_args;

} CerverEventData;

CERVER_PUBLIC void cerver_event_data_delete (CerverEventData *event_data);

#pragma endregion

#endif