#ifndef _CERVER_CLIENT_H_
#define _CERVER_CLIENT_H_

#include <stdbool.h>

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"

#include "cerver/cerver.h"
#include "cerver/config.h"
#include "cerver/connection.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/handler.h"

#include "cerver/utils/log.h"

#define CLIENT_FILES_MAX_PATHS           32

struct _Cerver;
struct _Client;
struct _Connection;
struct _Packet;
struct _PacketsPerType;
struct _Handler;

struct _FileHeader;

struct _ClientEvent;
struct _ClientError;

#pragma region stats

struct _ClientStats {

	time_t threshold_time;			// every time we want to reset the client's stats

	u64 n_receives_done;			// n calls to recv ()

	u64 total_bytes_received;		// total amount of bytes received from this client
	u64 total_bytes_sent;			// total amount of bytes that have been sent to the client (all of its connections)

	u64 n_packets_received;			// total number of packets received from this client (packet header + data)
	u64 n_packets_sent;				// total number of packets sent to this client (all connections)

	struct _PacketsPerType *received_packets;
	struct _PacketsPerType *sent_packets;

};

typedef struct _ClientStats ClientStats;

CERVER_PUBLIC void client_stats_print (
	struct _Client *client
);

struct _ClientFileStats {

	u64 n_files_requests;				// n requests to get a file
	u64 n_success_files_requests;		// fulfilled requests
	u64 n_bad_files_requests;			// bad requests
	u64 n_files_sent;					// n files sent
	u64 n_bad_files_sent;				// n files that failed to send
	u64 n_bytes_sent;					// total bytes sent

	u64 n_files_upload_requests;		// n requests to upload a file
	u64 n_success_files_uploaded;		// n files received
	u64 n_bad_files_upload_requests;	// bad requests to upload files
	u64 n_bad_files_received;			// files that failed to be received
	u64 n_bytes_received;				// total bytes received

};

typedef struct _ClientFileStats ClientFileStats;

CERVER_PUBLIC void client_file_stats_print (
	struct _Client *client
);

#pragma endregion

#pragma region main

#define CLIENT_MAX_EVENTS				32
#define CLIENT_MAX_ERRORS				32

// anyone that connects to the cerver
struct _Client {

	// generated using connection values
	u64 id;
	time_t connected_timestamp;

	String *name;

	DoubleList *connections;

	// multiple connections can be associated with the same client using the same session id
	String *session_id;

	time_t last_activity;	// the last time the client sent / receive data

	bool drop_client;		// client failed to authenticate

	void *data;
	Action delete_data;

	// used when the client connects to another server
	bool running;
	time_t time_started;
	u64 uptime;

	// custom packet handlers
	unsigned int num_handlers_alive;       // handlers currently alive
	unsigned int num_handlers_working;     // handlers currently working
	pthread_mutex_t *handlers_lock;
	struct _Handler *app_packet_handler;
	struct _Handler *app_error_packet_handler;
	struct _Handler *custom_packet_handler;

	bool check_packets;              // enable / disbale packet checking

	// general client lock
	pthread_mutex_t *lock;

	struct _ClientEvent *events[CLIENT_MAX_EVENTS];
	struct _ClientError *errors[CLIENT_MAX_ERRORS];

	// files
	unsigned int n_paths;
	String *paths[CLIENT_FILES_MAX_PATHS];

	// default path where received files will be placed
	String *uploads_path;

	u8 (*file_upload_handler) (
		struct _Client *, struct _Connection *,
		struct _FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	);

	void (*file_upload_cb) (
		struct _Client *, struct _Connection *,
		const char *saved_filename
	);

	ClientFileStats *file_stats;

	ClientStats *stats;

};

typedef struct _Client Client;

CERVER_PUBLIC Client *client_new (void);

// completely deletes a client and all of its data
CERVER_PUBLIC void client_delete (void *ptr);

// used in data structures that require a delete function
// but the client needs to stay alive
CERVER_PUBLIC void client_delete_dummy (void *ptr);

// creates a new client and inits its values
CERVER_PUBLIC Client *client_create (void);

