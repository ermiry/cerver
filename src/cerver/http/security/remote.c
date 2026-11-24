#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#ifdef CERVER_CURL
#include <curl/curl.h>
#endif

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/internal.h"

#include "cerver/http/core/connection.h"
#include "cerver/http/core/handler.h"
#include "cerver/http/requests/method.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/security/internal.h"
#include "cerver/http/security/remote.h"

// the body of an answer without one, and of a POST without a body
static const char http_remote_no_body[] = "";

#pragma region request

void http_remote_delete (HttpRemote *remote) {

	if (remote) {
		free (remote->url);

		#ifdef CERVER_CURL
		curl_slist_free_all ((struct curl_slist *) remote->headers);
		#endif

		free (remote->body);
		free (remote->response);

		free (remote);
	}

}

void http_remote_allow (HttpReceive *http_receive, const bool allowed) {

	HttpConnection *connection = http_receive->connection;
	if (connection) connection->remote_allowed = allowed;

}

bool http_remote_is_waiting (const HttpReceive *http_receive) {

	const HttpConnection *connection = http_receive->connection;

	return connection && connection->remote;

}

void http_remote_discard (HttpReceive *http_receive) {

	HttpConnection *connection = http_receive->connection;
	if (connection && connection->remote) {
		http_remote_delete (connection->remote);
		connection->remote = NULL;
	}

}

// on_response () with the answer that the request has
// returns 0 when the request is authenticated, 1 when it is not
static unsigned int http_remote_respond (const HttpReceive *http_receive, const HttpRemote *remote) {

	const HttpAuthResponse response = {
		remote->status,
		remote->response ? remote->response : http_remote_no_body,
		remote->response_size
	};

	return remote->on_response (http_receive, http_receive->request, &response, remote->data) ? 1 : 0;

}

unsigned int http_remote_answer (HttpReceive *http_receive) {

	HttpConnection *connection = http_receive->connection;

	HttpRemote *remote = connection->remote;
	connection->remote = NULL;

	const unsigned int verdict = http_remote_respond (http_receive, remote);

	http_remote_delete (remote);

	return verdict;

}

#pragma endregion

#ifdef CERVER_CURL

// the first room of the body of an answer, it doubles up to HTTP_AUTH_RESPONSE_MAX_BODY_SIZE
#define HTTP_REMOTE_RESPONSE_INITIAL_SIZE		(1 << 10)
#define HTTP_REMOTE_RESPONSE_GROWTH				2

// the connections that the multi of a loop keeps, and the ones that it opens to a destination at once
#define HTTP_REMOTE_LOOP_MAX_CONNECTIONS		(1 << 5)
#define HTTP_REMOTE_LOOP_MAX_HOST_CONNECTIONS	(1 << 4)

// libcurl starts once in the process, before its first request
static pthread_once_t http_remote_once = PTHREAD_ONCE_INIT;

// the handle of the requests that a thread makes by itself: it keeps the connection to the service between them,
// and the key releases it when its thread ends
static pthread_key_t http_remote_easy_key;
static bool http_remote_easy_key_created = false;

static __thread CURL *http_remote_thread_easy = NULL;

// called when a thread that made remote requests ends
static void http_remote_thread_easy_delete (void *easy_ptr) {

	http_remote_thread_easy = NULL;

	curl_easy_cleanup ((CURL *) easy_ptr);

}

static void http_remote_global_start (void) {

	(void) curl_global_init (CURL_GLOBAL_DEFAULT);

	http_remote_easy_key_created = !pthread_key_create (&http_remote_easy_key, http_remote_thread_easy_delete);

}

#pragma region copy

// the request keeps its own copy of what the handler gave, that can live in its stack
// returns true on success, false without memory
static bool http_remote_copy (HttpRemote *remote, const HttpAuthRequest *request) {

	remote->url = strdup (request->url);
	bool copied = (remote->url != NULL);

	if (copied && request->body && request->body_size) {
		remote->body = (char *) malloc (request->body_size);
		copied = (remote->body != NULL);
		if (copied) {
			(void) memcpy (remote->body, request->body, request->body_size);
			remote->body_size = request->body_size;
		}
	}

	const size_t n_headers = (request->n_headers < HTTP_AUTH_REQUEST_MAX_HEADERS) ?
		request->n_headers : HTTP_AUTH_REQUEST_MAX_HEADERS;

	for (size_t index = 0; copied && request->headers && (index < n_headers); index++) {
		struct curl_slist *headers = curl_slist_append ((struct curl_slist *) remote->headers, request->headers[index]);
		copied = (headers != NULL);
		if (copied) remote->headers = headers;
	}

	return copied;

}

