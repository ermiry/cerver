#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <stdarg.h>
#include <string.h>
#include <strings.h>

#include <ctype.h>

#include <openssl/rand.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/arena.h"

#include "cerver/config.h"

#include "cerver/http/content.h"
#include "cerver/http/custom.h"
#include "cerver/http/headers.h"
#include "cerver/http/http.h"
#include "cerver/http/recycle.h"
#include "cerver/http/request.h"
#include "cerver/http/url.h"
#include "cerver/http/utils.h"

#include "cerver/utils/utils.h"

// static const char *http_request_bytes_range = "bytes=";
static const size_t http_request_bytes_range_len = 6;

void http_request_multi_part_discard_files (
	const HttpRequest *http_request
);

static void http_request_init (HttpRequest *http_request);

HttpRequest *http_request_new (void) {

	HttpRequest *http_request = (HttpRequest *) malloc (sizeof (HttpRequest));
	if (http_request) {
		http_request_init (http_request);
	}

	return http_request;

}

// creates a request in the arena, see HttpRequest arena
HttpRequest *http_request_new_arena (Arena *arena) {

	HttpRequest *http_request = (HttpRequest *) arena_alloc (arena, sizeof (HttpRequest));
	if (http_request) {
		http_request_init (http_request);
		http_request->arena = arena;
	}

	return http_request;

}

static void http_request_init (HttpRequest *http_request) {

	if (http_request) {
		http_request->method = (RequestMethod) REQUEST_METHOD_UNDEFINED;

		http_request->url = NULL;
		http_request->query = NULL;

		http_request->query_params = NULL;

		http_request->n_params = 0;
		http_request->params = NULL;
		http_request->params_names = NULL;

		http_request->next_header = HTTP_HEADER_UNDEFINED;

		for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
			http_request->headers[i] = NULL;

		http_request->n_custom_headers = 0;
		http_request->custom_headers = NULL;
		http_request->current_custom_header = NULL;

		http_request->decoded_data = NULL;
		http_request->delete_decoded_data = NULL;

		http_request->custom_data = NULL;
		http_request->delete_custom_data = NULL;

		http_request->body = NULL;

		http_request->multi_parts = NULL;
		http_request->current_part = NULL;
		http_request->next_part = NULL;

		http_request->n_files = 0;
		http_request->n_values = 0;

		http_request->dirname_len = 0;
		(void) memset (http_request->dirname, 0, REQUEST_DIRNAME_SIZE);

		http_request->body_values = NULL;

		http_request->keep_files = false;

		http_request->id[0] = '\0';
		http_request->client_ip[0] = '\0';

		http_request->arena = NULL;
		http_request->own_arena = NULL;
	}

}

// returns the arena where the request's params and custom headers are created
Arena *http_request_get_arena (HttpRequest *http_request) {

	if (http_request->arena) return http_request->arena;

	if (!http_request->own_arena) {
		http_request->own_arena = arena_create (REQUEST_ARENA_CHUNK_SIZE);
	}

	return http_request->own_arena;

}

// the bytes that the buffer of the body has, including its '\0'
static size_t http_request_body_capacity (const String *body) {

	return (body->max_len > body->len) ? body->max_len : body->len + 1;

}

// the buffer of the body goes back to the ones that the thread reuses,
// its string lives in the arena of the request when it has one
static void http_request_body_release (HttpRequest *http_request) {

	String *body = http_request->body;
	if (body) {
		if (body->str) http_recycle_buffer_give_back (body->str, http_request_body_capacity (body));

		if (!http_request->arena) free (body);

		http_request->body = NULL;
	}

}

// the url, the query and the headers of a request in an arena are released with it
static void http_request_delete_strings (HttpRequest *http_request) {

	if (!http_request->arena) {
		str_delete (http_request->url);
		str_delete (http_request->query);

		for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
			str_delete (http_request->headers[i]);
	}

}

void http_request_delete (HttpRequest *http_request) {

	if (http_request) {
		http_request_delete_strings (http_request);

		dlist_delete (http_request->query_params);

		// the params and the custom headers live in an arena
		http_request->params = NULL;
		http_request->custom_headers = NULL;
		http_request->current_custom_header = NULL;

		if (http_request->decoded_data) {
			if (http_request->delete_decoded_data)
				http_request->delete_decoded_data (http_request->decoded_data);
		}

		if (http_request->custom_data) {
			if (http_request->delete_custom_data)
				http_request->delete_custom_data (http_request->custom_data);
		}

		http_request_body_release (http_request);

		dlist_delete (http_request->multi_parts);

		dlist_delete (http_request->body_values);

		arena_delete (http_request->own_arena);

		if (!http_request->arena) free (http_request);
	}

}