// creates a new client and registers a new connection
CERVER_PUBLIC Client *client_create_with_connection (
	struct _Cerver *cerver,
	const i32 sock_fd, const struct sockaddr_storage address
);

// sets the client's name
CERVER_EXPORT void client_set_name (
	Client *client, const char *name
);

// this methods is primarily used for logging
// returns the client's name directly (if any) & should NOT be deleted, if not
// returns a newly allocated string with the clients id that should be deleted after use
CERVER_EXPORT char *client_get_identifier (
	Client *client, bool *is_name
);

// sets the client's session id
CERVER_PUBLIC u8 client_set_session_id (
	Client *client, const char *session_id
);

// returns the client's app data
CERVER_EXPORT void *client_get_data (Client *client);

// sets client's data and a way to destroy it
// deletes the previous data of the client
CERVER_EXPORT void client_set_data (
	Client *client, void *data, Action delete_data
);

// sets customs PACKET_TYPE_APP and PACKET_TYPE_APP_ERROR packet types handlers
CERVER_EXPORT void client_set_app_handlers (
	Client *client,
	struct _Handler *app_handler, struct _Handler *app_error_handler
);

// sets a PACKET_TYPE_CUSTOM packet type handler
CERVER_EXPORT void client_set_custom_handler (
	Client *client, struct _Handler *custom_handler
);

// set whether to check or not incoming packets
// check packet's header protocol id & version compatibility
// if packets do not pass the checks, won't be handled and will be inmediately destroyed
// packets size must be cheked in individual methods (handlers)
// by default, this option is turned off
CERVER_EXPORT void client_set_check_packets (
	Client *client, bool check_packets
);

// compare clients based on their client ids
CERVER_PUBLIC int client_comparator_client_id (
	const void *a, const void *b
);

// compare clients based on their session ids
CERVER_PUBLIC int client_comparator_session_id (
	const void *a, const void *b
);

// closes all client connections
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_disconnect (Client *client);

// the client got disconnected from the cerver, so correctly clear our data
CERVER_EXPORT void client_got_disconnected (Client *client);

// drops a client form the cerver
// unregisters the client from the cerver and the deletes him
CERVER_EXPORT void client_drop (
	struct _Cerver *cerver, Client *client
);

// adds a new connection to the end of the client to the client's connection list
// without adding it to any other structure
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_connection_add (
	Client *client, struct _Connection *connection
);

// removes the connection from the client
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_connection_remove (
	Client *client, struct _Connection *connection
);

// closes the connection & then removes it from the client & finally deletes the connection
// moves the socket to the cerver's socket pool
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_connection_drop (
	struct _Cerver *cerver,
	Client *client, struct _Connection *connection
);

// removes the connection from the client referred to by the sock fd by calling client_connection_drop ()
// and also remove the client & connection from the cerver's structures when needed
// also checks if there is another active connection in the client, if not it will be dropped
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 client_remove_connection_by_sock_fd (
	struct _Cerver *cerver, Client *client, i32 sock_fd
);

// registers all the active connections from a client to the cerver's structures (like maps)
// returns 0 on success registering at least one, 1 if all connections failed
CERVER_PRIVATE u8 client_register_connections_to_cerver (
	struct _Cerver *cerver, Client *client
);

// unregiters all the active connections from a client from the cerver's structures (like maps)
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
CERVER_PRIVATE u8 client_unregister_connections_from_cerver (
	struct _Cerver *cerver, Client *client
);

// registers all the active connections from a client to the cerver's poll
// returns 0 on success registering at least one, 1 if all connections failed
CERVER_PRIVATE u8 client_register_connections_to_cerver_poll (
	struct _Cerver *cerver, Client *client
);

// unregisters all the active connections from a client from the cerver's poll
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
CERVER_PRIVATE u8 client_unregister_connections_from_cerver_poll (
	struct _Cerver *cerver, Client *client
);

// removes the client from cerver data structures, not taking into account its connections
CERVER_PRIVATE Client *client_remove_from_cerver (
	struct _Cerver *cerver, Client *client
);