static HttpRemote *http_remote_create (
	const HttpAuthRequest *request, HttpAuthResponseHandler on_response, void *data
) {

	HttpRemote *remote = (HttpRemote *) calloc (1, sizeof (HttpRemote));
	if (remote) {
		remote->method = request->method;
		remote->timeout_milliseconds = request->timeout_milliseconds ?
			request->timeout_milliseconds : HTTP_AUTH_REQUEST_DEFAULT_TIMEOUT_MILLISECONDS;

		remote->on_response = on_response;
		remote->data = data;

		if (!http_remote_copy (remote, request)) {
			http_remote_delete (remote);
			remote = NULL;
		}
	}

	return remote;

}

#pragma endregion

#pragma region answer

// the room of the body of the answer for more bytes and its terminator
// returns true when it has it
static bool http_remote_response_reserve (HttpRemote *remote, const size_t bytes) {

	const size_t needed = remote->response_size + bytes + 1;

	bool reserved = (needed <= remote->response_capacity);
	if (!reserved) {
		size_t capacity = remote->response_capacity ? remote->response_capacity : HTTP_REMOTE_RESPONSE_INITIAL_SIZE;
		while (capacity < needed) capacity *= HTTP_REMOTE_RESPONSE_GROWTH;

		char *response = (char *) realloc (remote->response, capacity);
		reserved = (response != NULL);
		if (reserved) {
			remote->response = response;
			remote->response_capacity = capacity;
		}
	}

	return reserved;

}

// the body of the answer as it arrives; taking less than what came fails the request
static size_t http_remote_write (char *data, size_t size, size_t n_members, void *remote_ptr) {

	HttpRemote *remote = (HttpRemote *) remote_ptr;

	const size_t bytes = size * n_members;
	size_t taken = 0;

	if ((remote->response_size + bytes) > HTTP_AUTH_RESPONSE_MAX_BODY_SIZE) {
		remote->response_overflow = true;
	}

	else if (http_remote_response_reserve (remote, bytes)) {
		(void) memcpy (remote->response + remote->response_size, data, bytes);
		remote->response_size += bytes;
		remote->response[remote->response_size] = '\0';

		taken = bytes;
	}

	return taken;

}

