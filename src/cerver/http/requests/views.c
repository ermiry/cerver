#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/connection/connection.h"
#include "cerver/handler/handler.h"

#include "cerver/http/core/context.h"
#include "cerver/http/core/custom.h"
#include "cerver/http/core/handler.h"
#include "cerver/http/core/pairs.h"
#include "cerver/http/core/utils.h"
#include "cerver/http/headers/types.h"
#include "cerver/http/parser/head.h"
#include "cerver/http/requests/content.h"
#include "cerver/http/requests/internal.h"
#include "cerver/http/requests/method.h"
#include "cerver/http/requests/request.h"

#include "cerver/json/json.h"

// the getters of a request that a handler uses: each one takes the context of the request
// and returns a view of a value that lives in the memory of the request

#pragma region views

static CerverHttpView http_view_with_string (const String *string) {

	CerverHttpView view = { NULL, 0 };

	if (string && string->str) {
		view.data = string->str;
		view.length = string->len;
	}

	return view;

}

// the characters end with '\0' and their length is known
static CerverHttpView http_view_with_characters (
	const char *characters, const size_t length
) {

	CerverHttpView view = { NULL, 0 };

	if (characters) {
		view.data = characters;
		view.length = length;
	}

	return view;

}

static inline HttpRequest *http_context_request (const CerverHttpContext *context) {

	return context ? context->request : NULL;

}

// the pair of a position, NULL for a position that the pairs do not have
static const KeyValuePair *http_view_pair_at (
	const KeyValuePair *pairs, const size_t n_pairs, const size_t index
) {

	return (pairs && (index < n_pairs)) ? &pairs[index] : NULL;

}

static CerverHttpView http_view_with_pair (
	const KeyValuePair *pair, CerverHttpView *key
) {

	if (key) *key = http_view_with_string (pair ? pair->key : NULL);

	return http_view_with_string (pair ? pair->value : NULL);

}

#pragma endregion

#pragma region line

CerverHttpMethod cerver_http_request_method (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? request->method : (CerverHttpMethod) REQUEST_METHOD_UNDEFINED;

}

bool cerver_http_request_is_head (const CerverHttpContext *context) {

	return context ? context->head : false;

}

CerverHttpView cerver_http_request_path (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_string (request ? request->url : NULL);

}

CerverHttpView cerver_http_request_query (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_string (request ? request->query : NULL);

}

CerverHttpView cerver_http_request_id (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	const bool identified = request && request->id[0];

	return http_view_with_characters (
		identified ? request->id : NULL, identified ? strlen (request->id) : 0
	);

}

CerverHttpView cerver_http_request_client_ip (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_characters (
		request ? request->client_ip : NULL, request ? strlen (request->client_ip) : 0
	);

}

// the connection of a request, NULL for a context that is not of one
static const Connection *http_context_connection (const CerverHttpContext *context) {

	return (context && context->cr) ? context->cr->connection : NULL;

}

CerverHttpView cerver_http_request_peer_ip (const CerverHttpContext *context) {

	const Connection *connection = http_context_connection (context);

	return http_view_with_characters (
		connection ? connection->ip : NULL, connection ? strlen (connection->ip) : 0
	);

}

const CerverHttpHead *cerver_http_request_head (const CerverHttpContext *context) {

	return context ? &context->parser.head : NULL;

}

#pragma endregion

#pragma region params

unsigned int cerver_http_request_params_count (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? request->n_params : 0;

}

CerverHttpView cerver_http_request_param (
	const CerverHttpContext *context, const char *name
) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_string (
		(request && name) ? http_request_get_param (request, name) : NULL
	);

}

CerverHttpView cerver_http_request_param_at (
	const CerverHttpContext *context, const unsigned int index
) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_string (
		request ? http_request_get_param_at_idx (request, index) : NULL
	);

}

const char *cerver_http_request_param_name_at (
	const CerverHttpContext *context, const unsigned int index
) {

	const HttpRequest *request = http_context_request (context);

	return request ? http_request_get_param_name_at_idx (request, index) : NULL;

}

size_t cerver_http_request_query_values_count (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? request->n_query_pairs : 0;

}

CerverHttpView cerver_http_request_query_value (
	const CerverHttpContext *context, const char *key
) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_string (
		(request && key) ? http_request_query_params_get_value (request, key) : NULL
	);

}

