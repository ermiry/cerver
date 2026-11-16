/**
 * @file cerver/handler/errors.h
 * @brief The errors of a cerver: the actions of the application that run when an error happens, and the error packets.
 */

#ifndef _CERVER_ERRORS_H_
#define _CERVER_ERRORS_H_

#include <stdbool.h>
#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/packets/serializer.h"

/**
 * @defgroup handler_errors Errors
 * @ingroup handler
 * @brief The errors of a cerver and the actions registered to them.
 * @{
 */

/** @brief The size of the buffer that gets the description of an errno with strerror_r (). */
#define CERVER_ERRNO_DESCRIPTION_SIZE			64

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Packet;
struct _Connection;

/** @brief The size of the array of the errors of a cerver: a valid error type is below it. */
#define CERVER_MAX_ERRORS				32

/**
 * @brief The X-macro table of the errors of a cerver: the number, the name and the description of each one.
 */
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
	XX(13,	DESTROY_LOBBY, 		The lobby could not be destroyed)				\
	XX(14,	UNKNOWN, 			Unknown error)

/**
 * @brief An error of a cerver, one `CERVER_ERROR_<name>` for each row of #CERVER_ERROR_MAP.
 */
typedef enum CerverErrorType {

	#define XX(num, name, description) CERVER_ERROR_##name = num,
	CERVER_ERROR_MAP (XX)
	#undef XX

} CerverErrorType;

/**
 * @brief Returns the description of an error.
 *
 * @param[in] type the error
 * @return the description of its row in #CERVER_ERROR_MAP, or the one of CERVER_ERROR_UNKNOWN for a value out of the
 * table; a static string that must not be freed
 */
CERVER_EXPORT const char *cerver_error_type_description (
	const CerverErrorType type
);

/**
 * @brief An action registered to an error.
 */
typedef struct CerverErrorEvent {

	/** @brief The error that triggers the action. */
	CerverErrorType type;

	/** @brief The action runs in its own thread, that the teardown waits for. */
	bool create_thread;
	/** @brief The action is unregistered after its first trigger. */
	bool drop_after_trigger;

	/** @brief The action. */
	Work work;
	/** @brief The arguments of the action. */
	void *work_args;
	/** @brief Releases the arguments when the error event and every trigger of it are done with them. */
	Action delete_action_args;

	/**
	 * @brief The arguments shared with the data of each trigger, for the internal methods of cerver: the last one
	 * that releases them deletes them.
	 * @since 1.9b-2
	 */
	struct _CerverActionArgs *shared_args;

} CerverErrorEvent;

/**
 * @brief Releases an error event and its arguments, with its delete_action_args.
 *
 * @param[in] event_ptr the error event, it can be NULL
 */
CERVER_PUBLIC void cerver_error_event_delete (void *event_ptr);

/**
 * @brief Registers an action to be triggered when an error happens; an action that was already registered to the
 * error is replaced (and its arguments released).
 *
 * The action gets a new CerverErrorEventData, that it must release with cerver_error_event_data_delete ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] error_type the error, below #CERVER_MAX_ERRORS
 * @param[in] work the action
 * @param[in] work_args the arguments of the action
 * @param[in] delete_action_args releases the arguments, NULL to keep them
 * @param[in] create_thread true to run the action in its own thread, false to run it in the thread of the error
 * @param[in] drop_after_trigger true to unregister the action after its first trigger
 * @retval 0 the action was registered
 * @retval 1 a NULL cerver, an error out of range or it could not be allocated
 * @note The arguments are shared by the error event and the data of each trigger: replacing or unregistering the
 * action while a thread of a previous trigger still runs it keeps them until that data is deleted.
 */
CERVER_EXPORT u8 cerver_error_event_register (
	struct _Cerver *cerver,
	const CerverErrorType error_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
);

/**
 * @brief Unregisters the action of an error; its arguments are released with its delete_action_args once every
 * trigger that still runs is done with them.
 *
 * @param[in,out] cerver the cerver
 * @param[in] error_type the error
 * @retval 0 the action was unregistered
 * @retval 1 a NULL cerver, an error out of range or one without an action
 */
CERVER_EXPORT u8 cerver_error_event_unregister (
	struct _Cerver *cerver, const CerverErrorType error_type
);

/**
 * @brief Triggers the action that is registered to an error.
 *
 * @param[in] error_type the error
 * @param[in] cerver the cerver
 * @param[in] client the client of the error, it can be NULL
 * @param[in] connection the connection of the error, it can be NULL
 * @param[in] error_message the message, copied into the data of the trigger; it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_error_event_trigger (
	const CerverErrorType error_type,
	const struct _Cerver *cerver,
	const struct _Client *client,
	const struct _Connection *connection,
	const char *error_message
);

/** @} */