HttpRequest *http_request_create (void) {

	return http_request_new ();

}

// destroys any information related to the request
void http_request_destroy (HttpRequest *http_request) {

	if (http_request) {
		if (!http_request->keep_files) {
			http_request_multi_part_discard_files (http_request);
		}
	}

}

const RequestMethod http_request_get_method (
	const HttpRequest *http_request
) {

	return http_request->method;

}

const char *http_request_get_id (
	const HttpRequest *http_request
) {

	return http_request->id;

}

const char *http_request_get_client_ip (
	const HttpRequest *http_request
) {

	return http_request->client_ip;

}

static const char http_request_id_symbols[] = { "._-" };

bool http_request_id_is_valid (
	const char *value, const size_t length
) {

	bool valid = value && length && (length <= HTTP_REQUEST_ID_LENGTH);

	// strchr () also finds the '\0' of the symbols
	for (size_t i = 0; valid && (i < length); i++) {
		valid = isalnum ((unsigned char) value[i])
			|| (value[i] && strchr (http_request_id_symbols, value[i]));
	}

	return valid;

}

// the bytes of a UUID, the version (4) and the variant (10xx) bits
#define HTTP_REQUEST_ID_UUID_BYTES				16
#define HTTP_REQUEST_ID_UUID_VERSION_BYTE		6
#define HTTP_REQUEST_ID_UUID_VARIANT_BYTE		8
#define HTTP_REQUEST_ID_UUID_VERSION_MASK		0x0fu
#define HTTP_REQUEST_ID_UUID_VERSION			0x40u
#define HTTP_REQUEST_ID_UUID_VARIANT_MASK		0x3fu
#define HTTP_REQUEST_ID_UUID_VARIANT			0x80u

#define HTTP_REQUEST_ID_HEX_SHIFT				4
#define HTTP_REQUEST_ID_HEX_MASK				0x0fu

static const char http_request_id_hex_digits[] = { "0123456789abcdef" };

static const char http_request_id_uuid_separator = '-';

// a separator goes before these bytes, "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
static const unsigned int http_request_id_uuid_groups[] = { 4, 6, 8, 10 };
static const unsigned int http_request_id_uuid_n_groups = 4;

static bool http_request_id_uuid_group_starts (const unsigned int byte) {

	bool starts = false;
	for (unsigned int i = 0; !starts && (i < http_request_id_uuid_n_groups); i++) {
		starts = (http_request_id_uuid_groups[i] == byte);
	}

	return starts;

}

static void http_request_id_uuid_write (
	const unsigned char *bytes, char *id
) {

	char *end = id;
	for (unsigned int i = 0; i < HTTP_REQUEST_ID_UUID_BYTES; i++) {
		if (http_request_id_uuid_group_starts (i)) *end++ = http_request_id_uuid_separator;

		*end++ = http_request_id_hex_digits[(bytes[i] >> HTTP_REQUEST_ID_HEX_SHIFT) & HTTP_REQUEST_ID_HEX_MASK];
		*end++ = http_request_id_hex_digits[bytes[i] & HTTP_REQUEST_ID_HEX_MASK];
	}

	*end = '\0';

}

u8 http_request_id_generate (char *id) {

	u8 retval = 1;

	unsigned char bytes[HTTP_REQUEST_ID_UUID_BYTES] = { 0 };
	if (id && (RAND_bytes (bytes, HTTP_REQUEST_ID_UUID_BYTES) == 1)) {
		bytes[HTTP_REQUEST_ID_UUID_VERSION_BYTE] = (unsigned char) (
			(bytes[HTTP_REQUEST_ID_UUID_VERSION_BYTE] & HTTP_REQUEST_ID_UUID_VERSION_MASK)
			| HTTP_REQUEST_ID_UUID_VERSION
		);

		bytes[HTTP_REQUEST_ID_UUID_VARIANT_BYTE] = (unsigned char) (
			(bytes[HTTP_REQUEST_ID_UUID_VARIANT_BYTE] & HTTP_REQUEST_ID_UUID_VARIANT_MASK)
			| HTTP_REQUEST_ID_UUID_VARIANT
		);

		http_request_id_uuid_write (bytes, id);

		retval = 0;
	}

	return retval;

}

