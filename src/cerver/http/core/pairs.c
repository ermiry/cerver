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
#include "cerver/http/core/pairs.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/headers/fields.h"
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

#pragma region kvp

KeyValuePair *key_value_pair_new (void) {

	KeyValuePair *kvp = (KeyValuePair *) malloc (sizeof (KeyValuePair));
	if (kvp) kvp->key = kvp->value = NULL;
	return kvp;

}

void key_value_pair_delete (void *kvp_ptr) {

	if (kvp_ptr) {
		KeyValuePair *kvp = (KeyValuePair *) kvp_ptr;

		if (kvp->key) str_delete (kvp->key);
		if (kvp->value) str_delete (kvp->value);

		free (kvp);
	}

}

KeyValuePair *key_value_pair_create (
	const char *key, const char *value
) {

	KeyValuePair *kvp = key_value_pair_new ();
	if (kvp) {
		kvp->key = key ? str_new (key) : NULL;
		kvp->value = value ? str_new (value) : NULL;
	}

	return kvp;

}

// a String with the characters between the two pointers, NULL without memory
static String *key_value_pair_piece (const char *first, const char *after) {

	String *piece = str_new (NULL);
	if (piece) {
		str_n_replace_with (piece, first, (size_t) (after - first));

		if (!piece->str) {
			str_delete (piece);
			piece = NULL;
		}
	}

	return piece;

}

// a pair without memory for its key or its value is not returned (it wrote in NULL)
static KeyValuePair *key_value_pair_create_pieces (
	const char *key_first, const char *key_after,
	const char *value_first, const char *value_after
) {

	KeyValuePair *kvp = NULL;

	if (key_first && key_after && value_first && value_after) {
		kvp = key_value_pair_new ();
		if (kvp) {
			kvp->key = key_value_pair_piece (key_first, key_after);
			kvp->value = key_value_pair_piece (value_first, value_after);

			if (!kvp->key || !kvp->value) {
				key_value_pair_delete (kvp);
				kvp = NULL;
			}
		}
	}

	return kvp;

}

const String *key_value_pairs_get_value (
	const DoubleList *pairs, const char *key
) {

	const String *value = NULL;

	if (pairs && key) {
		KeyValuePair *kvp = NULL;
		for (ListElement *le = dlist_start (pairs); le; le = le->next) {
			kvp = (KeyValuePair *) le->data;
			if (kvp->key && !strcmp (kvp->key->str, key)) {
				value = kvp->value;
				break;
			}
		}
	}

	return value;

}

// what is printed for the key or the value that a pair does not have
static const char *key_value_pair_missing = { "" };

static inline const char *key_value_pair_string (const String *string) {

	return string ? string->str : key_value_pair_missing;

}

void key_value_pairs_print (const DoubleList *pairs) {

	if (pairs) {
		unsigned int idx = 1;
		KeyValuePair *kv = NULL;
		for (ListElement *le = dlist_start (pairs); le; le = le->next) {
			kv = (KeyValuePair *) le->data;

			(void) printf (
				"[%u] - %s = %s\n",
				idx, key_value_pair_string (kv->key), key_value_pair_string (kv->value)
			);

			idx++;
		}
	}

}

// a pair that the list does not take is deleted (it was lost)
static void http_query_pairs_insert (
	DoubleList *pairs,
	const char *key_first, const char *key_after,
	const char *value_first, const char *value_after
) {

	KeyValuePair *kvp = key_value_pair_create_pieces (key_first, key_after, value_first, value_after);
	if (kvp && dlist_insert_at_end (pairs, kvp)) key_value_pair_delete (kvp);

}

DoubleList *http_parse_query_into_pairs (
	const char *first, const char *last
) {

	DoubleList *pairs = NULL;

	if (first && last) {
		pairs = dlist_init (key_value_pair_delete, NULL);

		const char *walk = first;
		const char *key_first = first;
		const char *key_after = NULL;
		const char *value_first = NULL;
		const char *value_after = NULL;

		for (; walk < last; walk++) {
			switch (*walk) {
				case '&': {
					if (value_first != NULL) value_after = walk;
					else key_after = walk;

					http_query_pairs_insert (pairs, key_first, key_after, value_first, value_after);

					if (walk + 1 < last) key_first = walk + 1;
					else key_first = NULL;

					key_after = NULL;
					value_first = NULL;
					value_after = NULL;
				} break;

				case '=': {
					if (key_after == NULL) {
						key_after = walk;
						if (walk + 1 <= last) {
							value_first = walk + 1;
							value_after = walk + 1;
						}
					}
				} break;

				default: break;
			}
		}

		if (value_first != NULL) value_after = walk;
		else key_after = walk;

		http_query_pairs_insert (pairs, key_first, key_after, value_first, value_after);
	}

	return pairs;

}

const String *http_query_pairs_get_value (
	const DoubleList *pairs, const char *key
) {

	return key_value_pairs_get_value (pairs, key);

}

void http_query_pairs_print (const DoubleList *pairs) {

	key_value_pairs_print (pairs);

}

#pragma endregion