CerverHttpView cerver_http_request_query_value_at (
	const CerverHttpContext *context, const size_t index, CerverHttpView *key
) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_pair (
		request ? http_view_pair_at (request->query_pairs, request->n_query_pairs, index) : NULL,
		key
	);

}

#pragma endregion

#pragma region headers

CerverHttpView cerver_http_request_header (
	const CerverHttpContext *context, const CerverHttpHeader header
) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_string (
		request ? http_request_get_header (request, header) : NULL
	);

}

static bool http_view_header_is_known (const CerverHttpHeader header) {

	return ((int) header >= 0) && ((int) header < HTTP_HEADERS_SIZE)
		&& (header != HTTP_HEADER_UNDEFINED);

}

CerverHttpView cerver_http_request_header_with_name (
	const CerverHttpContext *context, const char *name
) {

	CerverHttpView view = { NULL, 0 };

	const HttpRequest *request = http_context_request (context);
	if (request && name) {
		const CerverHttpHeader header = cerver_http_header_with_string (name);
		if (http_view_header_is_known (header)) {
			view = http_view_with_string (http_request_get_header (request, header));
		}

		else {
			const HttpCustomHeader *custom = http_request_custom_header_find (request, name);
			view = http_view_with_string (custom ? custom->value : NULL);
		}
	}

	return view;

}

CerverHttpContentType cerver_http_request_content_type (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? http_request_get_content_type (request)
		: (CerverHttpContentType) HTTP_CONTENT_TYPE_NONE;

}

bool cerver_http_request_is_json (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? http_request_content_type_is_json (request) : false;

}

bool cerver_http_request_bytes_range (
	const CerverHttpContext *context, CerverHttpBytesRange *bytes_range
) {

	const HttpRequest *request = http_context_request (context);

	return (request && bytes_range) ? http_request_get_bytes_range (request, bytes_range) : false;

}

#pragma endregion

#pragma region body

CerverHttpView cerver_http_request_body (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_string (request ? request->body : NULL);

}

const CerverJsonValue *cerver_http_request_json (
	const CerverHttpContext *context, CerverJsonError *error
) {

	const HttpRequest *request = http_context_request (context);

	return request ? http_request_json (request, error) : NULL;

}

size_t cerver_http_request_form_values_count (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? request->n_body_pairs : 0;

}

CerverHttpView cerver_http_request_form_value (
	const CerverHttpContext *context, const char *key
) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_string (
		(request && key) ? http_request_body_get_value (request, key) : NULL
	);

}

CerverHttpView cerver_http_request_form_value_at (
	const CerverHttpContext *context, const size_t index, CerverHttpView *key
) {

	const HttpRequest *request = http_context_request (context);

	return http_view_with_pair (
		request ? http_view_pair_at (request->body_pairs, request->n_body_pairs, index) : NULL,
		key
	);

}

#pragma endregion

#pragma region authentication

const CerverJsonValue *cerver_http_request_claims (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? request->claims : NULL;

}

const void *cerver_http_request_decoded_data (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? request->decoded_data : NULL;

}

void cerver_http_request_set_decoded_data (
	CerverHttpContext *context,
	void *decoded_data, void (*delete_decoded_data)(void *)
) {

	HttpRequest *request = http_context_request (context);
	if (request) {
		request->decoded_data = decoded_data;
		request->delete_decoded_data = delete_decoded_data;
	}

}

const void *cerver_http_request_custom_data (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? request->custom_data : NULL;

}

void cerver_http_request_set_custom_data (
	CerverHttpContext *context,
	void *custom_data, void (*delete_custom_data)(void *)
) {

	HttpRequest *request = http_context_request (context);
	if (request) {
		request->custom_data = custom_data;
		request->delete_custom_data = delete_custom_data;
	}

}

#pragma endregion

#pragma region parts

unsigned int cerver_http_request_files_count (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? request->n_files : 0;

}

unsigned int cerver_http_request_values_count (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? request->n_values : 0;

}

const CerverHttpPart *cerver_http_request_part (
	const CerverHttpContext *context, const char *name
) {

	const HttpRequest *request = http_context_request (context);

	return (request && name) ? http_request_multi_parts_get (request, name) : NULL;

}

// the element of the list of parts that follows the one of a part, the first one without a part
static const ListElement *http_view_part_element_next (
	const DoubleList *parts, const CerverHttpPart *previous
) {

	const ListElement *element = dlist_start (parts);

	if (previous) {
		while (element && (element->data != previous)) element = element->next;

		if (element) element = element->next;
	}

	return element;

}

