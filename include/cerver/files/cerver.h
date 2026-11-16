/**
 * @file cerver/files/cerver.h
 * @brief The files cerver: the data of a cerver of type CERVER_TYPE_FILES, that sends files to its clients and
 * saves the files that they upload.
 */

#ifndef _CERVER_FILES_CERVER_H_
#define _CERVER_FILES_CERVER_H_

#include "cerver/config.h"

#include "cerver/files/files.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup files_cerver Files cerver
 * @ingroup files
 * @brief The files cerver and its setters.
 *
 * cerver_create () with CERVER_TYPE_FILES creates it as the cerver data; the setters ignore a NULL files cerver.
 * @{
 */

/** @brief The max paths where a files cerver searches the requested files. */
#define FILE_CERVER_MAX_PATHS           32

/**
 * @brief The stats of a files cerver.
 */
typedef struct FileCerverStats {

	/** @brief The requests to get a file. */
	u64 n_files_requests;
	/** @brief The requests that were fulfilled. */
	u64 n_success_files_requests;
	/** @brief The bad requests. */
	u64 n_bad_files_requests;
	/** @brief The files sent. */
	u64 n_files_sent;
	/** @brief The files that failed to be sent. */
	u64 n_bad_files_sent;
	/** @brief The bytes sent. */
	u64 n_bytes_sent;

	/** @brief The requests to upload a file. */
	u64 n_files_upload_requests;
	/** @brief The files that were received. */
	u64 n_success_files_uploaded;
	/** @brief The bad requests to upload a file. */
	u64 n_bad_files_upload_requests;
	/** @brief The files that failed to be received. */
	u64 n_bad_files_received;
	/** @brief The bytes received. */
	u64 n_bytes_received;

} FileCerverStats;

/**
 * @brief The data of a files cerver.
 */
struct _FileCerver {

	/** @brief The cerver that the files cerver belongs to. */
	struct _Cerver *cerver;

	/** @brief The number of #paths. */
	unsigned int n_paths;
	/** @brief The paths where the requested files are searched. */
	String *paths[FILE_CERVER_MAX_PATHS];

	/** @brief The path where the uploaded files are saved. */
	String *uploads_path;

	/**
	 * @brief Receives an uploaded file: it reads the rest of the file from the connection and returns 0 on success
	 * and 1 on error, with the path where it was saved in saved_filename (allocated with malloc ()).
	 *
	 * The default one saves the file in #uploads_path.
	 */
	u8 (*file_upload_handler) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	);

	/** @brief Runs after a file was uploaded, with the path where it was saved. */
	void (*file_upload_cb) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		const char *saved_filename
	);

	/** @brief The stats of the files cerver. */
	FileCerverStats *stats;

};

/** @brief The data of a files cerver, see struct _FileCerver. */
typedef struct _FileCerver FileCerver;

/**
 * @brief Allocates a files cerver with the default upload handler.
 *
 * @return the new files cerver, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE FileCerver *file_cerver_new (void);

/**
 * @brief Releases a files cerver: its paths, its uploads path and its stats.
 *
 * @param[in] file_cerver_ptr the files cerver, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void file_cerver_delete (void *file_cerver_ptr);

/**
 * @brief Creates a files cerver with its stats.
 *
 * @param[in] cerver the cerver that it belongs to
 * @return the new files cerver, NULL if it could not be allocated
 * @note For the internal methods of cerver: cerver_create () calls it.
 */
CERVER_PRIVATE FileCerver *file_cerver_create (struct _Cerver *cerver);

/**
 * @brief Adds a path where the requested files are searched.
 *
 * @param[in,out] file_cerver the files cerver
 * @param[in] path the path, it is copied
 * @retval 0 the path was added
 * @retval 1 a NULL argument, or the files cerver already has #FILE_CERVER_MAX_PATHS paths
 */
CERVER_EXPORT u8 file_cerver_add_path (
	FileCerver *file_cerver, const char *path
);

/**
 * @brief Sets the path where the default upload handler saves the files that the clients send.
 *
 * @param[in,out] file_cerver the files cerver
 * @param[in] uploads_path the path, it is copied; NULL keeps the current one
 * @note Without an uploads path, the default upload handler rejects every upload: the client gets a
 * `CERVER_ERROR_SEND_FILE` and its connection is ended, because the bytes of the file follow its packet.
 */
CERVER_EXPORT void file_cerver_set_uploads_path (
	FileCerver *file_cerver, const char *uploads_path
);

/**
 * @brief Sets the method that receives an uploaded file.
 *
 * @param[in,out] file_cerver the files cerver
 * @param[in] file_upload_handler the method: it gets the FileHeader and the first bytes of the file that came with
 * its packet, it must consume the rest of the file from the socket, set saved_filename to a path allocated with
 * malloc (), and return 0 on success and 1 on error
 */
CERVER_EXPORT void file_cerver_set_file_upload_handler (
	FileCerver *file_cerver,
	u8 (*file_upload_handler) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	)
);

/**
 * @brief Sets a callback that runs after a client uploaded a file.
 *
 * @param[in,out] file_cerver the files cerver
 * @param[in] file_upload_cb the callback, it gets the path where the file was saved, that is released after it
 */
CERVER_EXPORT void file_cerver_set_file_upload_cb (
	FileCerver *file_cerver,
	void (*file_upload_cb) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		const char *saved_filename
	)
);

/**
 * @brief Searches a requested file in the paths of the files cerver, in the order they were added.
 *
 * @param[in] file_cerver the files cerver
 * @param[in] filename the name of the file
 * @return the first "path/filename" that exists, NULL for a NULL argument or when it is not found
 */
CERVER_PUBLIC String *file_cerver_search_file (
	FileCerver *file_cerver, const char *filename
);

/**
 * @brief Opens a file and sends it to a connection: first its FileHeader in a packet, then its contents.
 *
 * If the file can not be opened, the connection gets a CERVER_ERROR_FILE_NOT_FOUND error packet.
 *
 * @param[in] cerver the cerver
 * @param[in] client the client of the connection
 * @param[in] connection the connection
 * @param[in] filename the path of the file, with at least one '/'
 * @return the bytes of the file that were sent, -1 for a NULL filename or connection or on error
 */
CERVER_PUBLIC ssize_t file_cerver_send_file (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	const char *filename
);

/**
 * @brief Prints the stats of a files cerver.
 *
 * @param[in] file_cerver the files cerver, NULL prints nothing
 */
CERVER_EXPORT void file_cerver_stats_print (FileCerver *file_cerver);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
