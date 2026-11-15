#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <sys/stat.h>

#include "cerver/http/headers/fields.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/responses/conditional.h"
#include "cerver/http/responses/date.h"

// FNV-1a with 64 bits
static const unsigned long long http_etag_hash_offset = 14695981039346656037ull;
static const unsigned long long http_etag_hash_prime = 1099511628211ull;

#define HTTP_ETAG_DATA_FORMAT				"\"%016llx%s\""
#define HTTP_ETAG_FILE_FORMAT				"W/\"%llx-%llx\""

static const char http_etag_weak_prefix[] = { "W/" };
static const size_t http_etag_weak_prefix_length = 2;

static const char http_etag_quote = '"';
static const char http_etag_separator = ',';
static const char http_etag_any = '*';

static unsigned long long http_etag_hash (const void *data, const size_t length) {

	unsigned long long hash = http_etag_hash_offset;

	const unsigned char *bytes = (const unsigned char *) data;
	for (size_t i = 0; i < length; i++) {
		hash ^= (unsigned long long) bytes[i];
		hash *= http_etag_hash_prime;
	}

	return hash;

}

static size_t http_etag_written (const int written) {

	return ((written > 0) && (written < HTTP_ETAG_SIZE)) ? (size_t) written : 0;

}

size_t http_etag_from_data (
	const void *data, const size_t length,
	const bool compressed, char *etag
) {

	size_t etag_length = 0;

	if ((data || !length) && etag) {
		etag_length = http_etag_written (snprintf (
			etag, HTTP_ETAG_SIZE, HTTP_ETAG_DATA_FORMAT,
			http_etag_hash (data, length),
			compressed ? HTTP_ETAG_COMPRESSED_SUFFIX : ""
		));
	}

	return etag_length;

}

size_t http_etag_from_file (
	const struct stat *file_status, char *etag
) {

	size_t etag_length = 0;

	if (file_status && etag) {
		etag_length = http_etag_written (snprintf (
			etag, HTTP_ETAG_SIZE, HTTP_ETAG_FILE_FORMAT,
			(unsigned long long) file_status->st_size,
			(unsigned long long) file_status->st_mtime
		));
	}

	return etag_length;

}

// skips the weak prefix, the weak comparison only uses the opaque tags
static void http_etag_opaque (
	const char **etag, size_t *etag_length
) {

	if (
		(*etag_length > http_etag_weak_prefix_length)
		&& !strncmp (*etag, http_etag_weak_prefix, http_etag_weak_prefix_length)
	) {
		*etag += http_etag_weak_prefix_length;
		*etag_length -= http_etag_weak_prefix_length;
	}

}

static const char *http_etag_skip_separators (const char *current, const char *end) {

	while (
		(current < end)
		&& (isspace ((unsigned char) *current) || (*current == http_etag_separator))
	) {
		current += 1;
	}

	return current;

}

// returns the end of the entity tag that starts at current (its closing quote + 1),
// or the end of the list item if it is not a quoted entity tag
static const char *http_etag_list_item_end (const char *current, const char *end) {

	const char *item_end = current;

	const char *opening = current;
	if (
		((size_t) (end - opening) > http_etag_weak_prefix_length)
		&& !strncmp (opening, http_etag_weak_prefix, http_etag_weak_prefix_length)
	) {
		opening += http_etag_weak_prefix_length;
	}

	if ((opening < end) && (*opening == http_etag_quote)) {
		const char *closing = (const char *) memchr (opening + 1, http_etag_quote, (size_t) (end - opening - 1));
		item_end = closing ? closing + 1 : end;
	}

	else {
		while ((item_end < end) && (*item_end != http_etag_separator)) item_end += 1;
	}

	return item_end;

}

bool http_etag_none_match (
	const char *if_none_match, const size_t if_none_match_length,
	const char *etag, const size_t etag_length
) {

	bool match = false;

	if (if_none_match && etag && etag_length) {
		const char *opaque = etag;
		size_t opaque_length = etag_length;
		http_etag_opaque (&opaque, &opaque_length);

		const char *end = if_none_match + if_none_match_length;
		const char *current = http_etag_skip_separators (if_none_match, end);
		while (!match && (current < end)) {
			const char *item_end = http_etag_list_item_end (current, end);

			const char *item = current;
			size_t item_length = (size_t) (item_end - current);
			while (item_length && isspace ((unsigned char) item[item_length - 1])) item_length -= 1;
			http_etag_opaque (&item, &item_length);

			match = ((item_length == 1) && (*item == http_etag_any))
				|| ((item_length == opaque_length) && !memcmp (item, opaque, opaque_length));

			current = http_etag_skip_separators (item_end, end);
		}
	}

	return match;

}

static bool http_conditional_not_modified_since (
	const String *if_modified_since, const time_t last_modified
) {

	time_t since = 0;

	return last_modified
		&& http_date_parse (if_modified_since->str, if_modified_since->len, &since)
		&& (last_modified <= since);

}

bool http_conditional_not_modified (
	const HttpRequest *request,
	const char *etag, const size_t etag_length,
	const time_t last_modified
) {

	bool not_modified = false;

	if (
		request
		&& ((request->method == REQUEST_METHOD_GET) || (request->method == REQUEST_METHOD_HEAD))
	) {
		const String *if_none_match = request->headers[HTTP_HEADER_IF_NONE_MATCH];
		const String *if_modified_since = request->headers[HTTP_HEADER_IF_MODIFIED_SINCE];

		// If-None-Match takes precedence over If-Modified-Since (RFC 9110 section 13.2.2)
		if (if_none_match) {
			not_modified = http_etag_none_match (
				if_none_match->str, if_none_match->len, etag, etag_length
			);
		}

		else if (if_modified_since) {
			not_modified = http_conditional_not_modified_since (if_modified_since, last_modified);
		}
	}

	return not_modified;

}