void http_request_resolve_id (HttpRequest *http_request) {

	const String *incoming = http_request->headers[HTTP_HEADER_REQUEST_ID];

	if (incoming && http_request_id_is_valid (incoming->str, incoming->len)) {
		(void) memcpy (http_request->id, incoming->str, incoming->len);
		http_request->id[incoming->len] = '\0';
	}

	else if (http_request_id_generate (http_request->id)) {
		http_request->id[0] = '\0';
	}

}

const String *http_request_get_url (
	const HttpRequest *http_request
) {

	return http_request->url;

}

const String *http_request_get_query (
	const HttpRequest *http_request
) {

	return http_request->query;

}

const DoubleList *http_request_get_query_params (
	const HttpRequest *http_request
) {

	return http_request->query_params;

}

const unsigned int http_request_get_n_params (
	const HttpRequest *http_request
) {

	return http_request->n_params;

}

// the decoded value of a param is created once, in the request's arena
static const String *http_request_param_decode (
	const HttpRequest *http_request, HttpRequestParam *param
) {

	if (!param->decoded) {
		Arena *arena = http_request_get_arena ((HttpRequest *) http_request);
		String *decoded = arena ? str_create_from_bytes_arena (arena, param->value, param->len) : NULL;
		if (decoded) {
			decoded->len = http_url_decode_path (decoded->str, param->value, param->len);
			decoded->str[decoded->len] = '\0';
			param->decoded = decoded;
		}
	}

	return param->decoded;

}

const String *http_request_get_param_at_idx (
	const HttpRequest *http_request, const unsigned int idx
) {

	return (idx < http_request->n_params) ?
		http_request_param_decode (http_request, &http_request->params[idx]) : NULL;

}

const String *http_request_get_param (
	const HttpRequest *http_request, const char *name
) {

	const String *param = NULL;

	if (http_request->params_names && name) {
		for (unsigned int idx = 0; idx < http_request->n_params; idx++) {
			if (http_request->params_names[idx] && !strcmp (http_request->params_names[idx], name)) {
				param = http_request_param_decode (http_request, &http_request->params[idx]);
				break;
			}
		}
	}

	return param;

}

const char *http_request_get_param_name_at_idx (
	const HttpRequest *http_request, const unsigned int idx
) {

	return (http_request->params_names && (idx < http_request->n_params)) ?
		http_request->params_names[idx] : NULL;

}

const String *http_request_get_header (
	const HttpRequest *http_request, const http_header header
) {

	// a negative value of a signed enum is a large unsigned one
	return ((unsigned int) header < (unsigned int) HTTP_HEADERS_SIZE) ?
		http_request->headers[header] : NULL;

}

void http_request_set_current_custom_header (
	HttpRequest *http_request, const char *header
) {

	Arena *arena = http_request_get_arena (http_request);
	HttpCustomHeader *custom_header = arena ?
		http_custom_header_create (arena, header, strlen (header)) : NULL;

	if (custom_header) {
		if (http_request->current_custom_header) {
			http_request->current_custom_header->next = custom_header;
		}

		else {
			http_request->custom_headers = custom_header;
		}

		http_request->current_custom_header = custom_header;
		http_request->n_custom_headers += 1;
	}

}

void http_request_set_current_custom_header_value (
	HttpRequest *http_request, const char *header_value
) {

	Arena *arena = http_request_get_arena (http_request);
	if (arena && http_request->current_custom_header) {
		(void) http_custom_header_set_value (
			arena, http_request->current_custom_header,
			header_value, strlen (header_value)
		);
	}

}

const size_t http_request_get_custom_headers_count (
	const HttpRequest *http_request
) {

	return http_request->n_custom_headers;

}

// returns the custom header with the name, compared without case
HttpCustomHeader *http_request_custom_header_find (
	const HttpRequest *http_request, const char *header
) {

	HttpCustomHeader *found = NULL;

	for (
		HttpCustomHeader *custom_header = http_request->custom_headers;
		custom_header; custom_header = custom_header->next
	) {
		if (!strcasecmp (custom_header->header->str, header)) {
			found = custom_header;
			break;
		}
	}

	return found;

}

const char *http_request_get_custom_header (
	const HttpRequest *http_request, const char *header
) {

	const char *value = NULL;

	if (header) {
		const HttpCustomHeader *custom_header = http_request_custom_header_find (http_request, header);
		if (custom_header) {
			value = custom_header->value->str;
		}

		// a header that became part of the HTTP headers map is still found by its name
		else {
			const String *known = http_request_get_header (
				http_request, http_header_type_by_string (header)
			);

			if (known) value = known->str;
		}
	}

	return value;

}

