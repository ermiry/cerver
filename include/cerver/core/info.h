#ifndef _CERVER_CORE_INFO_H_
#define _CERVER_CORE_INFO_H_

#include <stddef.h>
#include <time.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;
struct _Packet;

#pragma region info

#define CERVER_INFO_NAME_SIZE				128
#define CERVER_INFO_ALIAS_SIZE				32
#define CERVER_INFO_WELCOME_SIZE			512

typedef struct CerverInfo {

	size_t name_len;
	char name[CERVER_INFO_NAME_SIZE];

	size_t alias_len;
	char alias[CERVER_INFO_ALIAS_SIZE];

	// this message is sent to the client when it first connects
	size_t welcome_len;
	char welcome[CERVER_INFO_WELCOME_SIZE];
	struct _Packet *cerver_info_packet;

	time_t time_started;					// the actual time the cerver was started
	u64 uptime;								// the seconds the cerver has been up

} CerverInfo;

// sets the cerver's alias
// to be used primarily to handle cerver's related threads names
// as they must not exceed a certain size
CERVER_EXPORT void cerver_set_alias (
	struct _Cerver *cerver, const char *alias
);

// sets the cerver msg to be sent when a client connects
// retuns 0 on success, 1 on error
CERVER_EXPORT u8 cerver_set_welcome_msg (
	struct _Cerver *cerver, const char *msg
);

CERVER_PRIVATE void cerver_update_uptime (
	struct _Cerver *cerver
);

// sends the cerver info packet
// retuns 0 on success, 1 on error
CERVER_PUBLIC u8 cerver_info_send_info_packet (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
