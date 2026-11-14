#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/version.h"

#include "cerver/connection/connection.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"
#include "cerver/system/timer.h"

#include "cerver/crypto/memory.h"

#include "cerver/http/core/custom.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/limits.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/json/json.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"
#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"
#include "cerver/http/requests/method.h"
#include "cerver/http/requests/multipart.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/requests/url.h"
#include "cerver/http/responses/compression.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/responses/stream.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/security/auth.h"
#include "cerver/http/security/proxy.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"
#include "cerver/http/websockets/websocket.h"

#include "cerver/threads/worker.h"

#include "cerver/utils/base64.h"
#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region limits

// the limits are tuned by measuring each service, always with powers of 2
static void http_cerver_limit_check_power_of_two (
	const char *limit_name, const size_t value
) {

	if (value && (value & (value - 1))) {
		cerver_log_warning (
			"http_cerver_set_%s () - %zu is not a power of 2",
			limit_name, value
		);
	}

}

void http_cerver_set_max_body_size (
	HttpCerver *http_cerver, const size_t max_body_size
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_body_size", max_body_size);
		http_cerver->max_body_size = max_body_size;
	}

}

void http_cerver_set_max_url_size (
	HttpCerver *http_cerver, const size_t max_url_size
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_url_size", max_url_size);
		http_cerver->max_url_size = max_url_size ?
			max_url_size : HTTP_CERVER_DEFAULT_MAX_URL_SIZE;
	}

}

void http_cerver_set_max_header_size (
	HttpCerver *http_cerver, const size_t max_header_size
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_header_size", max_header_size);
		http_cerver->max_header_size = max_header_size ?
			max_header_size : HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE;
	}

}

void http_cerver_set_max_headers (
	HttpCerver *http_cerver, const unsigned int max_headers
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_headers", max_headers);
		http_cerver->max_headers = max_headers;
	}

}

void http_cerver_set_max_custom_header_size (
	HttpCerver *http_cerver, const size_t max_custom_header_size
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_custom_header_size", max_custom_header_size);
		http_cerver->max_custom_header_size = max_custom_header_size;
	}

}

void http_cerver_set_max_multipart_parts (
	HttpCerver *http_cerver, const unsigned int max_multipart_parts
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_multipart_parts", max_multipart_parts);
		http_cerver->max_multipart_parts = max_multipart_parts;
	}

}

void http_cerver_set_max_file_size (
	HttpCerver *http_cerver, const size_t max_file_size
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_file_size", max_file_size);
		http_cerver->max_file_size = max_file_size;
	}

}

// the milliseconds of a timeout in secs, the most that fits in an unsigned int
static unsigned int http_cerver_timeout_milliseconds (const unsigned int timeout) {

	const unsigned int max_seconds = UINT_MAX / HTTP_MILLISECONDS_PER_SECOND;

	return (timeout > max_seconds) ? UINT_MAX : timeout * HTTP_MILLISECONDS_PER_SECOND;

}

void http_cerver_set_headers_timeout (
	HttpCerver *http_cerver, const unsigned int headers_timeout
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("headers_timeout", headers_timeout);
		http_cerver->headers_timeout = http_cerver_timeout_milliseconds (headers_timeout);
	}

}

void http_cerver_set_headers_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int headers_timeout
) {

	if (http_cerver) http_cerver->headers_timeout = headers_timeout;

}

void http_cerver_set_request_timeout (
	HttpCerver *http_cerver, const unsigned int request_timeout
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("request_timeout", request_timeout);
		http_cerver->request_timeout = http_cerver_timeout_milliseconds (request_timeout);
	}

}

void http_cerver_set_request_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int request_timeout
) {

	if (http_cerver) http_cerver->request_timeout = request_timeout;

}

void http_cerver_set_idle_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int idle_timeout
) {

	if (http_cerver) {
		http_cerver->idle_timeout = idle_timeout ?
			idle_timeout : HTTP_CERVER_DEFAULT_IDLE_TIMEOUT_MILLISECONDS;
	}

}

void http_cerver_set_send_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int send_timeout
) {

	if (http_cerver) http_cerver->send_timeout = send_timeout;

}

void http_cerver_set_half_close_is_gone (
	HttpCerver *http_cerver, const bool half_close_is_gone
) {

	if (http_cerver) http_cerver->half_close_is_gone = half_close_is_gone;

}

static void http_cerver_parser_lenient_warning (
	const HttpCerver *http_cerver, const HttpParserLenient lenient
) {

	if (http_cerver->parser_lenient & (unsigned int) lenient) {
		cerver_log_warning (
			"HTTP cerver - the parser accepts %s (lenient %s), "
			"it can open the cerver to request smuggling or cache poisoning attacks",
			http_parser_lenient_description (lenient), http_parser_lenient_string (lenient)
		);
	}

}

void http_cerver_set_parser_lenient (
	HttpCerver *http_cerver, const unsigned int lenient
) {

	if (http_cerver) {
		http_cerver->parser_lenient = lenient & HTTP_PARSER_LENIENT_ALL;

		#define XX(num, name, string, description) \
			http_cerver_parser_lenient_warning (http_cerver, HTTP_PARSER_LENIENT_##name);
		LLHTTP_LENIENT_MAP(XX)
		#undef XX
	}

}

#pragma endregion