const CerverHttpPart *cerver_http_request_part_next (
	const CerverHttpContext *context, const CerverHttpPart *previous
) {

	const CerverHttpPart *part = NULL;

	const HttpRequest *request = http_context_request (context);
	if (request && request->multi_parts) {
		const ListElement *element = http_view_part_element_next (request->multi_parts, previous);
		if (element) part = (const CerverHttpPart *) element->data;
	}

	return part;

}

CerverHttpView cerver_http_request_part_value (
	const CerverHttpContext *context, const char *name
) {

	return cerver_http_part_value (cerver_http_request_part (context, name));

}

CerverHttpView cerver_http_request_uploads_directory (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	const bool created = request && (request->dirname_len > 0);

	return http_view_with_characters (
		created ? request->dirname : NULL, created ? (size_t) request->dirname_len : 0
	);

}

void cerver_http_request_keep_files (CerverHttpContext *context) {

	HttpRequest *request = http_context_request (context);
	if (request) http_request_multi_part_keep_files (request);

}

void cerver_http_request_discard_files (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);
	if (request) http_request_multi_part_discard_files (request);

}

bool cerver_http_part_is_file (const CerverHttpPart *part) {

	return part ? http_multi_part_is_file (part) : false;

}

CerverHttpView cerver_http_part_name (const CerverHttpPart *part) {

	return http_view_with_string (part ? part->name : NULL);

}

CerverHttpView cerver_http_part_value (const CerverHttpPart *part) {

	const bool has_value = part && http_multi_part_is_value (part) && (part->value_len >= 0);

	return http_view_with_characters (
		has_value ? part->value : NULL, has_value ? (size_t) part->value_len : 0
	);

}

CerverHttpView cerver_http_part_filename (const CerverHttpPart *part) {

	const bool has_filename = part && http_multi_part_is_file (part) && (part->filename_len > 0);

	return http_view_with_characters (
		has_filename ? part->filename : NULL, has_filename ? (size_t) part->filename_len : 0
	);

}

CerverHttpView cerver_http_part_saved_filename (const CerverHttpPart *part) {

	const bool was_saved = part && http_multi_part_is_file (part) && (part->saved_filename_len > 0);

	return http_view_with_characters (
		was_saved ? part->saved_filename : NULL, was_saved ? (size_t) part->saved_filename_len : 0
	);

}

size_t cerver_http_part_size (const CerverHttpPart *part) {

	return (part && http_multi_part_is_file (part)) ? (size_t) part->total_wrote : 0;

}

unsigned int cerver_http_part_move_file (
	const CerverHttpPart *part, const char *destination
) {

	// the parts of a request are only read by its handler, that is the one that moves their files
	return (part && destination) ?
		http_multi_part_move_file ((MultiPart *) part, destination) : 1;

}

#pragma endregion

#pragma region uploads

const CerverHttpPart *cerver_http_request_current_part (const CerverHttpContext *context) {

	const HttpRequest *request = http_context_request (context);

	return request ? request->current_part : NULL;

}

void cerver_http_part_set_generated_filename (
	const CerverHttpPart *part, const char *format, ...
) {

	if (part && format) {
		va_list args;
		va_start (args, format);

		// the part that is being received is of its request, the generator is the one that names it
		MultiPart *multi_part = (MultiPart *) part;

		if (vsnprintf (
			multi_part->generated_filename,
			HTTP_MULTI_PART_GENERATED_FILENAME_SIZE - 1,
			format, args
		) < 0) {
			multi_part->generated_filename[0] = '\0';
		}

		multi_part->generated_filename_len = (int) strlen (multi_part->generated_filename);

		va_end (args);
	}

}

void cerver_http_request_set_uploads_directory (
	CerverHttpContext *context, const char *format, ...
) {

	HttpRequest *request = http_context_request (context);
	if (request && format) {
		va_list args;
		va_start (args, format);

		// the whole buffer, so a directory of REQUEST_DIRNAME_SIZE - 1 characters fits
		// and a longer one has a length that the cerver rejects
		request->dirname_len = vsnprintf (
			request->dirname, REQUEST_DIRNAME_SIZE,
			format, args
		);

		va_end (args);
	}

}

#pragma endregion
