#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/events/events.h"
#include "cerver/events/internal.h"

#include "cerver/http/clients/client.h"
#include "cerver/http/clients/internal.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/types.h"

#include "cerver/utils/log.h"

// a client gives its requests to event loops: its own one, that its thread runs, or the ones of its cerver.
// The loops of a cerver exist from its start until its end: a thread that gives them a request says so first,
// and the cerver releases its loops when no thread is doing it

// the milliseconds between two looks of a thread that waits for a client or for the loops of a cerver
#define HTTP_CLIENT_POLL_MILLISECONDS			1
#define HTTP_CLIENT_NANOSECONDS_PER_MILLISECOND	1000000L

// the milliseconds that the end of a client waits for its requests and its connections, over the times that
// they have: after them the loops of its cerver are not running anymore, and the client is not released
#define HTTP_CLIENT_DELETE_GRACE				4000

// the pools of a client alone: the one of its loop
#define HTTP_CLIENT_ALONE_POOLS					1

static void http_client_sleep (void) {

	struct timespec interval = { 0, HTTP_CLIENT_POLL_MILLISECONDS * HTTP_CLIENT_NANOSECONDS_PER_MILLISECOND };

	(void) nanosleep (&interval, NULL);

}

#pragma region cerver

void http_clients_open (CerverHttp *http_cerver) {

	if (http_cerver) __atomic_store_n (&http_cerver->clients_open, true, __ATOMIC_SEQ_CST);

}

// the flag goes first and the look at the threads after it: a thread that gives a request counts itself before
// it looks at the flag, so either this sees it or it sees that the loops are closed
void http_clients_close (CerverHttp *http_cerver) {

	if (http_cerver) {
		__atomic_store_n (&http_cerver->clients_open, false, __ATOMIC_SEQ_CST);

		while (__atomic_load_n (&http_cerver->clients_sending, __ATOMIC_SEQ_CST)) http_client_sleep ();
	}

}

// the calling thread uses the loops of a client until http_client_leave (): false when they do not exist
static bool http_client_enter (const CerverHttpClient *client) {

	bool entered = true;

	CerverHttp *http_cerver = client->http_cerver;
	if (http_cerver) {
		(void) __atomic_add_fetch (&http_cerver->clients_sending, 1, __ATOMIC_SEQ_CST);

		entered = __atomic_load_n (&http_cerver->clients_open, __ATOMIC_SEQ_CST);

		if (!entered) (void) __atomic_sub_fetch (&http_cerver->clients_sending, 1, __ATOMIC_SEQ_CST);
	}

	return entered;

}

static void http_client_leave (const CerverHttpClient *client) {

	if (client->http_cerver) {
		(void) __atomic_sub_fetch (&client->http_cerver->clients_sending, 1, __ATOMIC_SEQ_CST);
	}

}

static const EventLoopGroup *http_client_group (const CerverHttpClient *client) {

	return client->http_cerver->cerver->event_loops;

}

#pragma endregion

#pragma region loops

// the loop of the calling thread among the ones of a client, NULL when the thread runs none of them
static EventLoop *http_client_current_loop (const CerverHttpClient *client) {

	EventLoop *current = NULL;

	if (client->loop) {
		if (event_loop_is_current (client->loop)) current = client->loop;
	}

	else {
		const EventLoopGroup *group = http_client_group (client);

		for (unsigned int idx = 0; group && (idx < group->n_loops) && !current; idx++) {
			if (event_loop_is_current (group->loops[idx])) current = group->loops[idx];
		}
	}

	return current;

}

// the loop of a request that was sent by a thread that runs none: each loop of a cerver in its turn
static EventLoop *http_client_next_loop (CerverHttpClient *client) {

	EventLoop *loop = client->loop;

	if (!loop) {
		const EventLoopGroup *group = http_client_group (client);

		if (group && group->n_loops) {
			const unsigned int turn = __atomic_fetch_add (&client->next_loop, 1, __ATOMIC_RELAXED);

			loop = group->loops[turn % group->n_loops];
		}
	}

	return loop;

}

static HttpClientPool *http_client_pools_new (CerverHttpClient *client, const unsigned int n_pools) {

	HttpClientPool *pools = (HttpClientPool *) calloc (n_pools, sizeof (HttpClientPool));
	if (pools) {
		for (unsigned int idx = 0; idx < n_pools; idx++) {
			HttpClientPool *pool = &pools[idx];

			event_source_init (&pool->source, &http_client_pool_methods);

			pool->client = client;
		}
	}

	return pools;

}

// the pools of the client of a cerver are created with its first request, one for each loop that the cerver has
// then: the thread that sets them first wins, the others release theirs
static bool http_client_pools_ready (CerverHttpClient *client) {

	bool ready = __atomic_load_n (&client->pools, __ATOMIC_SEQ_CST) != NULL;

	if (!ready) {
		const EventLoopGroup *group = http_client_group (client);

		HttpClientPool *pools = (group && group->n_loops) ? http_client_pools_new (client, group->n_loops) : NULL;
		if (pools) {
			// every thread that gets here sets the same count
			__atomic_store_n (&client->n_pools, group->n_loops, __ATOMIC_SEQ_CST);

			HttpClientPool *none = NULL;
			if (!__atomic_compare_exchange_n (
				&client->pools, &none, pools, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST
			)) {
				free (pools);
			}

			ready = true;
		}
	}

	return ready;

}

