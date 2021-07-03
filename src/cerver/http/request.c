#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <stdarg.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/config.h"

#include "cerver/http/content.h"
#include "cerver/http/headers.h"
#include "cerver/http/http.h"
#include "cerver/http/request.h"

static const char *request_method_strings[] = {

	#define XX(num, name, string) #string,
	REQUEST_METHOD_MAP(XX)
	#undef XX

};

void http_request_multi_part_discard_files (
	const HttpRequest *http_request
);

const char *http_request_method_str (
	const RequestMethod request_method
) {

	return ELEM_AT (
		request_method_strings, request_method, "Undefined"
	);

}

HttpRequest *http_request_new (void) {

	HttpRequest *http_request = (HttpRequest *) malloc (sizeof (HttpRequest));
	if (http_request) {
		http_request->method = (RequestMethod) REQUEST_METHOD_UNDEFINED;

		http_request->url = NULL;
		http_request->query = NULL;

		http_request->query_params = NULL;

		http_request->n_params = 0;

		for (u8 i = 0; i < REQUEST_PARAMS_SIZE; i++)
			http_request->params[i] = NULL;

		http_request->next_header = HTTP_HEADER_INVALID;

		for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
			http_request->headers[i] = NULL;

		http_request->decoded_data = NULL;
		http_request->delete_decoded_data = NULL;
		
		http_request->body = NULL;

		http_request->current_part = NULL;
		http_request->multi_parts = NULL;
		http_request->n_files = 0;
		http_request->n_values = 0;

		http_request->dirname_len = 0;
		(void) memset (http_request->dirname, 0, REQUEST_DIRNAME_SIZE);
		
		http_request->body_values = NULL;

		http_request->keep_files = false;
	}

	return http_request;
	
}

void http_request_delete (HttpRequest *http_request) {

	if (http_request) {
		str_delete (http_request->url);
		str_delete (http_request->query);

		dlist_delete (http_request->query_params);

		for (u8 i = 0; i < REQUEST_PARAMS_SIZE; i++)
			str_delete (http_request->params[i]);

		for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
			str_delete (http_request->headers[i]);

		if (http_request->decoded_data) {
			if (http_request->delete_decoded_data) 
				http_request->delete_decoded_data (http_request->decoded_data);
		}

		str_delete (http_request->body);

		dlist_delete (http_request->multi_parts);

		dlist_delete (http_request->body_values);

		free (http_request);
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

const String *http_request_get_param_at_idx (
	const HttpRequest *http_request, const unsigned int idx
) {

	return (idx < http_request->n_params) ?
		http_request->params[idx] : NULL;

}

const String *http_request_get_header (
	const HttpRequest *http_request, const http_header header
) {

	return http_request->headers[header];

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

const void *http_request_get_decoded_data (
	const HttpRequest *http_request
) {

	return http_request->decoded_data;

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
		const char *null = "NULL";
		String *header = NULL;
		for (u8 i = 0; i < HTTP_HEADERS_MAX; i++) {
			header = http_request->headers[i];

			cerver_log_msg (
				"%s: %s",
				http_header_string ((const http_header) i),
				header ? header->str : null
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

// searches the request's multi parts values for a value with matching key
// returns a constant c string that should not be deleted if found, NULL if not match
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
