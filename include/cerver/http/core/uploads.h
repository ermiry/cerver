#ifndef _CERVER_HTTP_CORE_UPLOADS_H_
#define _CERVER_HTTP_CORE_UPLOADS_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region uploads

// sets the default uploads path where any multipart file will be saved
// this method will replace the previous value with the new one
CERVER_EXPORT void http_cerver_set_uploads_path (
	HttpCerver *http_cerver, const char *uploads_path
);

// works like http_cerver_set_uploads_path () but can generate
// a custom path on the fly using variable arguments
CERVER_EXPORT void http_cerver_generate_uploads_path (
	HttpCerver *http_cerver, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

// sets the mode_t to be used when creating uploads files
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE
CERVER_EXPORT void http_cerver_set_uploads_file_mode (
	HttpCerver *http_cerver, const unsigned int file_mode
);

// method that can be used to generate multi-part uploads filenames
// with format "%d-%ld-%zu-%u-%s" using "sock_fd-time (NULL)-sequence-file index-multi_part->filename"
// the sequence is unique for each request of the cerver, even if its fd is reused in the same second,
// and the file index keeps unique the files of a request that have the same filename
CERVER_EXPORT void http_cerver_default_uploads_filename_generator (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

// sets a method that should generate a c string to be used
// to save each incoming file of any multipart request
// the new filename should be placed in generated_filename
// with a max size of HTTP_MULTI_PART_GENERATED_FILENAME_SIZE
CERVER_EXPORT void http_cerver_set_uploads_filename_generator (
	HttpCerver *http_cerver,
	void (*uploads_filename_generator)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

// sets the HTTP cerver's uploads filename generator
// to be http_cerver_default_uploads_filename_generator ()
CERVER_EXPORT void http_cerver_set_default_uploads_filename_generator (
	HttpCerver *http_cerver
);

// sets the mode_t value to be used when creating uploads dirs
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE
CERVER_EXPORT void http_cerver_set_uploads_dir_mode (
	HttpCerver *http_cerver, const unsigned int dir_mode
);

// method that can be used to generate multi-part uploads dirnames
// with format "%d-%ld-%zu" using "sock_fd-time (NULL)-sequence"
// the files of the request share its sequence
CERVER_EXPORT void http_cerver_default_uploads_dirname_generator (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

// sets a method to be called on every new multi-part request
// that will be used to generate a new directory
// inside the uploads path to save all the files from the request
CERVER_EXPORT void http_cerver_set_uploads_dirname_generator (
	HttpCerver *http_cerver,
	void (*uploads_dirname_generator)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

// sets the HTTP cerver's uploads dirname generator
// to be http_cerver_default_uploads_dirname_generator ()
CERVER_EXPORT void http_cerver_set_default_uploads_dirname_generator (
	HttpCerver *http_cerver
);

// specifies whether uploads are deleted after the requested has ended
// unless the request files have been explicitly saved using
// http_request_multi_part_keep_files ()
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_DELETE
CERVER_EXPORT void http_cerver_set_uploads_delete_when_done (
	HttpCerver *http_cerver, bool value
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
