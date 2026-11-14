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
#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/pairs.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/settings.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/headers/header.h"
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

static Pool *multi_parts_pool = NULL;

static const char *multi_part_header_value = { "multipart/form-data;" };
static const size_t multi_part_header_value_len = 20;

static const char *multi_part_form_data = { "form-data;" };
static const size_t multi_part_form_data_len = 10;

#pragma region mpart

unsigned int http_multi_parts_init_pool (void) {

	unsigned int retval = 1;

	multi_parts_pool = pool_create (http_multi_part_delete);
	if (multi_parts_pool) {
		pool_set_create (multi_parts_pool, http_multi_part_new);
		pool_set_produce_if_empty (multi_parts_pool, true);
		if (!pool_init (
			multi_parts_pool,
			http_multi_part_new,
			HTTP_CERVER_MULTI_PARTS_POOL_INIT
		)) {
			retval = 0;
		}

		else {
			cerver_log_error ("Failed to init HTTP multi-parts pool!");
		}
	}

	else {
		cerver_log_error ("Failed to create HTTP multi-parts pool!");
	}

	return retval;

}

// released by the last HTTP cerver of the process, after its requests ended
void http_multi_parts_end_pool (void) {

	pool_delete (multi_parts_pool);
	multi_parts_pool = NULL;

}

static inline MultiPart *http_multi_parts_get (void) {

	return (MultiPart *) pool_pop (multi_parts_pool);

}

void http_multi_parts_return (void *multi_part_ptr) {

	if (multi_part_ptr) {
		http_multi_part_reset ((MultiPart *) multi_part_ptr);

		(void) pool_push (multi_parts_pool, multi_part_ptr);
	}

}

bool is_multipart (const char *content) {

	return !strncmp (
		content,
		multi_part_header_value, multi_part_header_value_len
	) ? true : false;

}

static DoubleList *http_mpart_attributes_parse (char *str) {

	DoubleList *attributes = dlist_init (key_value_pair_delete, NULL);

	char *pair = NULL, *name = NULL, *value = NULL;
	char *header_str = strdup (str);
	char *original = header_str;

	while (isspace (*header_str)) header_str++;

	while ((pair = strsep (&header_str, ";")) && pair != NULL) {
		name = strsep (&pair, "=");
		value = strsep (&pair, "=");

		// an attribute without a value ("form-data; name") is ignored
		if (!value) continue;

		(void) dlist_insert_at_end (
			attributes,
			key_value_pair_create (
				c_string_trim (name),
				c_string_trim (c_string_strip_quotes (value))
			)
		);
	}

	free (original);

	return attributes;

}

unsigned int http_mpart_get_boundary (
	char *boundary,
	const char *content_type, const unsigned int content_type_len
) {

	unsigned int retval = 1;

	if (content_type_len > multi_part_header_value_len) {
		char *end = (char *) content_type;
		// end += strlen ("multipart/form-data;");
		end += multi_part_header_value_len;

		DoubleList *attributes = http_mpart_attributes_parse (end);
		if (attributes) {
			// key_value_pairs_print (attributes);

			const String *original_boundary = http_query_pairs_get_value (attributes, "boundary");
			if (original_boundary) {
				(void) snprintf (
					boundary, HTTP_MULTI_PART_BOUNDARY_MAX_SIZE - 1,
					"--%s", original_boundary->str
				);

				retval = 0;
			}

			dlist_delete (attributes);
		}
	}

	return retval;

}

// a file that could not be saved completely (for example, the uploads file system is full)
// is never reported as saved: the part keeps the error and the request is answered with 500
// returns 1 to stop the parser
static int http_receive_mpart_file_failed (
	HttpReceive *http_receive, MultiPart *multi_part,
	const char *operation, const int error
) {

	multi_part->error = error;

	char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
	cerver_log_error (
		"Failed to %s %s to save multipart file - errno %d (%s) - wrote %u",
		operation, multi_part->saved_filename, error,
		strerror_r (error, error_description, sizeof (error_description)),
		multi_part->total_wrote
	);

	// a file that was never opened is not this request's,
	// it may be another one with the same name, so it is never discarded
	if (multi_part->fd == -1) multi_part->saved_filename_len = 0;

	return http_receive_reject (
		http_receive, server_error, HTTP_RECEIVE_STATUS_UPLOAD_ERROR
	);

}

