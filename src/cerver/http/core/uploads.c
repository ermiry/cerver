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
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/uploads.h"
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

#pragma region uploads

// sets the default uploads path where any multipart file will be saved
// this method will replace the previous value with the new one
void http_cerver_set_uploads_path (
	HttpCerver *http_cerver, const char *uploads_path
) {

	if (http_cerver && uploads_path) {
		(void) strncpy (
			http_cerver->uploads_path,
			uploads_path,
			HTTP_CERVER_UPLOADS_PATH_SIZE - 1
		);

		http_cerver->uploads_path_len = (int) strlen (
			http_cerver->uploads_path
		);
	}

}

// works like http_cerver_set_uploads_path () but can generate
// a custom path on the fly using variable arguments
void http_cerver_generate_uploads_path (
	HttpCerver *http_cerver, const char *format, ...
) {

	if (http_cerver && format) {
		va_list args;
		va_start (args, format);

		http_cerver->uploads_path_len = vsnprintf (
			http_cerver->uploads_path, HTTP_CERVER_UPLOADS_PATH_SIZE - 1,
			format, args
		);

		va_end (args);
	}

}

// sets the mode_t to be used when creating uploads files
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE
void http_cerver_set_uploads_file_mode (
	HttpCerver *http_cerver, const unsigned int file_mode
) {

	if (http_cerver) {
		http_cerver->uploads_file_mode = file_mode;
	}

}

// gives the request its number in the cerver the first time that a generator asks for it,
// so the directory and the files of a request share it
// a receive without its HTTP cerver has the number 0
static size_t http_receive_upload_sequence (const HttpReceive *http_receive) {

	HttpReceive *receive = (HttpReceive *) http_receive;

	if (!receive->upload_sequence && receive->http_cerver) {
		receive->upload_sequence = __atomic_add_fetch (
			&receive->http_cerver->uploads_sequence, 1, __ATOMIC_RELAXED
		);
	}

	return receive->upload_sequence;

}

// method that can be used to generate multi-part uploads filenames
// with format "%d-%ld-%zu-%u-%s" using "sock_fd-time (NULL)-sequence-file index-multi_part->filename"
// the sequence is unique for each request of the cerver, even if its fd is reused in the same second,
// and the file index keeps unique the files of a request that have the same filename
void http_cerver_default_uploads_filename_generator (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const MultiPart *mpart = http_request_get_current_mpart (request);

	http_multi_part_set_generated_filename (
		mpart,
		"%d-%ld-%zu-%u-%s",
		http_receive->cr->connection->socket->sock_fd,
		time (NULL),
		http_receive_upload_sequence (http_receive),
		(unsigned int) request->n_files,
		http_multi_part_get_filename (mpart)
	);

}

// sets a method that should generate a c string to be used
// to save each incoming file of any multipart request
// the new filename should be placed in generated_filename
// with a max size of HTTP_MULTI_PART_GENERATED_FILENAME_SIZE
void http_cerver_set_uploads_filename_generator (
	HttpCerver *http_cerver,
	void (*uploads_filename_generator)(
		const HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver) {
		http_cerver->uploads_filename_generator = uploads_filename_generator;
	}

}

// sets the HTTP cerver's uploads filename generator
// to be http_cerver_default_uploads_filename_generator ()
void http_cerver_set_default_uploads_filename_generator (
	HttpCerver *http_cerver
) {

	if (http_cerver) {
		http_cerver->uploads_filename_generator = http_cerver_default_uploads_filename_generator;
	}

}

// sets the mode_t value to be used when creating uploads dirs
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE
void http_cerver_set_uploads_dir_mode (
	HttpCerver *http_cerver, const unsigned int dir_mode
) {

	if (http_cerver) {
		http_cerver->uploads_dir_mode = dir_mode;
	}

}

// method that can be used to generate multi-part uploads dirnames
// with format "%d-%ld-%zu" using "sock_fd-time (NULL)-sequence"
// the files of the request share its sequence
void http_cerver_default_uploads_dirname_generator (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_request_set_dirname (
		request,
		"%d-%ld-%zu",
		http_receive->cr->connection->socket->sock_fd,
		time (NULL),
		http_receive_upload_sequence (http_receive)
	);

}

// sets a method to be called on every new multi-part request
// that will be used to generate a new directory
// inside the uploads path to save all the files from the request
void http_cerver_set_uploads_dirname_generator (
	HttpCerver *http_cerver,
	void (*uploads_dirname_generator)(
		const HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver) {
		http_cerver->uploads_dirname_generator = uploads_dirname_generator;
	}

}

// sets the HTTP cerver's uploads dirname generator
// to be http_cerver_default_uploads_dirname_generator ()
void http_cerver_set_default_uploads_dirname_generator (
	HttpCerver *http_cerver
) {

	if (http_cerver) {
		http_cerver->uploads_dirname_generator = http_cerver_default_uploads_dirname_generator;
	}

}

// specifies whether uploads are deleted after the requested has ended
// unless the request files have been explicitly saved using
// http_request_multi_part_keep_files ()
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_DELETE
void http_cerver_set_uploads_delete_when_done (
	HttpCerver *http_cerver, bool value
) {

	if (http_cerver) {
		http_cerver->uploads_delete_when_done = value;
	}

}

#pragma endregion