// a request that waits for its response can not be sent by a thread that runs a loop of its client: nobody
// would run the loop
static EventLoop *http_client_request_loop (CerverHttpClientRequest *request) {

	CerverHttpClient *client = request->client;

	EventLoop *current = http_client_current_loop (client);

	EventLoop *loop = NULL;

	if (!current) loop = http_client_next_loop (client);
	else if (!request->blocking) loop = current;

	return loop;

}

// the request counts before the look at the end of its client, that looks at its requests after it said that
// it ends: one of the two always sees the other
static bool http_client_request_count (CerverHttpClient *client) {

	(void) __atomic_add_fetch (&client->n_requests, 1, __ATOMIC_SEQ_CST);

	const bool counted = !__atomic_load_n (&client->deleting, __ATOMIC_SEQ_CST);

	if (!counted) (void) __atomic_sub_fetch (&client->n_requests, 1, __ATOMIC_SEQ_CST);

	return counted;

}

static u8 http_client_request_give_loop (CerverHttpClientRequest *request) {

	u8 retval = 1;

	CerverHttpClient *client = request->client;

	EventLoop *loop = http_client_pools_ready (client) ? http_client_request_loop (request) : NULL;

	if (loop && !event_loop_source_add (loop, &request->source)) {
		request->state = HTTP_CLIENT_STATE_GIVEN;

		event_loop_source_give (&request->source);

		retval = 0;
	}

	return retval;

}

u8 http_client_request_give (CerverHttpClientRequest *request) {

	u8 retval = 1;

	CerverHttpClient *client = request->client;

	if (http_client_request_count (client)) {
		if (http_client_enter (client)) {
			retval = http_client_request_give_loop (request);

			http_client_leave (client);
		}

		if (retval) (void) __atomic_sub_fetch (&client->n_requests, 1, __ATOMIC_SEQ_CST);
	}

	return retval;

}

#pragma endregion

#pragma region client

static void *http_client_thread (void *loop_ptr) {

	event_loop_run ((EventLoop *) loop_ptr);

	return NULL;

}

static CerverHttpClient *http_client_new (void) {

	CerverHttpClient *client = (CerverHttpClient *) calloc (1, sizeof (CerverHttpClient));
	if (client) {
		client->connect_timeout = CERVER_HTTP_CLIENT_DEFAULT_CONNECT_TIMEOUT;
		client->request_timeout = CERVER_HTTP_CLIENT_DEFAULT_REQUEST_TIMEOUT;
		client->idle_timeout = CERVER_HTTP_CLIENT_DEFAULT_IDLE_TIMEOUT;
		client->max_idle = CERVER_HTTP_CLIENT_DEFAULT_MAX_IDLE_CONNECTIONS;
		client->max_response_size = CERVER_HTTP_CLIENT_DEFAULT_MAX_RESPONSE_SIZE;
	}

	return client;

}

static void http_client_free (CerverHttpClient *client) {

	event_loop_delete (client->loop);

	free (client->pools);

	free (client);

}

// a client alone has its own loop, the pool of that loop and the thread that runs it
static bool http_client_alone_start (CerverHttpClient *client, const CerverEventsBackend backend) {

	client->loop = event_loop_create_alone (backend);

	client->pools = http_client_pools_new (client, HTTP_CLIENT_ALONE_POOLS);
	client->n_pools = HTTP_CLIENT_ALONE_POOLS;

	client->started = client->loop && client->pools
		&& !pthread_create (&client->thread, NULL, http_client_thread, client->loop);

	return client->started;

}

CerverHttpClient *cerver_http_client_create_with_backend (
	const CerverEventsBackend backend
) {

	CerverHttpClient *client = http_client_new ();

	if (client && !http_client_alone_start (client, backend)) {
		http_client_free (client);
		client = NULL;
	}

	return client;

}

CerverHttpClient *cerver_http_client_create (
	CerverHttp *http_cerver
) {

	CerverHttpClient *client = NULL;

	if (http_cerver) {
		client = http_client_new ();

		if (client) client->http_cerver = http_cerver;
	}

	else {
		client = cerver_http_client_create_with_backend (CERVER_EVENTS_BACKEND_AUTO);
	}

	return client;

}

// no thread looks for an address for the client: the ones that did left its loops and the client
static void http_client_wait_resolving (const CerverHttpClient *client) {

	while (__atomic_load_n (&client->n_resolving, __ATOMIC_SEQ_CST)) http_client_sleep ();

}

// the loop of a client alone ends: it tells its requests, that fail, and its connections, that are closed, and
// its thread ends when all of them left
static void http_client_alone_end (CerverHttpClient *client) {

	event_loop_finish (client->loop);

	(void) pthread_join (client->thread, NULL);

	http_client_wait_resolving (client);

}

