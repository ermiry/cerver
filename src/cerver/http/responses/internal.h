/**
 * @file http/responses/internal.h
 * @internal
 * @brief The methods that the parts of the responses share, never exported by libcerver.so.
 */

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

/**
 * @internal
 * @brief Works as http_response_send_pieces () for a complete response that is not compiled, and counts it with
 * its status, like the 101 of an upgrade.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status of the response
 * @param[in] parts the pieces of the response
 * @param[in] n_parts the number of pieces
 * @retval 0 the response was sent
 * @retval 1 it was not sent
 */
CERVER_HIDDEN u8 http_response_send_pieces_status (
	const struct _HttpReceive *http_receive, const http_status status,
	struct iovec *parts, const int n_parts
);

#ifdef __cplusplus
}
#endif

#endif
