/**
 * @file cerver/handler/events.h
 * @brief The events of a cerver: the actions of the application that run when something happens.
 */

#ifndef _CERVER_EVENTS_H_
#define _CERVER_EVENTS_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;
struct _Lobby;

/**
 * @defgroup handler_events Events
 * @ingroup handler
 * @brief The events of a cerver and the actions registered to them.
 * @{
 */

/** @brief The size of the array of the events of a cerver: a valid event type is below it. */
#define CERVER_MAX_EVENTS				32

/**
 * @brief The X-macro table of the events of a cerver: the number, the name and the description of each one.
 */
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
	XX(24,	LOBBY_DESTROY, 				A lobby was destroyed)										\
	XX(25,	UNKNOWN, 					Unknown event)

/**
 * @brief An event of a cerver, one `CERVER_EVENT_<name>` for each row of #CERVER_EVENT_MAP.
 */
typedef enum CerverEventType {

	#define XX(num, name, description) CERVER_EVENT_##name = num,
	CERVER_EVENT_MAP (XX)
	#undef XX

} CerverEventType;

/**
 * @brief Returns the description of an event.
 *
 * @param[in] type the event
 * @return the description of its row in #CERVER_EVENT_MAP, or the one of CERVER_EVENT_UNKNOWN for a value out of the
 * table; a static string that must not be freed
 */
CERVER_EXPORT const char *cerver_event_type_description (
	const CerverEventType type
);

/**
 * @brief An action registered to an event.
 */
typedef struct CerverEvent {

	/** @brief The event that triggers the action. */
	CerverEventType type;

	/** @brief The action runs in its own thread, that the teardown waits for. */
	bool create_thread;
	/** @brief The action is unregistered after its first trigger. */
	bool drop_after_trigger;

	/** @brief The action. */
	Work work;
	/** @brief The arguments of the action. */
	void *work_args;
	/** @brief Releases the arguments when the event and every trigger of it are done with them. */
	Action delete_action_args;

	/**
	 * @brief The arguments shared with the data of each trigger, for the internal methods of cerver: the last one
	 * that releases them deletes them.
	 * @since 1.9b-2
	 */
	struct _CerverActionArgs *shared_args;

} CerverEvent;

/**
 * @brief Releases an event and its arguments, with its delete_action_args.
 *
 * @param[in] event_ptr the event, it can be NULL
 */
CERVER_PUBLIC void cerver_event_delete (void *event_ptr);

/**
 * @brief Registers an action to be triggered when an event happens; an action that was already registered to the
 * event is replaced (and its arguments released).
 *
 * The action gets a new CerverEventData, that it must release with cerver_event_data_delete ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] event_type the event, below #CERVER_MAX_EVENTS
 * @param[in] work the action
 * @param[in] work_args the arguments of the action
 * @param[in] delete_action_args releases the arguments, NULL to keep them
 * @param[in] create_thread true to run the action in its own thread, false to run it in the thread of the event
 * @param[in] drop_after_trigger true to unregister the action after its first trigger
 * @retval 0 the action was registered
 * @retval 1 a NULL cerver, an event out of range or it could not be allocated
 * @note The arguments are shared by the event and the data of each trigger: replacing or unregistering the action
 * while a thread of a previous trigger still runs it keeps them until that data is deleted.
 */
CERVER_EXPORT u8 cerver_event_register (
	struct _Cerver *cerver,
	const CerverEventType event_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
);

/**
 * @brief Unregisters the action of an event; its arguments are released with its delete_action_args once every
 * trigger that still runs is done with them.
 *
 * @param[in,out] cerver the cerver
 * @param[in] event_type the event
 * @retval 0 the action was unregistered
 * @retval 1 a NULL cerver, an event out of range or one without an action
 */
CERVER_EXPORT u8 cerver_event_unregister (
	struct _Cerver *cerver, const CerverEventType event_type
);

/**
 * @brief Triggers the action that is registered to an event.
 *
 * @param[in] event_type the event
 * @param[in] cerver the cerver
 * @param[in] client the client of the event, it can be NULL
 * @param[in] connection the connection of the event, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_event_trigger (
	const CerverEventType event_type,
	const struct _Cerver *cerver,
	const struct _Client *client,
	const struct _Connection *connection
);

/**
 * @brief Triggers the action of a lobby event; the event data keeps a reference to the lobby.
 *
 * @param[in] event_type the event
 * @param[in] cerver the cerver
 * @param[in] client the client of the event, it can be NULL
 * @param[in] connection the connection of the event, it can be NULL
 * @param[in] lobby the lobby
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_event_trigger_lobby (
	const CerverEventType event_type,
	const struct _Cerver *cerver,
	const struct _Client *client,
	const struct _Connection *connection,
	struct _Lobby *lobby
);

/** @} */

/**
 * @defgroup handler_events_data Event data
 * @ingroup handler
 * @brief What the action of an event gets.
 * @{
 */

/**
 * @brief What the action of an event gets.
 */
typedef struct CerverEventData {

	/** @brief The cerver of the event. */
	const struct _Cerver *cerver;

	/** @brief The client of the event, NULL if it does not have one. */
	const struct _Client *client;
	/** @brief The connection of the event, NULL if it does not have one. */
	const struct _Connection *connection;

	/**
	 * @brief The lobby of the lobby events, NULL in the others; it stays valid until cerver_event_data_delete ()
	 * releases its reference.
	 */
	struct _Lobby *lobby;

	/** @brief The arguments of the action. */
	void *action_args;
	/** @brief Releases the arguments of the action. */
	Action delete_action_args;

	/**
	 * @brief The event was dropped after this trigger: cerver_event_data_delete () releases the last reference to the
	 * arguments of the action, so it deletes them with delete_action_args ().
	 */
	bool owns_action_args;

	/**
	 * @brief The reference of this trigger to the arguments of the action, for the internal methods of cerver:
	 * cerver_event_data_delete () releases it, and the arguments are deleted when the event and every trigger are
	 * done with them.
	 * @since 1.9b-2
	 */
	struct _CerverActionArgs *shared_args;

} CerverEventData;

/**
 * @brief Releases the data of an event: its reference to the lobby, and the arguments of the action if it owns them.
 *
 * @param[in] event_data the data, it can be NULL
 */
CERVER_PUBLIC void cerver_event_data_delete (
	CerverEventData *event_data
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
