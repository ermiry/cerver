#ifndef _CERVER_HTTP_RESPONSES_INTERNAL_H_
#define _CERVER_HTTP_RESPONSES_INTERNAL_H_

#include <sys/uio.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/responses/response.h"
#include "cerver/http/responses/status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpReceive;

// works as http_response_send_pieces () for a complete response that is not compiled,
// it is counted with its status, like a 101 of an upgrade
CERVER_HIDDEN u8 http_response_send_pieces_status (
	const struct _HttpReceive *http_receive, const http_status status,
	struct iovec *parts, const int n_parts
);

#ifdef __cplusplus
}
#endif

#endif
