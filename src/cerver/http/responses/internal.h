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

struct _HttpCerver;
struct _HttpReceive;

/**
 * @internal
 * @brief Compiles the header of a response with the default headers of an HttpCerver, the one of its request.
 *
 * http_response_compile_header () takes the ones of the producer cerver of the process instead: with two HTTP
 * cervers in a process, a response would have the headers of the first one that is alive.
 *
 * @param[in,out] response the response, it must not be NULL
 * @param[in] http_cerver the cerver of the request; NULL for none of the default headers
 */
CERVER_HIDDEN void http_response_compile_header_with_cerver (
	HttpResponse *response, const struct _HttpCerver *http_cerver
);

/**
 * @internal
 * @brief Works like http_response_compile (), with the default headers of an HttpCerver, the one of its request.
 *
 * @param[in,out] response the response, it can be NULL
 * @param[in] http_cerver the cerver of the request; NULL for none of the default headers
 * @retval 0 the response was compiled
 * @retval 1 a NULL response or its buffer could not be allocated
 */
CERVER_HIDDEN u8 http_response_compile_with_cerver (
	HttpResponse *response, const struct _HttpCerver *http_cerver
);

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

/**
 * @internal
 * @brief Sends bytes that are not the response of the request, like "100 Continue": the request is not marked as
 * answered and they are not counted as its response.
 *
 * A thread that handles the request waits for a full socket until the deadline of its response; the thread of an
 * event loop never waits, its connection keeps what the socket does not take.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] data the bytes
 * @param[in] length the number of bytes
 * @retval 0 they were sent or kept
 * @retval 1 they were not, errno tells why
 */
CERVER_HIDDEN u8 http_response_send_interim (
	const struct _HttpReceive *http_receive, const char *data, const size_t length
);

/**
 * @internal
 * @brief Answers a request of an event loop that the cerver can not run with #HTTP_RESPONSE_SERVICE_UNAVAILABLE,
 * and counts it with its status.
 *
 * @param[in] http_receive the receive of the request
 * @retval 0 the response was sent
 * @retval 1 it was not sent
 */
CERVER_HIDDEN u8 http_response_send_unavailable (const struct _HttpReceive *http_receive);

#ifdef __cplusplus
}
#endif

#endif