// registers a client to the cerver --> add it to cerver's structures
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 client_register_to_cerver (
	struct _Cerver *cerver, Client *client
);

// unregisters a client from a cerver -- removes it from cerver's structures
CERVER_PRIVATE Client *client_unregister_from_cerver (
	struct _Cerver *cerver, Client *client
);

// gets the client associated with a sock fd using the client-sock fd map
CERVER_PUBLIC Client *client_get_by_sock_fd (
	struct _Cerver *cerver, i32 sock_fd
);

// searches the avl tree to get the client associated with the session id
// the cerver must support sessions
CERVER_PUBLIC Client *client_get_by_session_id (
	struct _Cerver *cerver, const char *session_id
);

// broadcast a packet to all clients inside an avl structure
CERVER_PUBLIC void client_broadcast_to_all_avl (
	AVLNode *node,
	struct _Cerver *cerver,
	struct _Packet *packet
);

#pragma endregion

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
	XX(17,	UNKNOWN, 			Unknown event)

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

	Action action;                      // the action to be triggered
	void *action_args;                  // the action arguments
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
	Action action, void *action_args, Action delete_action_args,
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

// structure that is passed to the user registered method
typedef struct ClientEventData {

	const struct _Client *client;
	const struct _Connection *connection;

	void *response_data;                // data that came with the response
	Action delete_response_data;

	void *action_args;                  // the action arguments
	Action delete_action_args;

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
	XX(13,	UNKNOWN, 			Unknown error)

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

	Action action;                      // the action to be triggered
	void *action_args;                  // the action arguments
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
	Action action, void *action_args, Action delete_action_args,
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
	Client *client, Connection *connection
);

#pragma endregion

#pragma region client

/*** Use these to connect/disconnect a client to/from another server ***/

typedef struct ClientConnection {

	pthread_t connection_thread_id;
	struct _Client *client;
	struct _Connection *connection;

} ClientConnection;

CERVER_PRIVATE void client_connection_aux_delete (
	ClientConnection *cc
);

// creates a new connection that is ready to connect and registers it to the client
CERVER_EXPORT struct _Connection *client_connection_create (
	Client *client,
	const char *ip_address, u16 port,
	Protocol protocol, bool use_ipv6
);

// registers an existing connection to a client
// retuns 0 on success, 1 on error
CERVER_EXPORT int client_connection_register (
	Client *client, struct _Connection *connection
);

// unregister an exitsing connection from the client
// returns 0 on success, 1 on error or if the connection does not belong to the client
CERVER_EXPORT int client_connection_unregister (
	Client *client, struct _Connection *connection
);

// performs a receive in the connection's socket to get a complete packet & handle it
CERVER_EXPORT void client_connection_get_next_packet (
	Client *client, struct _Connection *connection
);

/*** connect ***/

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is a blocking method, as it will wait until the connection has been successfull or a timeout
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 when the connection has been established, 1 on error or failed to connect
CERVER_EXPORT unsigned int client_connect (
	Client *client, struct _Connection *connection
);

// connects a client to the host with the specified values in the connection
// performs a first read to get the cerver info packet
// this is a blocking method, and works exactly the same as if only calling client_connect ()
// returns 0 when the connection has been established, 1 on error or failed to connect
CERVER_EXPORT unsigned int client_connect_to_cerver (
	Client *client, struct _Connection *connection
);

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is NOT a blocking method, a new thread will be created to wait for a connection to be established
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 on success connection thread creation, 1 on error
CERVER_EXPORT unsigned int client_connect_async (
	Client *client, struct _Connection *connection
);

/*** start ***/

// after a client connection successfully connects to a server,
// it will start the connection's update thread to enable the connection to
// receive & handle packets in a dedicated thread
// returns 0 on success, 1 on error
CERVER_EXPORT int client_connection_start (
	Client *client, struct _Connection *connection
);

// connects a client connection to a server
// and after a success connection, it will start the connection (create update thread for receiving messages)
// this is a blocking method, returns only after a success or failed connection
// returns 0 on success, 1 on error
CERVER_EXPORT int client_connect_and_start (
	Client *client, struct _Connection *connection
);

