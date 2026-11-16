/**
 * @file cerver/handler/receive.h
 * @brief The receive handle of a connection: how the received buffers are assembled into packets.
 */

#ifndef _CERVER_RECEIVE_H_
#define _CERVER_RECEIVE_H_

#include <stddef.h>

#include "cerver/config.h"

#include "cerver/packets/packets.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

struct _Socket;
struct _Connection;
struct _Client;
struct _Admin;

struct _Lobby;

/**
 * @defgroup handler_receive Receive
 * @ingroup handler
 * @brief The packets that are assembled from the received buffers, no matter where the buffers are split.
 * @{
 */

/**
 * @brief The X-macro table of the errors of a receive: the number, the name and the string of each one.
 */
#define RECEIVE_ERROR_MAP(XX)			\
	XX(0,	NONE,		None)			\
	XX(1,	TIMEOUT,	Timeout)		\
	XX(2,	EMPTY,		Empty)			\
	XX(3,	FAILED,		Failed)

/**
 * @brief An error of a receive, one `RECEIVE_ERROR_<name>` for each row of #RECEIVE_ERROR_MAP.
 */
typedef enum ReceiveError {

	#define XX(num, name, string) RECEIVE_ERROR_##name = num,
	RECEIVE_ERROR_MAP (XX)
	#undef XX

} ReceiveError;

/**
 * @brief Returns the string of an error of a receive.
 *
 * @param[in] error the error
 * @return the string of its row in #RECEIVE_ERROR_MAP, or the one of RECEIVE_ERROR_NONE for a value out of the
 * table; a static string that must not be freed
 */
CERVER_PUBLIC const char *receive_error_to_string (
	const ReceiveError error
);

/**
 * @brief The X-macro table of the types of a receive: the number, the name and the string of each one.
 */
#define RECEIVE_TYPE_MAP(XX)			\
	XX(0,	NONE,		None)			\
	XX(1,	NORMAL,		Normal)			\
	XX(2,	ON_HOLD,	On-Hold)		\
	XX(3,	ADMIN,		Admin)

/**
 * @brief What a receive is for, one `RECEIVE_TYPE_<name>` for each row of #RECEIVE_TYPE_MAP.
 */
typedef enum ReceiveType {

	#define XX(num, name, string) RECEIVE_TYPE_##name = num,
	RECEIVE_TYPE_MAP (XX)
	#undef XX

} ReceiveType;

/**
 * @brief Returns the string of a type of receive.
 *
 * @param[in] type the type
 * @return the string of its row in #RECEIVE_TYPE_MAP, or the one of RECEIVE_TYPE_NONE for a value out of the table;
 * a static string that must not be freed
 */
CERVER_PUBLIC const char *receive_type_to_string (
	const ReceiveType type
);

/**
 * @brief The X-macro table of the states of a receive handle: the number, the name and the string of each one.
 */
#define RECEIVE_HANDLE_STATE_MAP(XX)			\
	XX(0,	NONE, 			None)				\
	XX(1,	NORMAL, 		Normal)				\
	XX(2,	SPLIT_HEADER, 	Split-Header)		\
	XX(3,	SPLIT_PACKET, 	Split-Packet)		\
	XX(4,	COMP_HEADER, 	Complete-Header)	\
	XX(5,	LOST, 			Lost)

/**
 * @brief The state of a receive handle between two buffers, one `RECEIVE_HANDLE_STATE_<name>` for each row of
 * #RECEIVE_HANDLE_STATE_MAP.
 */
typedef enum ReceiveHandleState {

	#define XX(num, name, string) RECEIVE_HANDLE_STATE_##name = num,
	RECEIVE_HANDLE_STATE_MAP (XX)
	#undef XX

} ReceiveHandleState;

/**
 * @brief Returns the string of a state of a receive handle.
 *
 * @param[in] state the state
 * @return the string of its row in #RECEIVE_HANDLE_STATE_MAP, or the one of RECEIVE_HANDLE_STATE_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *receive_handle_state_to_string (
	const ReceiveHandleState state
);

/**
 * @brief The receive handle of a connection: the buffer that was received and the packet that is being assembled.
 */
struct _ReceiveHandle {

	/** @brief What the receive is for. */
	ReceiveType type;

	/** @brief The cerver. */
	struct _Cerver *cerver;

	/** @brief The socket that the buffer came from. */
	struct _Socket *socket;
	/** @brief The connection. */
	struct _Connection *connection;
	/** @brief The client of the connection. */
	struct _Client *client;
	/** @brief The admin of the connection. */
	struct _Admin *admin;

	/** @brief The lobby of the connection. */
	struct _Lobby *lobby;

	/** @brief The received buffer. */
	char *buffer;
	/** @brief The size of the buffer. */
	size_t buffer_size;
	/** @brief The bytes that were received in the buffer. */
	size_t received_size;

	/** @brief The state between two buffers. */
	ReceiveHandleState state;

	/** @brief The part of a header that came at the end of a buffer, completed with the next one. */
	u8 header[PACKET_HEADER_SIZE];
	/** @brief The end of the part of the header. */
	char *header_end;
	/** @brief The bytes that are missing of the header. */
	unsigned int remaining_header;

	/** @brief The packet that is being assembled. */
	struct _Packet *spare_packet;

	/**
	 * @brief The bytes of the buffer that come after the packet that is being dispatched, like the contents of a file
	 * that are sent after its packet; the handler takes them with receive_handle_take_trailing ().
	 */
	const char *trailing;
	/** @brief The size of the trailing bytes. */
	size_t trailing_size;
	/** @brief The trailing bytes that were taken. */
	size_t trailing_taken;

};

/** @brief The receive handle of a connection, see struct _ReceiveHandle. */
typedef struct _ReceiveHandle ReceiveHandle;

/**
 * @brief Allocates an empty receive handle.
 *
 * @return the new receive handle, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE ReceiveHandle *receive_handle_new (void);

/**
 * @brief Releases a receive handle, not the packet that it assembles (see receive_handle_end ()).
 *
 * @param[in] receive_ptr the receive handle, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void receive_handle_delete (void *receive_ptr);

/**
 * @brief Assembles the packets of the received buffer, no matter where the buffers are split.
 *
 * Every complete packet is passed to dispatch; a packet with an invalid size (smaller than its header or bigger than
 * max_packet_size) closes the connection, and a handle in RECEIVE_HANDLE_STATE_LOST discards what it receives.
 *
 * @param[in,out] receive_handle the receive handle, with the received buffer
 * @param[in] max_packet_size the biggest packet
 * @param[in] dispatch gets every complete packet, and returns 0 to keep handling the buffer or 1 to stop (when the
 * connection was deleted, its receive handle is not used anymore)
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void receive_handle_buffer (
	ReceiveHandle *receive_handle, const size_t max_packet_size,
	u8 (*dispatch) (ReceiveHandle *receive_handle, struct _Packet *packet)
);

/**
 * @brief Releases the packet that was being assembled and resets the state.
 *
 * @param[in,out] receive_handle the receive handle, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void receive_handle_end (ReceiveHandle *receive_handle);

/**
 * @brief Takes some of the bytes that come after the packet that is being dispatched; they are not handled as
 * packets after the dispatch.
 *
 * @param[in,out] receive_handle the receive handle
 * @param[out] data where the bytes start
 * @param[in] max_size the most bytes to take
 * @return the number of bytes in data, 0 if there are none
 * @note It can only be called from the dispatch of the packet.
 */
CERVER_PRIVATE size_t receive_handle_take_trailing (
	ReceiveHandle *receive_handle,
	const char **data, const size_t max_size
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