// sets in the part's saved filename the original one with a number,
// before its extension if it has one: "uploads/name.png" -> "uploads/name-1.png"
// returns 0 on success, 1 if the new filename does not fit
static int http_receive_mpart_file_number (
	MultiPart *multi_part, const char *original, const unsigned int number
) {

	const char *basename = strrchr (original, '/');
	const char *extension = strrchr (basename ? basename : original, '.');
	const int name_len = extension ? (int) (extension - original) : (int) strlen (original);

	const int written = snprintf (
		multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_SIZE,
		"%.*s-%u%s", name_len, original, number, extension ? extension : ""
	);

	if ((written < 0) || (written >= HTTP_MULTI_PART_SAVED_FILENAME_SIZE)) return 1;

	multi_part->saved_filename_len = written;

	return 0;

}

// opens the file where the part is saved, a file is never overwritten:
// if the filename already exists, a number is added to it
// returns 0 on success, 1 if the request was rejected
static int http_receive_mpart_file_open (
	HttpReceive *http_receive, MultiPart *multi_part
) {

	char original[HTTP_MULTI_PART_SAVED_FILENAME_SIZE] = { 0 };
	(void) snprintf (original, HTTP_MULTI_PART_SAVED_FILENAME_SIZE, "%s", multi_part->saved_filename);

	const int flags = O_CREAT | O_EXCL | O_WRONLY;
	const unsigned int mode = http_receive->http_cerver->uploads_file_mode;

	multi_part->fd = open (multi_part->saved_filename, flags, mode);

	for (
		unsigned int number = 1;
		(multi_part->fd == -1) && (errno == EEXIST) && (number <= HTTP_MULTI_PART_FILENAME_NUMBERS);
		number++
	) {
		if (http_receive_mpart_file_number (multi_part, original, number)) break;

		multi_part->fd = open (multi_part->saved_filename, flags, mode);
	}

	return (multi_part->fd == -1) ?
		http_receive_mpart_file_failed (http_receive, multi_part, "open", errno) : 0;

}

// writes a piece of a multi-part file, write () can save less bytes than requested
// returns 0 on success, 1 if the request was rejected
static int http_receive_mpart_file_write (
	HttpReceive *http_receive, MultiPart *multi_part,
	const char *at, const size_t length
) {

	multi_part->n_reads += 1;

	const size_t max_file_size = http_receive->http_cerver->max_file_size;
	if (max_file_size && (((size_t) multi_part->total_wrote + length) > max_file_size)) {
		return http_receive_reject (
			http_receive, payload_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		);
	}

	const char *pending = at;
	size_t pending_length = length;
	while (pending_length > 0) {
		const ssize_t wrote = write (multi_part->fd, pending, pending_length);
		if (wrote < 0) {
			if (errno == EINTR) continue;

			return http_receive_mpart_file_failed (
				http_receive, multi_part, "write", errno
			);
		}

		multi_part->total_wrote += (u32) wrote;
		pending += wrote;
		pending_length -= (size_t) wrote;
	}

	return 0;

}

// adds a piece of a multi-part value, a value that does not fit is truncated
static int http_receive_mpart_value_append (
	MultiPart *multi_part, const char *at, const size_t length
) {

	char *end = multi_part->value + multi_part->value_len;

	(void) snprintf (
		end, HTTP_MULTI_PART_VALUE_SIZE - multi_part->value_len,
		"%.*s", (int) length, at
	);

	multi_part->value_len = (int) strlen (multi_part->value);

	return 0;

}

int http_receive_handle_mpart_part_data_begin (
	multipart_parser *parser
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	HttpRequest *request = http_receive->request;

	http_receive->n_parts += 1;

	const unsigned int max_multipart_parts = http_receive->http_cerver->max_multipart_parts;
	if (max_multipart_parts && (http_receive->n_parts > max_multipart_parts)) {
		return http_receive_reject (
			http_receive, payload_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		);
	}

	// create a new multipart structure to handle new data
	request->current_part = http_multi_parts_get ();
	(void) dlist_insert_at_end_unsafe (
		request->multi_parts, request->current_part
	);

	return 0;

}

