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

#include <openssl/crypto.h>
#include <openssl/sha.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/connection/connection.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"

#include "cerver/http/core/custom.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/settings.h"
#include "cerver/http/headers/header.h"
#include "cerver/http/json/json.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"
#include "cerver/http/parser/http_parser.h"
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

#include "cerver/threads/worker.h"

#include "cerver/utils/base64.h"
#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region headers

void http_cerver_set_date_header (
	HttpCerver *http_cerver, const bool date_header
) {

	if (http_cerver) http_cerver->date_header = date_header;

}

void http_cerver_set_request_id (
	HttpCerver *http_cerver, const bool request_id
) {

	if (http_cerver) http_cerver->request_id = request_id;

}

void http_cerver_set_compression_min_size (
	HttpCerver *http_cerver, const size_t min_size
) {

	if (http_cerver) http_cerver->compression_min_size = min_size;

}

void http_cerver_set_compression_level (
	HttpCerver *http_cerver, const int level
) {

	if (http_cerver) {
		if ((level >= HTTP_COMPRESSION_MIN_LEVEL) && (level <= HTTP_COMPRESSION_MAX_LEVEL)) {
			http_cerver->compression_level = level;
		}

		else {
			cerver_log_warning (
				"http_cerver_set_compression_level () - %d is not between %d and %d",
				level, HTTP_COMPRESSION_MIN_LEVEL, HTTP_COMPRESSION_MAX_LEVEL
			);
		}
	}

}

#pragma endregion

#pragma region proxies

void http_cerver_set_trusted_proxies (
	HttpCerver *http_cerver, const HttpTrustedProxies trusted_proxies
) {

	if (http_cerver) http_cerver->trusted_proxies = trusted_proxies;

}

u8 http_cerver_add_trusted_proxy (
	HttpCerver *http_cerver, const char *cidr
) {

	u8 retval = 1;

	if (http_cerver) {
		if (http_cerver->n_trusted_proxies >= HTTP_TRUSTED_PROXIES_SIZE) {
			http_tree_error (
				http_cerver->tree, "Trusted proxy %s exceeds the %d networks",
				cidr ? cidr : "NULL", HTTP_TRUSTED_PROXIES_SIZE
			);
		}

		else if (!http_proxy_network_parse (
			cidr, &http_cerver->trusted_proxies_networks[http_cerver->n_trusted_proxies]
		)) {
			http_tree_error (
				http_cerver->tree, "Trusted proxy %s is not a valid network",
				cidr ? cidr : "NULL"
			);
		}

		else {
			http_cerver->n_trusted_proxies += 1;
			retval = 0;
		}
	}

	return retval;

}

#pragma endregion
#pragma region origins

// adds a new domain to the HTTP cerver's origins whitelist
// returns 0 on success, 1 on error
u8 http_cerver_add_origin_to_whitelist (
	HttpCerver *http_cerver, const char *domain
) {

	u8 retval = 1;

	if (http_cerver && domain) {
		if (http_cerver->n_origins < HTTP_ORIGINS_SIZE) {
			http_origin_init (
				&http_cerver->origins_whitelist[http_cerver->n_origins],
				domain
			);

			http_cerver->n_origins += 1;

			retval = 0;
		}
	}

	return retval;

}

void http_cerver_print_origins_whitelist (
	const HttpCerver *http_cerver
) {

	if (http_cerver) {
		if (http_cerver->n_origins) {
			(void) printf ("Origins whitelist (%u): \n", http_cerver->n_origins);

			const HttpOrigin *origin = NULL;
			for (u8 idx = 0; idx < http_cerver->n_origins; idx++) {
				origin = &http_cerver->origins_whitelist[idx];

				(void) printf (
					"[%u]: (%d) - %s\n",
					idx, origin->len, origin->value
				);
			}
		}

		else {
			(void) printf ("Origins whitelist is empty!\n");
		}
	}

}

#pragma endregion

#pragma region data

const void *http_cerver_get_custom_data (
	const HttpCerver *http_cerver
) {

	return http_cerver ? http_cerver->custom_data : NULL;

}

void http_cerver_set_custom_data (
	HttpCerver *http_cerver, void *custom_data
) {

	if (http_cerver) http_cerver->custom_data = custom_data;

}

void http_cerver_set_delete_custom_data (
	HttpCerver *http_cerver, void (*delete_custom_data)(void *)
) {

	if (http_cerver) http_cerver->delete_custom_data = delete_custom_data;

}

void http_cerver_set_default_delete_custom_data (
	HttpCerver *http_cerver
) {

	if (http_cerver) http_cerver->delete_custom_data = free;

}

#pragma endregion

#pragma region responses

// adds a new global responses header
// this header will be added to all the responses
// if the response has the same header type,
// it will be used instead of the global header
// a header that does not fit in HTTP_HEADER_VALUE_SIZE is not added
// returns 0 on success, 1 on error
u8 http_cerver_add_responses_header (
	HttpCerver *http_cerver,
	const http_header type, const char *actual_header
) {

	u8 retval = 1;

	if (http_cerver && actual_header && (type < HTTP_HEADERS_SIZE)) {
		const bool had = (http_cerver->response_headers[type].len > 0);

		retval = http_response_header_set_with_type (
			&http_cerver->response_headers[type],
			type, actual_header
		);

		if (!retval && !had) http_cerver->n_response_headers += 1;
	}

	return retval;

}

#pragma endregion
