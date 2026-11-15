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
#include "cerver/http/core/public.h"
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

#pragma region public

static HttpStaticPath *http_static_path_new (void) {

	HttpStaticPath *http_static_path = (HttpStaticPath *) malloc (sizeof (HttpStaticPath));
	if (http_static_path) {
		http_static_path->path = NULL;
		http_static_path->auth_type = HTTP_ROUTE_AUTH_TYPE_NONE;

		http_static_path->decode_data = NULL;
		http_static_path->delete_decoded_data = NULL;

		http_static_path->authentication_handler = NULL;
	}

	return http_static_path;

}

void http_static_path_delete (void *http_static_path_ptr) {

	if (http_static_path_ptr) {
		HttpStaticPath *http_static_path = (HttpStaticPath *) http_static_path_ptr;

		str_delete (http_static_path->path);

		free (http_static_path);
	}

}

int http_static_path_comparator (const void *a, const void *b) {

	return strcmp (((HttpStaticPath *) a)->path->str, ((HttpStaticPath *) b)->path->str);

}

static HttpStaticPath *http_static_path_create (const char *path) {

	HttpStaticPath *http_static_path = http_static_path_new ();
	if (http_static_path) {
		http_static_path->path = str_new (path);
	}

	return http_static_path;

}

// sets the authentication of the files of a whole static path
void http_static_path_set_auth (
	HttpStaticPath *static_path, HttpRouteAuthType auth_type
) {

	if (static_path) static_path->auth_type = auth_type;

}

void http_static_path_set_decode_data (
	HttpStaticPath *static_path,
	void *(*decode_data)(void *),
	void (*delete_decoded_data)(void *)
) {

	if (static_path) {
		static_path->decode_data = decode_data;
		static_path->delete_decoded_data = delete_decoded_data;
	}

}

void http_static_path_set_authentication_handler (
	HttpStaticPath *static_path,
	HttpAuthenticationHandler authentication_handler
) {

	if (static_path) static_path->authentication_handler = authentication_handler;

}

// add a new static path where static files can be served upon request
// it is recomened to set absoulute paths
// returns the http static path structure that was added to the cerver
HttpStaticPath *http_cerver_static_path_add (
	HttpCerver *http_cerver, const char *static_path
) {

	HttpStaticPath *http_static_path = NULL;

	if (http_cerver && static_path) {
		http_static_path = http_static_path_create (static_path);

		(void) dlist_insert_at_end (http_cerver->static_paths, http_static_path);
	}

	return http_static_path;

}

// removes a path from the served public paths
// returns 0 on success, 1 on error
u8 http_receive_public_path_remove (
	HttpCerver *http_cerver, const char *static_path
) {

	u8 retval = 1;

	if (http_cerver && static_path) {
		// the list's comparator receives static paths
		HttpStaticPath *query = http_static_path_create (static_path);

		void *data = query ? dlist_remove (
			http_cerver->static_paths,
			query,
			NULL
		) : NULL;

		if (data) {
			http_static_path_delete (data);
			retval = 0;
		}

		http_static_path_delete (query);
	}

	return retval;

}

#pragma endregion