// Content-Disposition: form-data; name="mirary"; filename="M.jpg"
// Content-Type: image/jpeg
static inline MultiPartHeader http_receive_handle_mpart_header_field_handle (
	const char *header
) {

	if (!strcasecmp ("Content-Disposition", header)) return MULTI_PART_HEADER_CONTENT_DISPOSITION;
	if (!strcasecmp ("Content-Length", header)) return MULTI_PART_HEADER_CONTENT_LENGTH;
	if (!strcasecmp ("Content-Type", header)) return MULTI_PART_HEADER_CONTENT_TYPE;

	return MULTI_PART_HEADER_INVALID;		// unknown header

}

int http_receive_handle_mpart_header_field (
	multipart_parser *parser, const char *at, size_t length
) {

	MultiPart *mpart = (((HttpReceive *) parser->data)->request)->current_part;

	#ifdef HTTP_MPART_DEBUG
	(void) printf ("Header field original: /%.*s/\n", (int) length, at);
	#endif

	// build header in a temp location
	char *end = mpart->temp_header.value + mpart->temp_header.len;

	(void) snprintf (
		end, HTTP_HEADER_VALUE_SIZE - mpart->temp_header.len,
		"%.*s", (int) length, at
	);

	mpart->temp_header.len = (int) strlen (mpart->temp_header.value);

	#ifdef HTTP_MPART_DEBUG
	(void) printf ("Header field build: /%s/\n", mpart->temp_header.value);
	#endif

	mpart->next_header = http_receive_handle_mpart_header_field_handle (
		mpart->temp_header.value
	);

	return 0;

}

int http_receive_handle_mpart_header_value (
	multipart_parser *parser, const char *at, size_t length
) {

	#ifdef HTTP_MPART_DEBUG
	(void) printf ("Header value: %.*s\n", (int) length, at);
	#endif

	MultiPart *multi_part = ((HttpReceive *) parser->data)->request->current_part;
	if (multi_part->next_header != MULTI_PART_HEADER_INVALID) {
		HttpHeader *header = &multi_part->headers[multi_part->next_header];
		char *end = header->value + header->len;

		(void) snprintf (
			end, HTTP_HEADER_VALUE_SIZE - header->len,
			"%.*s", (int) length, at
		);

		header->len = (int) strlen (header->value);
	}

	// reset temp header after the actual value has been handled
	http_header_reset (&multi_part->temp_header);

	return 0;

}

// the data of the part is ignored
static int http_receive_mpart_discard (MultiPart *multi_part) {

	multi_part->discard = true;

	return 0;

}

// a name that is empty after it was sanitized, or that refers to a directory,
// is replaced, so the file is always saved inside the uploads path
static void http_receive_mpart_filename_fallback (MultiPart *multi_part) {

	const char *filename = multi_part->filename;

	if (!filename[0] || !strcmp (filename, ".") || !strcmp (filename, "..")) {
		(void) snprintf (
			multi_part->filename, HTTP_MULTI_PART_FILENAME_SIZE,
			"%s", HTTP_MULTI_PART_DEFAULT_FILENAME
		);
	}

}

// the name of the saved file: the one of the uploads filename generator,
// or the sanitized one of the part if there is no generator or it did not set one
static const char *http_receive_mpart_saved_name (
	HttpReceive *http_receive, MultiPart *multi_part
) {

	const char *name = multi_part->filename;

	if (http_receive->http_cerver->uploads_filename_generator) {
		multi_part->generated_filename_len = 0;
		multi_part->generated_filename[0] = '\0';

		http_receive->http_cerver->uploads_filename_generator (
			http_receive, http_receive->request
		);

		if (multi_part->generated_filename_len > 0) {
			name = multi_part->generated_filename;
		}

		else {
			cerver_log_warning (
				"The uploads filename generator did not set a name, using %s",
				multi_part->filename
			);
		}
	}

	return name;

}

// sets the path where the part is saved, inside the uploads path and the request's directory
// returns 0 on success, 1 if the path does not fit
static int http_receive_mpart_saved_filename (
	HttpReceive *http_receive, MultiPart *multi_part
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;
	const HttpRequest *request = http_receive->request;

	const char *name = http_receive_mpart_saved_name (http_receive, multi_part);

	const int written = (request->dirname_len > 0) ?
		snprintf (
			multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_SIZE,
			"%s/%s/%s", http_cerver->uploads_path, request->dirname, name
		) :
		snprintf (
			multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_SIZE,
			"%s/%s", http_cerver->uploads_path, name
		);

	const bool fits = (written > 0) && (written < HTTP_MULTI_PART_SAVED_FILENAME_SIZE);

	multi_part->saved_filename_len = fits ? written : 0;

	return fits ? 0 : 1;

}

