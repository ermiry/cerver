#ifndef _CERVER_RECEIVE_H_
#define _CERVER_RECEIVE_H_

#include <stddef.h>

#include "cerver/config.h"
#include "cerver/packets.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

struct _Socket;
struct _Connection;
struct _Client;
struct _Admin;

struct _Lobby;

#define RECEIVE_ERROR_MAP(XX)			\
	XX(0,	NONE,		None)			\
	XX(1,	TIMEOUT,	Timeoout)		\
	XX(2,	EMPTY,		Empty)			\
	XX(3,	FAILED,		Failed)

typedef enum ReceiveError {

	#define XX(num, name, string) RECEIVE_ERROR_##name = num,
	RECEIVE_ERROR_MAP (XX)
	#undef XX

} ReceiveError;

CERVER_PUBLIC const char *receive_error_to_string (
	const ReceiveError error
);

#define RECEIVE_TYPE_MAP(XX)			\
	XX(0,	NONE,		None)			\
	XX(1,	NORMAL,		Normal)			\
	XX(2,	ON_HOLD,	On-Hold)		\
	XX(3,	ADMIN,		Admin)

typedef enum ReceiveType {

	#define XX(num, name, string) RECEIVE_TYPE_##name = num,
	RECEIVE_TYPE_MAP (XX)
	#undef XX

} ReceiveType;

CERVER_PUBLIC const char *receive_type_to_string (
	const ReceiveType type
);

#define RECEIVE_HANDLE_STATE_MAP(XX)			\
	XX(0,	NONE, 			None)				\
	XX(1,	NORMAL, 		Normal)				\
	XX(2,	SPLIT_HEADER, 	Split-Header)		\
	XX(3,	SPLIT_PACKET, 	Split-Packet)		\
	XX(4,	COMP_HEADER, 	Complete-Header)	\
	XX(5,	LOST, 			Lost)

typedef enum ReceiveHandleState {

	#define XX(num, name, string) RECEIVE_HANDLE_STATE_##name = num,
	RECEIVE_HANDLE_STATE_MAP (XX)
	#undef XX

} ReceiveHandleState;

CERVER_PUBLIC const char *receive_handle_state_to_string (
	const ReceiveHandleState state
);

struct _ReceiveHandle {

	ReceiveType type;

	struct _Cerver *cerver;

	struct _Socket *socket;
	struct _Connection *connection;
	struct _Client *client;
	struct _Admin *admin;

	struct _Lobby *lobby;

	char *buffer;
	size_t buffer_size;
	size_t received_size;

	ReceiveHandleState state;

	// used to handle split headers between buffers
	// this can happen when the header is at the buffer's end
	PacketHeader header;
	char *header_end;
	unsigned int remaining_header;

	struct _Packet *spare_packet;

	// the bytes of the buffer that come after the packet that is being dispatched,
	// like the contents of a file that are sent after its packet,
	// the handler takes them with receive_handle_take_trailing ()
	const char *trailing;
	size_t trailing_size;
	size_t trailing_taken;

};

typedef struct _ReceiveHandle ReceiveHandle;

CERVER_PRIVATE ReceiveHandle *receive_handle_new (void);

CERVER_PRIVATE void receive_handle_delete (void *receive_ptr);

// assembles the packets of the received buffer, no matter where the buffers are split
// every complete packet is passed to dispatch, that returns 0 to keep handling the buffer,
// or 1 to stop (when the connection was deleted, its receive handle is not used anymore)
// a packet with an invalid size (smaller than its header or bigger than max_packet_size)
// closes the connection
CERVER_PRIVATE void receive_handle_buffer (
	ReceiveHandle *receive_handle, const size_t max_packet_size,
	u8 (*dispatch) (ReceiveHandle *receive_handle, struct _Packet *packet)
);

// releases the packet that was being assembled
CERVER_PRIVATE void receive_handle_end (ReceiveHandle *receive_handle);

// takes up to max_size of the bytes that come after the packet that is being dispatched,
// they are not handled as packets after the dispatch
// it can only be called from the dispatch of the packet
// returns the number of bytes in data
CERVER_PRIVATE size_t receive_handle_take_trailing (
	ReceiveHandle *receive_handle,
	const char **data, const size_t max_size
);

#ifdef __cplusplus
}
#endif

#endif