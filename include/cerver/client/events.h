/**
 * @file cerver/client/events.h
 * @brief The events and the errors of a client: the actions that the application registers for them.
 */

#ifndef _CERVER_CLIENT_EVENTS_H_
#define _CERVER_CLIENT_EVENTS_H_

#include "cerver/config.h"

#include "cerver/client/client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup client_events Events
 * @ingroup client
 * @brief The actions that run when something happens to a client.
 * @{
 */

/**
 * @brief The X-macro table of the events of a client: the number, the name and the description of each one.
 */
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

/**
 * @brief The type of an event of a client, one `CLIENT_EVENT_<name>` for each row of #CLIENT_EVENT_MAP.
 */
typedef enum ClientEventType {

	#define XX(num, name, description) CLIENT_EVENT_##name = num,
	CLIENT_EVENT_MAP (XX)
	#undef XX

} ClientEventType;

/**
 * @brief Returns the description of an event type.
 *
 * @param[in] type the event type
 * @return the description of its row in #CLIENT_EVENT_MAP, or the one of CLIENT_EVENT_UNKNOWN for a value out of
 * the table; a static string that must not be freed
 */
CERVER_EXPORT const char *client_event_type_description (
	const ClientEventType type
);

/**
 * @brief The action that the application registered for an event of a client.
 */
struct _ClientEvent {

	/** @brief The event that triggers the action. */
	ClientEventType type;
	/** @brief The action runs in a thread of the event threads of the client, instead of the one that triggers it. */
	bool create_thread;
	/** @brief The action is unregistered after its first trigger. */
	bool drop_after_trigger;

	/**
	 * @brief The request that triggered the event, for custom events.
	 *
	 * @note cerver sets it to 0 and does not use it.
	 * @deprecated It goes away in the 3.0.
	 */
	u32 request_type;
	/** @brief The data that came with the response, set by client_event_set_response (). */
	void *response_data;
	/** @brief Releases #response_data once the event and every trigger are done with it, NULL to keep it. */
	Action delete_response_data;

	/** @brief The action, it gets a ClientEventData. */
	Work work;
	/** @brief The arguments of the action. */
	void *work_args;
	/** @brief Releases the arguments once the event and every trigger are done with them, NULL to keep them. */
	Action delete_action_args;

	/**
	 * @brief The arguments shared with the data of each trigger, for the internal methods of cerver: the last one
	 * that releases them deletes them.
	 * @since 1.9
	 */
	struct _CerverActionArgs *shared_args;
	/**
	 * @brief The response data shared with the data of each trigger, like #shared_args.
	 * @since 1.9
	 */
	struct _CerverActionArgs *shared_response;

};

/** @brief The action of an event of a client, see struct _ClientEvent. */
typedef struct _ClientEvent ClientEvent;

/**
 * @brief Registers an action to be triggered when an event occurs; an action that was registered for the event is
 * replaced (and deleted).
 *
 * The action gets a newly allocated ClientEventData, that it releases with client_event_data_delete ().
 *
 * @param[in,out] client the client
 * @param[in] event_type the event, below #CLIENT_MAX_EVENTS
 * @param[in] work the action
 * @param[in] work_args the arguments of the action
 * @param[in] delete_action_args releases the arguments when the action is unregistered, NULL to keep them
 * @param[in] create_thread true to run the action in its own thread
 * @param[in] drop_after_trigger true to unregister the action after its first trigger
 * @retval 0 the action was registered
 * @retval 1 a NULL client, an event out of range, or it could not be allocated
 */
CERVER_EXPORT u8 client_event_register (
	struct _Client *client,
	const ClientEventType event_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
);

/**
 * @brief Unregisters the action of an event; its arguments and its response are deleted with their delete actions
 * once every trigger that still runs is done with them.
 *
 * @param[in,out] client the client
 * @param[in] event_type the event
 * @retval 0 the action was unregistered
 * @retval 1 a NULL client, an event out of range, or the event does not have an action
 */
CERVER_EXPORT u8 client_event_unregister (
	struct _Client *client, const ClientEventType event_type
);

/**
 * @brief Sets the response data of the action of an event, that the event owns from now on.
 *
 * The response that it replaces is released once every trigger that still runs is done with it.
 *
 * @param[in,out] client the client
 * @param[in] event_type the event, one without an action is ignored
 * @param[in] response_data the data
 * @param[in] delete_response_data releases the data when the event and every trigger are done with it
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void client_event_set_response (
	struct _Client *client,
	const ClientEventType event_type,
	void *response_data, Action delete_response_data
);

/**
 * @brief Triggers the action that is registered to an event, if it has one.
 *
 * @param[in] event_type the event
 * @param[in] client the client
 * @param[in] connection the connection of the event, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void client_event_trigger (
	const ClientEventType event_type,
	const struct _Client *client, const struct _Connection *connection
);

/**
 * @brief Triggers the event with a response that only this trigger owns.
 *
 * client_event_data_delete () releases the response with delete_response (free () if it is NULL), and it is
 * released right away when the event has no action.
 *
 * @param[in] event_type the event
 * @param[in] client the client
 * @param[in] connection the connection of the event, it can be NULL
 * @param[in] response the response
 * @param[in] delete_response releases the response
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void client_event_trigger_with_response (
	const ClientEventType event_type,
	const struct _Client *client, const struct _Connection *connection,
	void *response, Action delete_response
);

/**
 * @brief What the action of an event gets, released with client_event_data_delete ().
 */
typedef struct ClientEventData {

	/** @brief The client. */
	const struct _Client *client;
	/** @brief The connection of the event, it can be NULL. */
	const struct _Connection *connection;

	/** @brief The data that came with the response. */
	void *response_data;
	/** @brief Releases #response_data when #owns_response_data. */
	Action delete_response_data;

	/**
	 * @brief client_event_data_delete () releases the response of this event only, like the LobbySummary of the
	 * lobby events.
	 */
	bool owns_response_data;

	/** @brief The arguments of the action. */
	void *action_args;
	/** @brief What releases #action_args, once the event and every trigger are done with them. */
	Action delete_action_args;

	/**
	 * @brief The event was dropped after this trigger: the reference of this data is the last one to the
	 * arguments, so client_event_data_delete () releases them.
	 */
	bool owns_action_args;

	/**
	 * @brief The reference of this trigger to the arguments of the action, for the internal methods of cerver:
	 * client_event_data_delete () releases it.
	 * @since 1.9
	 */
	struct _CerverActionArgs *shared_args;
	/**
	 * @brief The reference of this trigger to the response data of the event, like #shared_args.
	 * @since 1.9
	 */
	struct _CerverActionArgs *shared_response;

} ClientEventData;

/**
 * @brief Releases what the action of an event got: the response and the arguments that it owns, and the data.
 *
 * @param[in] event_data the data, it can be NULL
 */
CERVER_PUBLIC void client_event_data_delete (
	ClientEventData *event_data
);

/** @} */

/**
 * @defgroup client_errors Errors
 * @ingroup client
 * @brief The actions that run when a client gets an error.
 * @{
 */

/**
 * @brief The X-macro table of the errors of a client: the number, the name and the description of each one.
 */
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

/**
 * @brief The type of an error of a client, one `CLIENT_ERROR_<name>` for each row of #CLIENT_ERROR_MAP.
 */
typedef enum ClientErrorType {

	#define XX(num, name, description) CLIENT_ERROR_##name = num,
	CLIENT_ERROR_MAP (XX)
	#undef XX

} ClientErrorType;

/**
 * @brief Returns the description of an error type.
 *
 * @param[in] type the error type
 * @return the description of its row in #CLIENT_ERROR_MAP, or the one of CLIENT_ERROR_UNKNOWN for a value out of
 * the table; a static string that must not be freed
 */
CERVER_EXPORT const char *client_error_type_description (
	const ClientErrorType type
);

/**
 * @brief The action that the application registered for an error of a client.
 */
struct _ClientError {

	/** @brief The error that triggers the action. */
	ClientErrorType type;
	/** @brief The action runs in a thread of the event threads of the client, instead of the one that triggers it. */
	bool create_thread;
	/** @brief The action is unregistered after its first trigger. */
	bool drop_after_trigger;

	/** @brief The action, it gets a ClientErrorData. */
	Work work;
	/** @brief The arguments of the action. */
	void *work_args;
	/** @brief Releases the arguments once the error and every trigger are done with them, NULL to keep them. */
	Action delete_action_args;

	/**
	 * @brief The arguments shared with the data of each trigger, for the internal methods of cerver: the last one
	 * that releases them deletes them.
	 * @since 1.9
	 */
	struct _CerverActionArgs *shared_args;

};

/** @brief The action of an error of a client, see struct _ClientError. */
typedef struct _ClientError ClientError;

/**
 * @brief Registers an action to be triggered when an error occurs; an action that was registered for the error is
 * replaced (and deleted).
 *
 * The action gets a newly allocated ClientErrorData, that it releases with client_error_data_delete ().
 *
 * @param[in,out] client the client
 * @param[in] error_type the error, below #CLIENT_MAX_ERRORS
 * @param[in] work the action
 * @param[in] work_args the arguments of the action
 * @param[in] delete_action_args releases the arguments when the action is unregistered, NULL to keep them
 * @param[in] create_thread true to run the action in its own thread
 * @param[in] drop_after_trigger true to unregister the action after its first trigger
 * @retval 0 the action was registered
 * @retval 1 a NULL client, an error out of range, or it could not be allocated
 */
CERVER_EXPORT u8 client_error_register (
	struct _Client *client,
	const ClientErrorType error_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
);

/**
 * @brief Unregisters the action of an error; its arguments are deleted with its delete_action_args () once every
 * trigger that still runs is done with them.
 *
 * @param[in,out] client the client
 * @param[in] error_type the error
 * @retval 0 the action was unregistered
 * @retval 1 a NULL client, an error out of range, or the error does not have an action
 */
CERVER_EXPORT u8 client_error_unregister (
	struct _Client *client, const ClientErrorType error_type
);

/**
 * @brief Triggers the action that is registered to an error, if it has one.
 *
 * @param[in] error_type the error
 * @param[in] client the client
 * @param[in] connection the connection of the error, it can be NULL
 * @param[in] error_message the message of the error, copied
 * @retval 0 the action ran, or its thread was created
 * @retval 1 a NULL client, an error out of range, an error without an action, or its thread could not be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 client_error_trigger (
	const ClientErrorType error_type,
	const struct _Client *client, const struct _Connection *connection,
	const char *error_message
);

/**
 * @brief What the action of an error gets, released with client_error_data_delete ().
 */
typedef struct ClientErrorData {

	/** @brief The client. */
	const struct _Client *client;
	/** @brief The connection of the error, it can be NULL. */
	const struct _Connection *connection;

	/** @brief The arguments of the action. */
	void *action_args;

	/** @brief What releases #action_args, once the error and every trigger are done with them. */
	Action delete_action_args;
	/**
	 * @brief The error was dropped after this trigger: the reference of this data is the last one to the
	 * arguments, so client_error_data_delete () releases them.
	 */
	bool owns_action_args;

	/** @brief The message of the error. */
	String *error_message;

	/**
	 * @brief The reference of this trigger to the arguments of the action, for the internal methods of cerver:
	 * client_error_data_delete () releases it.
	 * @since 1.9
	 */
	struct _CerverActionArgs *shared_args;

} ClientErrorData;

/**
 * @brief Releases what the action of an error got: its message, the arguments that it owns, and the data.
 *
 * @param[in] error_data the data, it can be NULL
 */
CERVER_PUBLIC void client_error_data_delete (
	ClientErrorData *error_data
);

/**
 * @brief Creates an error packet, with the type and the message, ready to be sent.
 *
 * @param[in] type the error type
 * @param[in] msg the message
 * @return the new packet, NULL if it could not be created
 */
CERVER_PUBLIC struct _Packet *client_error_packet_generate (
	const ClientErrorType type, const char *msg
);

/**
 * @brief Creates an error packet and sends it to a connection.
 *
 * @param[in] type the error type
 * @param[in] msg the message
 * @param[in] client the client of the connection
 * @param[in] connection the connection
 * @retval 0 the packet was sent
 * @retval 1 the packet could not be created or sent
 */
CERVER_PUBLIC u8 client_error_packet_generate_and_send (
	const ClientErrorType type, const char *msg,
	Client *client, struct _Connection *connection
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