/**
 * @defgroup handler_errors_data Error data
 * @ingroup handler
 * @brief What the action of an error gets.
 * @{
 */

/**
 * @brief What the action of an error gets.
 */
typedef struct CerverErrorEventData {

	/** @brief The cerver of the error. */
	const struct _Cerver *cerver;

	/** @brief The client of the error, NULL if it does not have one. */
	const struct _Client *client;
	/** @brief The connection of the error, NULL if it does not have one. */
	const struct _Connection *connection;

	/** @brief The arguments of the action. */
	void *action_args;

	/** @brief Releases the arguments of the action when the last reference to them is released. */
	Action delete_action_args;
	/**
	 * @brief The error was dropped after this trigger: cerver_error_event_data_delete () releases the last reference
	 * to the arguments of the action, so it deletes them with delete_action_args ().
	 */
	bool owns_action_args;

	/** @brief The message of the error, NULL if it does not have one. */
	String *error_message;

	/**
	 * @brief The reference of this trigger to the arguments of the action, for the internal methods of cerver:
	 * cerver_error_event_data_delete () releases it.
	 * @since 1.9b-2
	 */
	struct _CerverActionArgs *shared_args;

} CerverErrorEventData;

/**
 * @brief Releases the data of an error: its message, and the arguments of the action if it owns them.
 *
 * @param[in] error_event_data the data, it can be NULL
 */
CERVER_PUBLIC void cerver_error_event_data_delete (
	CerverErrorEventData *error_event_data
);

/** @} */

/**
 * @defgroup handler_errors_packets Error packets
 * @ingroup handler
 * @brief The packets that send an error to a peer, and how they are handled.
 * @{
 */

/**
 * @brief Handles a received error packet: it triggers the error of its type.
 *
 * CERVER_ERROR_PACKET_ERROR, CERVER_ERROR_GET_FILE, CERVER_ERROR_SEND_FILE and CERVER_ERROR_FILE_NOT_FOUND trigger
 * their own error, CERVER_ERROR_NONE nothing, and every other type CERVER_ERROR_UNKNOWN; a packet that is not a valid
 * ErrorMessage is ignored.
 *
 * @param[in] packet the error packet
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_error_packet_handler (
	struct _Packet *packet
);

/**
 * @brief Creates an error packet ready to be sent, like error_message_packet_create ().
 *
 * @param[in] type the error
 * @param[in] msg the message, it can be NULL
 * @return the new packet, NULL on error
 */
CERVER_PUBLIC struct _Packet *error_packet_generate (
	const CerverErrorType type, const char *msg
);

/**
 * @brief Creates an error packet and sends it to a connection.
 *
 * @param[in] type the error
 * @param[in] msg the message, it can be NULL
 * @param[in] cerver the cerver
 * @param[in] client the client
 * @param[in] connection the connection
 * @retval 0 the packet was sent
 * @retval 1 the packet could not be created or sent
 */
CERVER_PUBLIC u8 error_packet_generate_and_send (
	const CerverErrorType type, const char *msg,
	struct _Cerver *cerver,
	struct _Client *client,
	struct _Connection *connection
);

/** @brief The longest message of an error packet, with its NUL. */
#define ERROR_MESSAGE_LENGTH        128

/** @brief The bytes of the map, the version, the keys, the timestamp, the type and the length of the message. */
#define ERROR_MESSAGE_ENCODING_OVERHEAD		32

/** @brief The biggest encoded error message. */
#define ERROR_MESSAGE_MAX_ENCODED_SIZE		(ERROR_MESSAGE_LENGTH + ERROR_MESSAGE_ENCODING_OVERHEAD)

/** @brief The version of the schema of the error message. */
#define ERROR_MESSAGE_VERSION				1

/**
 * @brief The X-macro table of the fields of the data of an error packet, the timestamp in seconds since the epoch.
 *
 * SERIALIZER_DECLARE () makes the struct ErrorMessage and the schema error_message_schema from it.
 */
#define ERROR_MESSAGE_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	SCALAR(T, 1, I64, timestamp, SERIALIZER_OPTIONAL, 0)							\
	SCALAR(T, 2, U32, error_type, SERIALIZER_REQUIRED, 0)							\
	STRING(T, 3, TEXT, msg, ERROR_MESSAGE_LENGTH, SERIALIZER_OPTIONAL)

SERIALIZER_DECLARE (ErrorMessage, error_message, ERROR_MESSAGE_FIELDS)

/**
 * @brief Creates an error packet (PACKET_TYPE_ERROR) ready to be sent, with the type, the message and the time.
 *
 * @param[in] error_type the error
 * @param[in] msg the message, it can be NULL; a message longer than #ERROR_MESSAGE_LENGTH is cut without splitting a
 * character
 * @return the new packet, NULL on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE struct _Packet *error_message_packet_create (
	const u32 error_type, const char *msg
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
