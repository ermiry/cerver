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

// what is done with each piece of a query: the characters of its key and of its value,
// the value ones are NULL for a piece without a '='
typedef void (*HttpQueryPiece) (
	void *args,
	const char *key_first, const char *key_after,
	const char *value_first, const char *value_after
);

// splits a query by '&' and each piece by its first '='
static void http_query_split (
	const char *first, const char *last, HttpQueryPiece piece, void *args
) {

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

				piece (args, key_first, key_after, value_first, value_after);

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

	piece (args, key_first, key_after, value_first, value_after);

}

// a pair that the list does not take is deleted (it was lost)
static void http_query_pairs_insert (
	void *pairs_ptr,
	const char *key_first, const char *key_after,
	const char *value_first, const char *value_after
) {

	KeyValuePair *kvp = key_value_pair_create_pieces (key_first, key_after, value_first, value_after);
	if (kvp && dlist_insert_at_end ((DoubleList *) pairs_ptr, kvp)) key_value_pair_delete (kvp);

}

DoubleList *http_parse_query_into_pairs (
	const char *first, const char *last
) {

	DoubleList *pairs = NULL;

	if (first && last) {
		pairs = dlist_init (key_value_pair_delete, NULL);

		if (pairs) http_query_split (first, last, http_query_pairs_insert, pairs);
	}

	return pairs;

}

// the pairs of a query in an arena: at most one for each piece between its '&'
typedef struct HttpQueryArena {

	struct Arena *arena;

	KeyValuePair *pairs;
	size_t n_pairs;
	size_t size;

} HttpQueryArena;

// the pieces of a query, one more than its '&'
static size_t http_query_pieces_count (const char *first, const char *last) {

	size_t pieces = 1;
	for (const char *walk = first; walk < last; walk++) {
		if (*walk == '&') pieces += 1;
	}

	return pieces;

}

static String *http_query_arena_piece (struct Arena *arena, const char *first, const char *after) {

	return str_create_from_bytes_arena (arena, first, (size_t) (after - first));

}

// a piece without a '=' is not a pair, like in a list
static void http_query_arena_insert (
	void *query_ptr,
	const char *key_first, const char *key_after,
	const char *value_first, const char *value_after
) {

	HttpQueryArena *query = (HttpQueryArena *) query_ptr;

	if (key_first && key_after && value_first && value_after && (query->n_pairs < query->size)) {
		KeyValuePair *pair = &query->pairs[query->n_pairs];

		pair->key = http_query_arena_piece (query->arena, key_first, key_after);
		pair->value = http_query_arena_piece (query->arena, value_first, value_after);

		if (pair->key && pair->value) query->n_pairs += 1;
	}

}

KeyValuePair *http_query_pairs_arena (
	struct Arena *arena, const char *first, const char *last, size_t *n_pairs
) {

	KeyValuePair *pairs = NULL;

	if (n_pairs) *n_pairs = 0;

	if (arena && first && last && n_pairs) {
		const size_t size = http_query_pieces_count (first, last);

		HttpQueryArena query = {
			arena, (KeyValuePair *) arena_alloc (arena, size * sizeof (KeyValuePair)), 0, size
		};

		if (query.pairs) {
			http_query_split (first, last, http_query_arena_insert, &query);

			pairs = query.pairs;
			*n_pairs = query.n_pairs;
		}
	}

	return pairs;

}

const String *http_query_pairs_find (
	const KeyValuePair *pairs, const size_t n_pairs, const char *key
) {

	const String *value = NULL;

	if (pairs && key) {
		for (size_t idx = 0; (idx < n_pairs) && !value; idx++) {
			if (!strcmp (pairs[idx].key->str, key)) value = pairs[idx].value;
		}
	}

	return value;

}

DoubleList *http_query_pairs_list (const KeyValuePair *pairs, const size_t n_pairs) {

	DoubleList *list = (pairs && n_pairs) ? dlist_init (key_value_pair_delete, NULL) : NULL;
	if (list) {
		// with their lengths: a decoded value can have a '\0'
		for (size_t idx = 0; idx < n_pairs; idx++) {
			const String *key = pairs[idx].key;
			const String *value = pairs[idx].value;

			http_query_pairs_insert (list, key->str, key->str + key->len, value->str, value->str + value->len);
		}
	}

	return list;

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
