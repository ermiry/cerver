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
	const struct _HttpReceive *http_receive, const CerverHttpStatus status,
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

/**
 * @internal
 * @brief Takes the response that the handler of a request wrote in its context (its status and its headers): the
 * context does not have it anymore, and whoever takes it releases it with http_response_delete ().
 *
 * @param[in,out] context the context of the request
 * @return the response, NULL when the handler wrote none
 */
CERVER_HIDDEN struct _HttpResponse *http_response_writer_take (struct _HttpReceive *context);

/**
 * @internal
 * @brief Gives back the response that the handler of a request wrote in its context and did not send; the request
 * ends with it.
 *
 * @param[in,out] context the context of the request
 */
CERVER_HIDDEN void http_response_writer_release (struct _HttpReceive *context);

#ifdef __cplusplus
}
#endif

#endif