// the status of the answer, 0 when the request failed
static void http_remote_finished (HttpRemote *remote, CURL *easy, const CURLcode result) {

	long status = 0;
	if ((result != CURLE_OK) || (curl_easy_getinfo (easy, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK)) status = 0;

	remote->status = (unsigned int) status;

}

#pragma endregion

#pragma region easy

// the method of the request: GET and POST are the ones of libcurl, any other one is named
static void http_remote_easy_method (CURL *easy, const HttpRemote *remote) {

	if (remote->body) {
		(void) curl_easy_setopt (easy, CURLOPT_POSTFIELDSIZE, (long) remote->body_size);
		(void) curl_easy_setopt (easy, CURLOPT_POSTFIELDS, remote->body);
	}

	else if (remote->method == REQUEST_METHOD_POST) {
		(void) curl_easy_setopt (easy, CURLOPT_POSTFIELDSIZE, 0L);
		(void) curl_easy_setopt (easy, CURLOPT_POSTFIELDS, http_remote_no_body);
	}

	const bool named = !((remote->method == REQUEST_METHOD_GET) && !remote->body)
		&& (remote->method != REQUEST_METHOD_POST);

	if (named) (void) curl_easy_setopt (easy, CURLOPT_CUSTOMREQUEST, http_request_method_to_string (remote->method));

	if (remote->method == REQUEST_METHOD_HEAD) (void) curl_easy_setopt (easy, CURLOPT_NOBODY, 1L);

}

// the options of a request in a handle without any
static void http_remote_easy_set (CURL *easy, HttpRemote *remote) {

	(void) curl_easy_setopt (easy, CURLOPT_URL, remote->url);
	(void) curl_easy_setopt (easy, CURLOPT_NOSIGNAL, 1L);
	(void) curl_easy_setopt (easy, CURLOPT_TIMEOUT_MS, (long) remote->timeout_milliseconds);
	(void) curl_easy_setopt (easy, CURLOPT_HTTPHEADER, (struct curl_slist *) remote->headers);
	(void) curl_easy_setopt (easy, CURLOPT_WRITEFUNCTION, http_remote_write);
	(void) curl_easy_setopt (easy, CURLOPT_WRITEDATA, remote);

	http_remote_easy_method (easy, remote);

}

// a handle of its own for a request, the one that waits in the multi of a loop
static CURL *http_remote_easy_create (HttpRemote *remote) {

	CURL *easy = curl_easy_init ();
	if (easy) http_remote_easy_set (easy, remote);

	return easy;

}

// the first request of a thread creates its handle, with the key that releases it when the thread ends
static CURL *http_remote_thread_easy_create (void) {

	CURL *easy = http_remote_easy_key_created ? curl_easy_init () : NULL;
	if (easy && pthread_setspecific (http_remote_easy_key, easy)) {
		curl_easy_cleanup (easy);
		easy = NULL;
	}

	http_remote_thread_easy = easy;

	return easy;

}

// the handle of the thread without the options of its last request: a reset keeps its connections,
// NULL when the thread could not have one
static CURL *http_remote_thread_easy_get (void) {

	CURL *easy = http_remote_thread_easy;
	if (easy) curl_easy_reset (easy);
	else easy = http_remote_thread_easy_create ();

	return easy;

}

#pragma endregion

#pragma region perform

// the request in the thread of the authentication, and its answer
// returns 0 when the request is authenticated, 1 when it is not
static unsigned int http_remote_perform (const HttpReceive *http_receive, HttpRemote *remote) {

	// with the handle of the thread, that keeps the connection to the service; a thread without one
	// makes the request with a handle that ends with it
	CURL *thread_easy = http_remote_thread_easy_get ();
	CURL *easy = thread_easy ? thread_easy : curl_easy_init ();
	if (easy) {
		http_remote_easy_set (easy, remote);

		const CURLcode result = curl_easy_perform (easy);
		http_remote_finished (remote, easy, result);

		if (!thread_easy) curl_easy_cleanup (easy);
	}

	const unsigned int verdict = http_remote_respond (http_receive, remote);

	http_remote_delete (remote);

	return verdict;

}

// only the authentication of the headers of a request of an event loop can wait in its loop, once
static bool http_remote_waits_in_loop (const HttpReceive *http_receive) {

	const HttpConnection *connection = http_receive->connection;

	return connection && connection->remote_allowed && !connection->remote;

}

// the connection keeps the request, its loop sends it when the job of its authentication gives it back
static unsigned int http_remote_leave (const HttpReceive *http_receive, HttpRemote *remote) {

	http_receive->connection->remote = remote;

	return HTTP_AUTHENTICATION_PENDING;

}

unsigned int http_auth_request (
	const HttpReceive *http_receive, const HttpAuthRequest *request,
	HttpAuthResponseHandler on_response, void *data
) {

	unsigned int retval = 1;

	if (http_receive && request && request->url && on_response) {
		(void) pthread_once (&http_remote_once, http_remote_global_start);

		HttpRemote *remote = http_remote_create (request, on_response, data);
		if (remote) {
			retval = http_remote_waits_in_loop (http_receive) ?
				http_remote_leave (http_receive, remote) : http_remote_perform (http_receive, remote);
		}
	}

	return retval;

}

#pragma endregion

#pragma region loop

// a socket of the requests of a loop, the source that its poller reports
typedef struct HttpRemoteSocket {

	EventSource source;

	curl_socket_t fd;

	// the sockets of the loop that libcurl did not remove, released with it
	struct HttpRemoteSocket *next;
	struct HttpRemoteSocket *previous;

} HttpRemoteSocket;

// the multi of libcurl of a loop, with its sockets and the deadline of its timer
struct HttpRemoteLoop {

	EventLoop *loop;

	CURLM *multi;

	// the timer of libcurl, in the milliseconds of the loop
	bool timer;
	u64 deadline;

	HttpRemoteSocket *sockets;

};

static void http_remote_socket_link (struct HttpRemoteLoop *remote, HttpRemoteSocket *socket) {

	socket->previous = NULL;
	socket->next = remote->sockets;
	if (remote->sockets) remote->sockets->previous = socket;
	remote->sockets = socket;

}

static void http_remote_socket_unlink (struct HttpRemoteLoop *remote, HttpRemoteSocket *socket) {

	if (socket->previous) socket->previous->next = socket->next;
	else remote->sockets = socket->next;

	if (socket->next) socket->next->previous = socket->previous;

}

static void http_remote_socket_remove (struct HttpRemoteLoop *remote, HttpRemoteSocket *socket) {

	if (socket) {
		(void) event_poller_remove (remote->loop->poller, &socket->source);

		http_remote_socket_unlink (remote, socket);

		free (socket);
	}

}

// the interest of libcurl in a socket, in the poller of the loop
static void http_remote_socket_watch (
	struct HttpRemoteLoop *remote, HttpRemoteSocket *socket, const curl_socket_t fd, const int what
) {

	if (!socket) {
		socket = (HttpRemoteSocket *) calloc (1, sizeof (HttpRemoteSocket));
		if (socket) {
			event_source_init (&socket->source, EVENT_SOURCE_TYPE_REMOTE, socket);
			socket->fd = fd;

			http_remote_socket_link (remote, socket);

			(void) curl_multi_assign (remote->multi, fd, socket);
		}
	}

	if (socket) {
		const unsigned int interest = ((what & CURL_POLL_IN) ? EVENT_INTEREST_READ : EVENT_INTEREST_NONE)
			| ((what & CURL_POLL_OUT) ? EVENT_INTEREST_WRITE : EVENT_INTEREST_NONE);

		(void) event_poller_watch (remote->loop->poller, &socket->source, fd, interest);
	}

}

static int http_remote_loop_socket (CURL *easy, curl_socket_t fd, int what, void *remote_ptr, void *socket_ptr) {

	(void) easy;

	struct HttpRemoteLoop *remote = (struct HttpRemoteLoop *) remote_ptr;
	HttpRemoteSocket *socket = (HttpRemoteSocket *) socket_ptr;

	if (what == CURL_POLL_REMOVE) http_remote_socket_remove (remote, socket);
	else http_remote_socket_watch (remote, socket, fd, what);

	return 0;

}

// the timer of libcurl is a deadline of the loop, more precise than its wheel
static int http_remote_loop_timer (CURLM *multi, long timeout_milliseconds, void *remote_ptr) {

	(void) multi;

	struct HttpRemoteLoop *remote = (struct HttpRemoteLoop *) remote_ptr;

	remote->timer = (timeout_milliseconds >= 0);
	remote->deadline = remote->timer ? (remote->loop->now + (u64) timeout_milliseconds) : 0;

	return 0;

}

static struct HttpRemoteLoop *http_remote_loop_create (EventLoop *loop) {

	(void) pthread_once (&http_remote_once, http_remote_global_start);

	struct HttpRemoteLoop *remote = (struct HttpRemoteLoop *) calloc (1, sizeof (struct HttpRemoteLoop));
	if (remote) {
		remote->loop = loop;

		remote->multi = curl_multi_init ();
		if (remote->multi) {
			(void) curl_multi_setopt (remote->multi, CURLMOPT_SOCKETFUNCTION, http_remote_loop_socket);
			(void) curl_multi_setopt (remote->multi, CURLMOPT_SOCKETDATA, remote);
			(void) curl_multi_setopt (remote->multi, CURLMOPT_TIMERFUNCTION, http_remote_loop_timer);
			(void) curl_multi_setopt (remote->multi, CURLMOPT_TIMERDATA, remote);
			(void) curl_multi_setopt (remote->multi, CURLMOPT_MAXCONNECTS, (long) HTTP_REMOTE_LOOP_MAX_CONNECTIONS);
			(void) curl_multi_setopt (remote->multi, CURLMOPT_MAX_HOST_CONNECTIONS, (long) HTTP_REMOTE_LOOP_MAX_HOST_CONNECTIONS);

			loop->remote = remote;
		}

		else {
			free (remote);
			remote = NULL;
		}
	}

	return remote;

}

void http_remote_loop_delete (EventLoop *loop) {

	struct HttpRemoteLoop *remote = loop->remote;
	if (remote) {
		(void) curl_multi_cleanup (remote->multi);

		// the sockets that libcurl did not remove
		while (remote->sockets) http_remote_socket_remove (remote, remote->sockets);

		free (remote);
		loop->remote = NULL;
	}

}

int http_remote_loop_wait (const EventLoop *loop) {

	const struct HttpRemoteLoop *remote = loop->remote;

	int wait = EVENT_WHEEL_NEXT_NONE;
	if (remote && remote->timer) wait = (remote->deadline > loop->now) ? (int) (remote->deadline - loop->now) : 0;

	return wait;

}

// the request of the connection leaves the multi, its connection stays in the cache of the multi
static void http_remote_easy_end (struct HttpRemoteLoop *remote, HttpRemote *request) {

	CURL *easy = (CURL *) request->easy;
	request->easy = NULL;

	(void) curl_multi_remove_handle (remote->multi, easy);
	curl_easy_cleanup (easy);

}

static void http_remote_loop_ended (struct HttpRemoteLoop *remote, CURL *easy, const CURLcode result) {

	char *connection_ptr = NULL;
	(void) curl_easy_getinfo (easy, CURLINFO_PRIVATE, &connection_ptr);

	HttpConnection *connection = (HttpConnection *) connection_ptr;
	HttpRemote *request = connection->remote;

	http_remote_finished (request, easy, result);

	http_remote_easy_end (remote, request);

	http_connection_remote_answered (connection);

}

// gives each request that ended back to its connection
// returns how many ended
static unsigned int http_remote_loop_done (struct HttpRemoteLoop *remote) {

	unsigned int ended = 0;

	int left = 0;
	CURLMsg *message = NULL;
	while ((message = curl_multi_info_read (remote->multi, &left))) {
		if (message->msg == CURLMSG_DONE) {
			// the message does not survive the end of its request
			CURL *easy = message->easy_handle;
			const CURLcode result = message->data.result;

			http_remote_loop_ended (remote, easy, result);

			ended += 1;
		}
	}

	return ended;

}

unsigned int http_remote_loop_step (EventLoop *loop) {

	struct HttpRemoteLoop *remote = loop->remote;

	unsigned int ended = 0;

	if (remote) {
		if (remote->timer && (loop->now >= remote->deadline)) {
			// the timer can be set again inside the action
			remote->timer = false;

			int running = 0;
			(void) curl_multi_socket_action (remote->multi, CURL_SOCKET_TIMEOUT, 0, &running);
		}

		ended = http_remote_loop_done (remote);
	}

	return ended;

}

void http_remote_loop_ready (EventLoop *loop, const EventSource *source, const unsigned int ready) {

	struct HttpRemoteLoop *remote = loop->remote;

	// the action can remove the socket
	const curl_socket_t fd = ((const HttpRemoteSocket *) source->owner)->fd;

	const int flags = ((ready & EVENT_READY_READ) ? CURL_CSELECT_IN : 0)
		| ((ready & EVENT_READY_WRITE) ? CURL_CSELECT_OUT : 0)
		| ((ready & EVENT_READY_ERROR) ? CURL_CSELECT_ERR : 0);

	int running = 0;
	(void) curl_multi_socket_action (remote->multi, fd, flags, &running);

	(void) http_remote_loop_done (remote);

}

unsigned int http_remote_loop_start (EventLoop *loop, HttpConnection *connection) {

	unsigned int retval = 1;

	HttpRemote *request = connection->remote;

	struct HttpRemoteLoop *remote = loop->remote ? loop->remote : http_remote_loop_create (loop);
	CURL *easy = remote ? http_remote_easy_create (request) : NULL;
	if (easy) {
		(void) curl_easy_setopt (easy, CURLOPT_PRIVATE, (void *) connection);

		if (curl_multi_add_handle (remote->multi, easy) == CURLM_OK) {
			request->easy = easy;
			retval = 0;
		}

		else {
			curl_easy_cleanup (easy);
		}
	}

	return retval;

}

void http_remote_loop_cancel (EventLoop *loop, HttpConnection *connection) {

	HttpRemote *request = connection->remote;
	if (loop->remote && request && request->easy) {
		http_remote_easy_end (loop->remote, request);

		request->status = 0;
	}

}

#pragma endregion

#else

#pragma region without curl

unsigned int http_auth_request (
	const HttpReceive *http_receive, const HttpAuthRequest *request,
	HttpAuthResponseHandler on_response, void *data
) {

	(void) http_receive;
	(void) request;
	(void) on_response;
	(void) data;

	return 1;

}

void http_remote_loop_delete (EventLoop *loop) {

	(void) loop;

}

int http_remote_loop_wait (const EventLoop *loop) {

	(void) loop;

	return EVENT_WHEEL_NEXT_NONE;

}

unsigned int http_remote_loop_step (EventLoop *loop) {

	(void) loop;

	return 0;

}

void http_remote_loop_ready (EventLoop *loop, const EventSource *source, const unsigned int ready) {

	(void) loop;
	(void) source;
	(void) ready;

}

unsigned int http_remote_loop_start (EventLoop *loop, HttpConnection *connection) {

	(void) loop;
	(void) connection;

	return 1;

}

void http_remote_loop_cancel (EventLoop *loop, HttpConnection *connection) {

	(void) loop;
	(void) connection;

}

#pragma endregion

#endif
