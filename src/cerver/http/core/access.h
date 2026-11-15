#ifndef _CERVER_HTTP_ACCESS_H_
#define _CERVER_HTTP_ACCESS_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

// the address of the client and the one of the proxy that forwarded its request
#define HTTP_ACCESS_CLIENT_SIZE			((2 * CONNECTION_IP_SIZE) + 8)

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpReceive;

// accounts for a request that ended: its line in the log (its client, its method,
// its path, the bytes that were received, the time that it took, its status
// and the bytes that were sent) and the stats of its endpoint and of its cerver
// it is called once for each request, before its data is released
CERVER_PRIVATE void http_access_request (
	const struct _HttpReceive *http_receive
);

#ifdef __cplusplus
}
#endif

#endif