ContentType http_request_get_content_type (
	const HttpRequest *http_request
) {

	return http_request->headers[HTTP_HEADER_CONTENT_TYPE] ?
		http_content_type_by_mime (
			http_request->headers[HTTP_HEADER_CONTENT_TYPE]->str
		) : HTTP_CONTENT_TYPE_NONE;

}

const String *http_request_get_content_type_string (
	const HttpRequest *http_request
) {

	return http_request->headers[HTTP_HEADER_CONTENT_TYPE];

}

bool http_request_content_type_is_json (
	const HttpRequest *http_request
) {

	return http_request->headers[HTTP_HEADER_CONTENT_TYPE] ?
		http_content_type_is_json (
			http_request->headers[HTTP_HEADER_CONTENT_TYPE]->str
		) : false;

}

// bytes=0-
// bytes=4194304-8388608
static void http_request_get_bytes_range_internal (
	const char *range, BytesRange *bytes_range
) {

	static const char dlm[2] = { '-', '\0' };

	char range_string[HTTP_BYTES_RANGE_VALUE_SIZE] = { 0 };
	(void) strncpy (
		range_string, range + http_request_bytes_range_len,
		HTTP_BYTES_RANGE_VALUE_SIZE - 1
	);

	char *rest = range_string;

	// we want the first value
	char *token = __strtok_r (rest, dlm, &rest);
	bytes_range->start = atol (token);

	if (bytes_range->type == HTTP_RANGE_TYPE_FULL) {
		// and the second value
		token = __strtok_r (rest, dlm, &rest);
		bytes_range->end = atol (token);
	}

}

bool http_request_get_bytes_range (
	const HttpRequest *http_request, BytesRange *bytes_range
) {

	bool result = false;

	const String *range = http_request->headers[HTTP_HEADER_RANGE];
	if (range) {
		// (void) printf ("Range: %s\n", range->str);

		if (c_string_count_tokens (range->str, '-') == 1) {
			http_request_get_bytes_range_internal (
				range->str, bytes_range
			);

			bytes_range->type = bytes_range->start ?
				HTTP_RANGE_TYPE_FIRST : HTTP_RANGE_TYPE_EMPTY;
		}

		else {
			bytes_range->type = HTTP_RANGE_TYPE_FULL;

			http_request_get_bytes_range_internal (
				range->str, bytes_range
			);
		}

		result = true;
	}

	return result;

}

const void *http_request_get_decoded_data (
	const HttpRequest *http_request
) {

	return http_request->decoded_data;

}

void http_request_set_decoded_data (
	HttpRequest *http_request, void *decoded_data
) {

	http_request->decoded_data = decoded_data;

}

void http_request_set_delete_decoded_data (
	HttpRequest *http_request, void (*delete_decoded_data)(void *)
) {

	http_request->delete_decoded_data = delete_decoded_data;

}

void http_request_set_default_delete_decoded_data (
	HttpRequest *http_request
) {

	http_request->delete_decoded_data = free;

}

const void *http_request_get_custom_data (
	const HttpRequest *http_request
) {

	return http_request->custom_data;

}

void http_request_set_custom_data (
	HttpRequest *http_request, void *custom_data
) {

	http_request->custom_data = custom_data;

}

void http_request_set_delete_custom_data (
	HttpRequest *http_request, void (*delete_custom_data)(void *)
) {

	http_request->delete_custom_data = delete_custom_data;

}

void http_request_set_default_delete_custom_data (
	HttpRequest *http_request
) {

	http_request->delete_custom_data = free;

}

const String *http_request_get_body (
	const HttpRequest *http_request
) {

	return http_request->body;

}

const MultiPart *http_request_get_current_mpart (
	const HttpRequest *http_request
) {

	return http_request->current_part;

}

const u8 http_request_get_n_files (
	const HttpRequest *http_request
) {

	return http_request->n_files;

}

const u8 http_request_get_n_values (
	const HttpRequest *http_request
) {

	return http_request->n_values;

}

const int http_request_get_dirname_len (
	const HttpRequest *http_request
) {

	return http_request->dirname_len;

}

const char *http_request_get_dirname (
	const HttpRequest *http_request
) {

	return http_request->dirname;

}

