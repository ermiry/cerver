#ifndef _CERVER_EVENTS_H_
#define _CERVER_EVENTS_H_

#include <stdbool.h>

#include "cerver/types/types.h"

struct _Cerver;
struct _Client;
struct _Connection;

typedef enum CerverEventType {

	CERVER_EVENT_NONE                  = 0, 

	CERVER_EVENT_STARTED,              // the cerver has started
	CERVER_EVENT_TEARDOWN,             // the cerver is goind to be teardown 

	CERVER_EVENT_ON_HOLD_CONNECTION,   // a connection has been put on hold
	CERVER_EVENT_CLIENT_SUCCESS_AUTH,  // a client connection has successfully authenticated
	CERVER_EVENT_CLIENT_FAILED_AUTH,   // a client connection failed to authenticate
	CERVER_EVENT_CLIENT_CONNECTED,     // a new client has connected to the cerver
	CERVER_EVENT_ADMIN_CONNECTED,      // a new admin has connected & authenticated to the cerver
	CERVER_EVENT_CLIENT_DISCONNECTED,  // a client has disconnected from the cerver
	CERVER_EVENT_CLIENT_DROPPED,       // a client has been dropped from the cerver

	CERVER_EVENT_LOBBY_CREATE,         // a new lobby was successfully created
	CERVER_EVENT_LOBBY_JOIN,           // someone has joined a lobby
	CERVER_EVENT_LOBBY_LEAVE,          // someone has exited the lobby
	CERVER_EVENT_LOBBY_START,          // the game in the lobby has started

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

#endif