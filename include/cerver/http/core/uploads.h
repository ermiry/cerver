/**
 * @file cerver/http/core/uploads.h
 * @brief Where and how the files of the multipart requests of a CerverHttp are saved.
 */

#ifndef _CERVER_HTTP_CORE_UPLOADS_H_
#define _CERVER_HTTP_CORE_UPLOADS_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_uploads Uploads
 * @ingroup http_core
 * @brief The directory, the names and the modes of the uploaded files.
 *
 * The setters ignore a NULL CerverHttp.
 * @{
 */

/**
 * @brief Sets the directory where the files of the multipart requests are saved, replacing the previous one.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] uploads_path the path, truncated to CERVER_HTTP_UPLOADS_PATH_SIZE - 1 characters; NULL keeps the
 * current one
 */
CERVER_EXPORT void cerver_http_set_uploads_path (
	CerverHttp *http_cerver, const char *uploads_path
);

/**
 * @brief Works like cerver_http_set_uploads_path () with a path generated from a format.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] format the printf () format of the path, NULL keeps the current one
 * @param[in] ... the arguments of the format
 * @note A path that does not fit is truncated, and its length is the one of the part that was kept; a format that
 * fails leaves the path empty, which disables the uploads.
 */
CERVER_EXPORT void cerver_http_generate_uploads_path (
	CerverHttp *http_cerver, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/**
 * @brief Sets the mode of the uploaded files.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] file_mode the mode, the default is #CERVER_HTTP_DEFAULT_UPLOADS_FILE_MODE
 */
CERVER_EXPORT void cerver_http_set_uploads_file_mode (
	CerverHttp *http_cerver, const unsigned int file_mode
);

/**
 * @brief Generates the name of an uploaded file as "sock_fd-time-sequence-index-filename".
 *
 * The format is "%d-%ld-%zu-%u-%s" with the socket of the connection, time (NULL), the sequence of the request, the
 * index of the file in its request and the filename of the part: the sequence is unique for each request of the
 * cerver, even if its fd is reused in the same second, and the index keeps unique the files of a request with the
 * same filename.
 *
 * @param[in,out] context the context of the request, whose current part gets the name
 */
CERVER_EXPORT void cerver_http_default_uploads_filename_generator (
	CerverHttpContext *context
);

/**
 * @brief Sets the method that generates the name of each file of a multipart request.
 *
 * The method sets the generated filename of the current part, up to HTTP_MULTI_PART_GENERATED_FILENAME_SIZE.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] uploads_filename_generator the method, NULL to save each file with the sanitized filename of its part;
 * a method that does not set a name also falls back to it, with a warning
 * @warning With CERVER_HANDLER_TYPE_EVENTS the method runs in an I/O thread, that reads no other connection until
 * it returns: it must not wait.
 */
CERVER_EXPORT void cerver_http_set_uploads_filename_generator (
	CerverHttp *http_cerver,
	CerverHttpHandler uploads_filename_generator
);

/**
 * @brief Sets cerver_http_default_uploads_filename_generator () as the generator of the names of the uploaded
 * files.
 *
 * @param[in,out] http_cerver the CerverHttp
 */
CERVER_EXPORT void cerver_http_set_default_uploads_filename_generator (
	CerverHttp *http_cerver
);

/**
 * @brief Sets the mode of the directories of the uploads.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] dir_mode the mode, the default is #CERVER_HTTP_DEFAULT_UPLOADS_DIR_MODE
 */
CERVER_EXPORT void cerver_http_set_uploads_dir_mode (
	CerverHttp *http_cerver, const unsigned int dir_mode
);

/**
 * @brief Generates the directory of the uploads of a request as "sock_fd-time-sequence".
 *
 * The format is "%d-%ld-%zu" with the socket of the connection, time (NULL) and the sequence of the request; the
 * files of the request share its sequence.
 *
 * @param[in,out] context the context of the request, that gets the directory
 */
CERVER_EXPORT void cerver_http_default_uploads_dirname_generator (
	CerverHttpContext *context
);

/**
 * @brief Sets the method that generates, for each multipart request, the directory inside the uploads path where
 * its files are saved.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] uploads_dirname_generator the method, NULL to save the files in the uploads path
 * @warning With CERVER_HANDLER_TYPE_EVENTS the method runs in an I/O thread, that reads no other connection until
 * it returns: it must not wait.
 */
CERVER_EXPORT void cerver_http_set_uploads_dirname_generator (
	CerverHttp *http_cerver,
	CerverHttpHandler uploads_dirname_generator
);

/**
 * @brief Sets cerver_http_default_uploads_dirname_generator () as the generator of the directories of the uploads.
 *
 * @param[in,out] http_cerver the CerverHttp
 */
CERVER_EXPORT void cerver_http_set_default_uploads_dirname_generator (
	CerverHttp *http_cerver
);

/**
 * @brief Sets whether the uploaded files are deleted when their request ends, unless they were kept with
 * http_request_multi_part_keep_files ().
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] value true to delete them, the default is #CERVER_HTTP_DEFAULT_UPLOADS_DELETE
 */
CERVER_EXPORT void cerver_http_set_uploads_delete_when_done (
	CerverHttp *http_cerver, bool value
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