void http_request_set_dirname (
	const HttpRequest *http_request, const char *format, ...
) {

	if (http_request && format) {
		va_list args;
		va_start (args, format);

		HttpRequest *request = (HttpRequest *) http_request;

		request->dirname_len = vsnprintf (
			request->dirname, REQUEST_DIRNAME_SIZE - 1,
			format, args
		);

		va_end (args);
	}

}

const DoubleList *http_request_get_body_values (
	const HttpRequest *http_request
) {

	return http_request->body_values;

}

void http_request_headers_print (const HttpRequest *http_request) {

	if (http_request) {
		for (u8 i = 0; i < HTTP_HEADERS_MAX; i++) {
			if (http_request->headers[i]) {
				cerver_log_msg (
					"%s: %s",
					http_header_string ((const http_header) i),
					http_request->headers[i]->str
				);
			}
		}
	}

}

void http_request_headers_print_full (const HttpRequest *http_request) {

	if (http_request) {
		static const char *null = "NULL";
		for (u8 i = 0; i < HTTP_HEADERS_MAX; i++) {
			cerver_log_msg (
				"%s: %s",
				http_header_string ((const http_header) i),
				http_request->headers[i] ? http_request->headers[i]->str : null
			);
		}
	}

}

void http_request_query_params_print (
	const HttpRequest *http_request
) {

	if (http_request) key_value_pairs_print (http_request->query_params);

}

// returns the matching query param value for the specified key
// returns NULL if no match or no query params
const String *http_request_query_params_get_value (
	const HttpRequest *http_request, const char *key
) {

	return http_request ?
		key_value_pairs_get_value (http_request->query_params, key) : NULL;

}

static MultiPart *http_request_multi_parts_get_internal (
	const HttpRequest *http_request, const char *key
) {

	if (http_request->multi_parts) {
		MultiPart *mpart = NULL;
		for (
			ListElement *le = dlist_start (http_request->multi_parts);
			le; le = le->next
		) {
			mpart = (MultiPart *) le->data;

			if (mpart->name) {
				if (!strcmp (mpart->name->str, key)) {
					return mpart;
				}
			}
		}
	}

	return NULL;

}

// searches the request's multi parts values for one with matching key & type
// returns a constant Stirng that should not be deleted if found, NULL if not match
const MultiPart *http_request_multi_parts_get (
	const HttpRequest *http_request, const char *key
) {

	return (http_request && key) ?
		http_request_multi_parts_get_internal (http_request, key) : NULL;

}

// searches the request's multi-parts for one with matching key
// returns a reference to the multi-part instance that should not be deleted if found
// returns NULL if not match
const char *http_request_multi_parts_get_value (
	const HttpRequest *http_request, const char *key
) {

	if (http_request && key) {
		MultiPart *mpart = http_request_multi_parts_get_internal (http_request, key);
		if (mpart) return mpart->value;
	}

	return NULL;

}

// searches the request's multi parts values for a filename with matching key
// returns a constant c string that should not be deleted if found, NULL if not match
const char *http_request_multi_parts_get_filename (
	const HttpRequest *http_request, const char *key
) {

	if (http_request && key) {
		MultiPart *mpart = http_request_multi_parts_get_internal (http_request, key);
		if (mpart) return mpart->filename;
	}

	return NULL;

}

// searches the request's multi parts values for a saved filename with matching key
// returns a constant c string that should not be deleted if found, NULL if not match
const char *http_request_multi_parts_get_saved_filename (
	const HttpRequest *http_request, const char *key
) {

	if (http_request && key) {
		MultiPart *mpart = http_request_multi_parts_get_internal (http_request, key);
		if (mpart) return mpart->saved_filename;
	}

	return NULL;

}

// starts the HTTP request's multi-parts internal iterator
// returns true on success, false on error
bool http_request_multi_parts_iter_start (
	const HttpRequest *http_request
) {

	bool retval = false;

	if (http_request) {
		if (http_request->multi_parts) {
			if (dlist_start (http_request->multi_parts)) {
				((HttpRequest *) http_request)->next_part = dlist_start (
					http_request->multi_parts
				);

				retval = true;
			}
		}
	}

	return retval;

}

// gets the next request's multi-part using the iterator
// returns NULL if at the end of the list or error
const MultiPart *http_request_multi_parts_iter_get_next (
	const HttpRequest *http_request
) {

	const MultiPart *mpart = NULL;

	if (http_request->next_part) {
		mpart = (const MultiPart *) http_request->next_part->data;
		((HttpRequest *) http_request)->next_part = http_request->next_part->next;
	}

	return mpart;

}

