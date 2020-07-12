#ifndef _CERVER_EVENTS_H_
#define _CERVER_EVENTS_H_

#include <stdbool.h>

#include "cerver/types/types.h"

struct _Cerver;
struct _Client;
struct _Connection;

typedef enum CerverEventType {

	CERVER_EVENT_NONE                  		= 0, 

	CERVER_EVENT_STARTED,              		// the cerver has started
	CERVER_EVENT_TEARDOWN,             		// the cerver is going to be teardown 

	CERVER_EVENT_ON_HOLD_CONNECTED,   		// a connection has been put on hold
	CERVER_EVENT_ON_HOLD_DISCONNECTED,  	// an on hold connection disconnected
	CERVER_EVENT_ON_HOLD_DROPPED,   		// an on hold connection was dropped

	CERVER_EVENT_CLIENT_SUCCESS_AUTH,  		// a client connection has successfully authenticated
	CERVER_EVENT_CLIENT_FAILED_AUTH,   		// a client connection failed to authenticate
	CERVER_EVENT_CLIENT_CONNECTED,     		// a new client has connected to the cerver
	CERVER_EVENT_CLIENT_NEW_CONNECTION,		// added a connection to an existing client
	CERVER_EVENT_CLIENT_CLOSE_CONNECTION,	// a connection from an existing client was closed
	CERVER_EVENT_CLIENT_DISCONNECTED,  		// a client has disconnected from the cerver
	CERVER_EVENT_CLIENT_DROPPED,       		// a client has been dropped from the cerver

	CERVER_EVENT_ADMIN_FAILED_AUTH,  		// a possible admin connection failed to authenticate
	CERVER_EVENT_ADMIN_CONNECTED,      		// a new admin has connected & authenticated to the cerver
	CERVER_EVENT_ADMIN_NEW_CONNECTION,		// added a connection to an existing client
	CERVER_EVENT_ADMIN_CLOSE_CONNECTION,	// a connection from an existing client was closed
	CERVER_EVENT_ADMIN_DISCONNECTED,    	// an admin disconnected from the cerver
	CERVER_EVENT_ADMIN_DROPPED,    			// an admin was dropped from the cerver

	CERVER_EVENT_LOBBY_CREATE,         		// a new lobby was successfully created
	CERVER_EVENT_LOBBY_JOIN,           		// someone has joined a lobby
	CERVER_EVENT_LOBBY_LEAVE,          		// someone has exited the lobby
	CERVER_EVENT_LOBBY_START,          		// the game in the lobby has started

} CerverEventType;

typedef struct CerverEvent {

	CerverEventType type;               // the event we are waiting to happen

	bool create_thread;                 // create a detachable thread to run action
	bool drop_after_trigger;            // if we only want to trigger the event once 

	Action action;                      // the action to be triggered
	void *action_args;                  // the action arguments
	Action delete_action_args;          // how to get rid of the data when deleting the events

} CerverEvent;

extern void cerver_event_delete (void *event_ptr);

// registers an action to be triggered when the specified event occurs
// if there is an existing action registered to an event, it will be overrided
// a newly allocated CerverEventData structure will be passed to your method 
// that should be free using the cerver_event_data_delete () method
// returns 0 on success, 1 on error
extern u8 cerver_event_register (struct _Cerver *cerver, const CerverEventType event_type, 
	Action action, void *action_args, Action delete_action_args, 
	bool create_thread, bool drop_after_trigger);

// unregister the action associated with an event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error
extern u8 cerver_event_unregister (struct _Cerver *cerver, const CerverEventType event_type);

// triggers all the actions that are registred to an event
extern void cerver_event_trigger (const CerverEventType event_type, const struct _Cerver *cerver, 
	const struct _Client *client, const struct _Connection *connection);

#pragma region data

// structure that is passed to the user registered method
typedef struct CerverEventData {

	const struct _Cerver *cerver;

	const struct _Client *client;
	const struct _Connection *connection;

	void *action_args;                  // the action arguments
	Action delete_action_args;

} CerverEventData;

extern void cerver_event_data_delete (CerverEventData *event_data);

#pragma endregion

#endif