// connects a client connection to a server in a new thread to avoid blocking the calling thread,
// and after a success connection, it will start the connection (create update thread for receiving messages)
// returns 0 on success creating connection thread, 1 on error
CERVER_EXPORT u8 client_connect_and_start_async (
	Client *client, struct _Connection *connection
);

/*** requests ***/

// when a client is already connected to the cerver, a request can be made to the cerver
// the response will be handled by the client's handlers
// this is a blocking method, as it will wait until a complete cerver response has been received
// the response will be handled using the client's packet handler
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// retruns 0 when the response has been handled, 1 on error
CERVER_EXPORT unsigned int client_request_to_cerver (
	Client *client, struct _Connection *connection,
	struct _Packet *request
);

// when a client is already connected to the cerver, a request can be made to the cerver
// the response will be handled by the client's handlers
// this method will NOT block
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// returns 0 on success request, 1 on error
CERVER_EXPORT unsigned int client_request_to_cerver_async (
	Client *client, struct _Connection *connection,
	struct _Packet *request
);

/*** files ***/

// adds a new file path to take into account when getting a request for a file
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_files_add_path (
	Client *client, const char *path
);

// sets the default uploads path to be used when receiving a file
CERVER_EXPORT void client_files_set_uploads_path (
	Client *client, const char *uploads_path
);

// sets a custom method to be used to handle a file upload (receive)
// in this method, file contents must be consumed from the sock fd
// and return 0 on success and 1 on error
CERVER_EXPORT void client_files_set_file_upload_handler (
	Client *client,
	u8 (*file_upload_handler) (
		struct _Client *, struct _Connection *,
		struct _FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	)
);

// sets a callback to be executed after a file has been successfully received
CERVER_EXPORT void client_files_set_file_upload_cb (
	Client *client,
	void (*file_upload_cb) (
		struct _Client *, struct _Connection *,
		const char *saved_filename
	)
);

// search for the requested file in the configured paths
// returns the actual filename (path + directory) where it was found, NULL on error
CERVER_PUBLIC String *client_files_search_file (
	Client *client, const char *filename
);

// requests a file from the cerver
// the client's uploads_path should have been configured before calling this method
// returns 0 on success sending request, 1 on failed to send request
CERVER_EXPORT u8 client_file_get (
	Client *client, struct _Connection *connection,
	const char *filename
);

// sends a file to the cerver
// returns 0 on success sending request, 1 on failed to send request
CERVER_EXPORT u8 client_file_send (
	Client *client, struct _Connection *connection,
	const char *filename
);

/*** update ***/

// receive data from connection's socket
// this method does not perform any checks and expects a valid buffer
// to handle incomming data
// returns 0 on success, 1 on error
CERVER_PRIVATE unsigned int client_receive_internal (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size
);

// allocates a new packet buffer to receive incoming data from the connection's socket
// returns 0 on success handle, 1 if any error ocurred and must likely the connection was ended
CERVER_PUBLIC unsigned int client_receive (
	Client *client, struct _Connection *connection
);

/*** end ***/

// closes the connection's socket & set it to be inactive
// does not send a close connection packet to the cerver
// returns 0 on success, 1 on error
CERVER_PUBLIC int client_connection_stop (
	Client *client, Connection *connection
);

// terminates the connection & closes the socket
// but does NOT destroy the current connection
// returns 0 on success, 1 on error
CERVER_PUBLIC int client_connection_close (
	Client *client, struct _Connection *connection
);

// terminates and destroys a connection registered to a client
// that is connected to a cerver
// returns 0 on success, 1 on error
CERVER_PUBLIC int client_connection_end (
	Client *client, struct _Connection *connection
);

// stop any on going connection and process and destroys the client
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_teardown (Client *client);

// calls client_teardown () in a new thread as handlers might need time to stop
// that will cause the calling thread to wait at least a second
// returns 0 on success creating thread, 1 on error
CERVER_EXPORT u8 client_teardown_async (Client *client);

#pragma endregion

#endif