int http_receive_handle_mpart_headers_completed (multipart_parser *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	MultiPart *multi_part = (http_receive)->request->current_part;

	#ifdef HTTP_MPART_DEBUG
	http_multi_part_headers_print (multi_part);
	#endif

	if (multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION].len > 0) {
		if (c_string_starts_with (
			multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION].value,
			multi_part_form_data
		)) {
			char *end = (char *) multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION].value;
			// end += strlen ("form-data;");
			end += multi_part_form_data_len;

			// the part already has an empty list from http_multi_part_new ()
			dlist_delete (multi_part->params);
			multi_part->params = http_mpart_attributes_parse (end);

			multi_part->name = key_value_pairs_get_value (multi_part->params, "name");
			// multi_part->filename = key_value_pairs_get_value (multi_part->params, "filename");
			const String *original_filename = key_value_pairs_get_value (
				multi_part->params, "filename"
			);

			if (original_filename) {
				multi_part->type = MULTI_PART_TYPE_FILE;

				// a file input without a file, there is nothing to save
				if (!original_filename->len) return http_receive_mpart_discard (multi_part);

				// stats
				http_receive->file_stats->n_uploaded_files += 1;

				// sanitize file
				(void) snprintf (
					multi_part->filename, HTTP_MULTI_PART_FILENAME_SIZE,
					"%s", original_filename->str
				);

				files_sanitize_filename (multi_part->filename);
				http_receive_mpart_filename_fallback (multi_part);

				multi_part->filename_len = (int) strlen (multi_part->filename);

				http_receive->request->n_files += 1;

				if (http_receive->http_cerver->uploads_path_len > 0) {
					if (http_receive_mpart_saved_filename (http_receive, multi_part)) {
						return http_receive_mpart_file_failed (
							http_receive, multi_part, "name", ENAMETOOLONG
						);
					}

					if (http_receive_mpart_file_open (http_receive, multi_part)) {
						return 1;
					}

					http_receive->stage = HTTP_STAGE_UPLOAD;
				}

				else {
					cerver_log_error ("Can't save multipart file - no uploads path!");
				}
			}

			else {
				multi_part->type = MULTI_PART_TYPE_VALUE;

				http_receive->request->n_values += 1;
			}
		}

		#ifdef HTTP_MPART_DEBUG
		else {
			cerver_log_error ("c_string_starts_with");
		}
		#endif
	}

	#ifdef HTTP_MPART_DEBUG
	else {
		cerver_log_error ("multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION].len > 0");
	}
	#endif

	return 0;

}

int http_receive_handle_mpart_data (
	multipart_parser *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	MultiPart *multi_part = http_receive->request->current_part;

	if (multi_part->discard) return 0;

	return (multi_part->fd != -1) ?
		http_receive_mpart_file_write (http_receive, multi_part, at, length) :
		http_receive_mpart_value_append (multi_part, at, length);

}

int http_receive_handle_mpart_data_end (multipart_parser *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	MultiPart *multi_part = ((HttpReceive *) parser->data)->request->current_part;

	if (multi_part->fd != -1) {
		(void) close (multi_part->fd);
		multi_part->fd = -1;

		// the file is complete, the rest of the body follows
		http_receive->stage = HTTP_STAGE_BODY;
	}

	if (multi_part->total_wrote < http_receive->file_stats->min_file_size)
		http_receive->file_stats->min_file_size = multi_part->total_wrote;

	if (multi_part->total_wrote > http_receive->file_stats->max_file_size)
		http_receive->file_stats->max_file_size = multi_part->total_wrote;

	http_receive->file_stats->sum_file_size += multi_part->total_wrote;

	return 0;

}

int http_receive_handle_mpart_body (
	llhttp_t *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	if (http_receive_check_body_size (http_receive, length)) return HTTP_RECEIVE_PARSER_STOP;

	const size_t parsed = multipart_parser_execute (
		http_receive->mpart_parser, at, length
	);

	// a callback rejected the request, or the multi-part body is not valid
	return http_receive_parser_result (
		(parsed != length) ?
			http_receive_reject (http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0
	);

}

#pragma endregion