static bool http_client_is_quiet (const CerverHttpClient *client) {

	return !__atomic_load_n (&client->n_requests, __ATOMIC_SEQ_CST);

}

// no loop is closing a pool of the client
static bool http_client_pools_are_closed (const CerverHttpClient *client) {

	bool closed = true;

	const unsigned int n_pools = __atomic_load_n (&client->n_pools, __ATOMIC_SEQ_CST);

	for (unsigned int idx = 0; (idx < n_pools) && closed; idx++) {
		closed = __atomic_load_n (&client->pools[idx].state, __ATOMIC_SEQ_CST) != HTTP_CLIENT_POOL_STATE_CLOSING;
	}

	return closed;

}

// nothing uses the client: its loops left its pools, its connections were released, and no thread looks for an
// address for it
static bool http_client_is_closed (const CerverHttpClient *client) {

	return http_client_pools_are_closed (client)
		&& !__atomic_load_n (&client->n_alive, __ATOMIC_SEQ_CST)
		&& !__atomic_load_n (&client->n_resolving, __ATOMIC_SEQ_CST);

}

// waits until a condition of a client is true, false when the milliseconds ended first; 0 waits without a limit
static bool http_client_wait_until (
	const CerverHttpClient *client, bool (*condition) (const CerverHttpClient *client), const u64 milliseconds
) {

	u64 waited = 0;

	bool reached = condition (client);
	while (!reached && (!milliseconds || (waited < milliseconds))) {
		http_client_sleep ();

		waited += HTTP_CLIENT_POLL_MILLISECONDS;

		reached = condition (client);
	}

	return reached;

}

// each loop that keeps connections of the client closes them
static void http_client_pools_close (CerverHttpClient *client) {

	if (http_client_enter (client)) {
		const unsigned int n_pools = __atomic_load_n (&client->n_pools, __ATOMIC_SEQ_CST);

		for (unsigned int idx = 0; idx < n_pools; idx++) {
			HttpClientPool *pool = &client->pools[idx];

			EventLoop *loop = __atomic_load_n (&pool->loop, __ATOMIC_SEQ_CST);

			// a loop that ends takes nothing more: it closes the connections of the pool by itself
			if (loop && !event_loop_source_add (loop, &pool->source)) {
				__atomic_store_n (&pool->state, HTTP_CLIENT_POOL_STATE_CLOSING, __ATOMIC_SEQ_CST);

				event_loop_source_give (&pool->source);
			}
		}

		http_client_leave (client);
	}

}

// the requests of the client of a cerver end by themselves, each one within the time of its client (without a
// limit when it has none); then its loops close the connections that they keep. The loops of a cerver that ended
// left nothing of the client
static bool http_client_cerver_end (CerverHttpClient *client) {

	const u64 milliseconds = client->request_timeout ?
		(client->request_timeout + client->connect_timeout + HTTP_CLIENT_DELETE_GRACE) : 0;

	bool ended = http_client_wait_until (client, http_client_is_quiet, milliseconds);

	if (ended) {
		http_client_pools_close (client);

		ended = http_client_wait_until (client, http_client_is_closed, HTTP_CLIENT_DELETE_GRACE);
	}

	return ended;

}

void cerver_http_client_delete (
	CerverHttpClient *client
) {

	if (client) {
		__atomic_store_n (&client->deleting, true, __ATOMIC_SEQ_CST);

		if (client->loop) {
			http_client_alone_end (client);

			http_client_free (client);
		}

		else if (http_client_cerver_end (client)) {
			http_client_free (client);
		}

		// what is left of the client is still used by its loops: its memory stays
		else {
			cerver_log_warning (
				"cerver_http_client_delete () - the client still has %u requests and %u connections, "
				"it was NOT released",
				__atomic_load_n (&client->n_requests, __ATOMIC_SEQ_CST),
				__atomic_load_n (&client->n_alive, __ATOMIC_SEQ_CST)
			);
		}
	}

}

void cerver_http_client_set_connect_timeout (
	CerverHttpClient *client, const u32 milliseconds
) {

	if (client) client->connect_timeout = milliseconds;

}

void cerver_http_client_set_request_timeout (
	CerverHttpClient *client, const u32 milliseconds
) {

	if (client) client->request_timeout = milliseconds;

}

void cerver_http_client_set_idle_timeout (
	CerverHttpClient *client, const u32 milliseconds
) {

	if (client) client->idle_timeout = milliseconds;

}

void cerver_http_client_set_max_idle_connections (
	CerverHttpClient *client, const unsigned int connections
) {

	if (client) client->max_idle = connections;

}

void cerver_http_client_set_max_response_size (
	CerverHttpClient *client, const size_t size
) {

	if (client) client->max_response_size = size;

}

u64 cerver_http_client_get_connections (
	const CerverHttpClient *client
) {

	return client ? __atomic_load_n (&client->n_connections, __ATOMIC_SEQ_CST) : 0;

}

u64 cerver_http_client_get_reused (
	const CerverHttpClient *client
) {

	return client ? __atomic_load_n (&client->n_reused, __ATOMIC_SEQ_CST) : 0;

}

#pragma endregion