// returns a dlist with constant c strings values (that should not be deleted)
// with all the filenames from the request
// the dlist must be deleted using http_request_multi_parts_all_filenames_delete ()
DoubleList *http_request_multi_parts_get_all_filenames (
	const HttpRequest *http_request
) {

	DoubleList *all = NULL;

	if (http_request) {
		if (http_request->multi_parts) {
			all = dlist_init (NULL, NULL);

			MultiPart *mpart = NULL;
			for (
				ListElement *le = dlist_start (http_request->multi_parts);
				le; le = le->next
			) {
				mpart = (MultiPart *) le->data;

				if (mpart->filename_len > 0) {
					(void) dlist_insert_at_end_unsafe (
						all, mpart->filename
					);
				}
			}
		}
	}

	return all;

}

// returns a dlist with constant c strings values (that should not be deleted)
// with all the saved filenames from the request
// the dlist must be deleted using http_request_multi_parts_all_filenames_delete ()
DoubleList *http_request_multi_parts_get_all_saved_filenames (
	const HttpRequest *http_request
) {

	DoubleList *all = NULL;

	if (http_request) {
		if (http_request->multi_parts) {
			all = dlist_init (NULL, NULL);

			MultiPart *mpart = NULL;
			for (
				ListElement *le = dlist_start (http_request->multi_parts);
				le; le = le->next
			) {
				mpart = (MultiPart *) le->data;

				if (mpart->saved_filename_len > 0) {
					(void) dlist_insert_at_end_unsafe (
						all, mpart->saved_filename
					);
				}
			}
		}
	}

	return all;

}

// used to safely delete a dlist generated by
// http_request_multi_parts_get_all_filenames ()
// or http_request_multi_parts_get_all_saved_filenames ()
void http_request_multi_parts_all_filenames_delete (
	DoubleList *all_filenames
) {

	if (all_filenames) {
		dlist_clear (all_filenames);
		dlist_delete (all_filenames);
	}

}

// signals that the files referenced by the request should be kept
// so they won't be deleted after the request has ended
// files only get deleted if the http cerver's uploads_delete_when_done
// is set to TRUE
void http_request_multi_part_keep_files (
	HttpRequest *http_request
) {

	if (http_request) {
		http_request->keep_files = true;
	}

}

// discards all the saved files from the multipart request
void http_request_multi_part_discard_files (
	const HttpRequest *http_request
) {

	if (http_request) {
		if (http_request->multi_parts) {
			MultiPart *mpart = NULL;
			for (
				ListElement *le = dlist_start (http_request->multi_parts);
				le; le = le->next
			) {
				mpart = (MultiPart *) le->data;

				if (mpart->saved_filename_len > 0) {
					(void) remove (mpart->saved_filename);
				}
			}
		}
	}

}

void http_request_multi_parts_print (
	const HttpRequest *http_request
) {

	if (http_request) {
		(void) printf ("HTTP request multi part values: \n");
		(void) printf ("n files: %d\n", http_request->n_files);
		(void) printf ("n values: %d\n", http_request->n_values);

		if (http_request->multi_parts) {
			for (ListElement *le = dlist_start (http_request->multi_parts); le; le = le->next) {
				http_multi_part_print ((MultiPart *) le->data);
			}

			(void) printf ("\n");
		}
	}

}

void http_request_multi_parts_files_print (
	const HttpRequest *http_request
) {

	if (http_request) {
		if (http_request->multi_parts && http_request->n_files) {
			(void) printf (
				"HTTP request multi part files (%u): \n",
				http_request->n_files
			);

			const MultiPart *mpart = NULL;
			for (ListElement *le = dlist_start (http_request->multi_parts); le; le = le->next) {
				mpart = (const MultiPart *) le->data;

				if (http_multi_part_is_file (mpart)) {
					if (mpart->moved_file) {
						(void) printf (
							"MOVED %s - %s -> %s\n",
							mpart->name->str, mpart->filename, mpart->saved_filename
						);
					}

					else {
						(void) printf (
							"%s - %s -> %s\n",
							mpart->name->str, mpart->filename, mpart->saved_filename
						);
					}
				}
			}
		}

		else {
			(void) printf (
				"HTTP request does NOT have multi part files\n"
			);
		}
	}

}

// search request's body values for matching value by key
// returns a constant String that should not be deleted if match
// returns NULL if not found
const String *http_request_body_get_value (
	const HttpRequest *http_request, const char *key
) {

	return key_value_pairs_get_value (http_request->body_values, key);

}
