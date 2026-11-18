#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libgen.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"
#include "cerver/connection/end.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/internal.h"
#include "cerver/network/socket.h"

#include "cerver/threads/thpool.h"
#include "cerver/threads/thread.h"

#include "cerver/tls/cerver.h"
#include "cerver/tls/internal.h"
#include "cerver/tls/tls.h"

#include "cerver/utils/log.h"

// the cervers whose eventfd a reload signal writes: 2^4
#define CERVER_TLS_SIGNAL_SLOTS				16

// the events of the folders of the certificates that a renewal makes
#define CERVER_TLS_WATCH_EVENTS				(IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)

// the bytes of the events of inotify that are read at once
#define CERVER_TLS_WATCH_BUFFER_SIZE		4096

// the milliseconds of a second
#define CERVER_TLS_MILLISECONDS_PER_SECOND	1000

// the name of the threads of the handshakes and of the maintenance
#define CERVER_TLS_HANDSHAKES_NAME			"tls"
#define CERVER_TLS_MAINTENANCE_NAME			"tls-maintenance"

// a poll () of the maintenance that waits forever
#define CERVER_TLS_WAIT_FOREVER				(-1)

// the fds of the maintenance: its eventfd, and the one of inotify when it watches the files
#define CERVER_TLS_MAINTENANCE_FDS			2
#define CERVER_TLS_MAINTENANCE_WAKE_FDS		1

// the eventfds of the cervers that reload with a signal: the handler only writes them
static int cerver_tls_signal_fds[CERVER_TLS_SIGNAL_SLOTS] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

#pragma region settings

static CerverTls *cerver_tls_create (TlsSettings *settings) {

	CerverTls *tls = (CerverTls *) calloc (1, sizeof (CerverTls));
	if (tls) {
		tls->settings = settings;
		tls->wake_fd = -1;
		tls->reload_debounce_milliseconds = TLS_RELOAD_DEBOUNCE_MILLISECONDS_DEFAULT;

		(void) pthread_mutex_init (&tls->mutex, NULL);
	}

	return tls;

}

void cerver_tls_delete (void *tls_ptr) {

	CerverTls *tls = (CerverTls *) tls_ptr;
	if (tls) {
		cerver_crypto_provider_tls_context_release (tls->context);
		tls_settings_delete (tls->settings);

		(void) pthread_mutex_destroy (&tls->mutex);

		free (tls);
	}

}

u8 cerver_set_tls (Cerver *cerver, TlsSettings *settings) {

	u8 retval = 1;

	const bool valid = cerver && settings && (cerver->protocol == PROTOCOL_TCP);

	CerverTls *tls = valid ? cerver_tls_create (settings) : NULL;
	if (tls) {
		cerver_tls_delete (cerver->tls);
		cerver->tls = tls;
		retval = 0;
	}

	else {
		tls_settings_delete (settings);
	}

	return retval;

}

bool cerver_has_tls (const Cerver *cerver) {

	return cerver && cerver->tls;

}

void cerver_tls_set_reload_signal (Cerver *cerver, const int signal_number) {

	if (cerver && cerver->tls) cerver->tls->reload_signal = signal_number;

}

void cerver_tls_set_reload_watch (
	Cerver *cerver, const bool enable, const unsigned int debounce_milliseconds
) {

	if (cerver && cerver->tls) {
		cerver->tls->reload_watch = enable;
		cerver->tls->reload_debounce_milliseconds = debounce_milliseconds ?
			debounce_milliseconds : TLS_RELOAD_DEBOUNCE_MILLISECONDS_DEFAULT;
	}

}

u8 cerver_tls_get_stats (const Cerver *cerver, TlsStats *stats) {

	u8 retval = 1;

	if (cerver && cerver->tls && stats) {
		tls_counters_copy (&cerver->tls->counters, stats);
		retval = 0;
	}

	return retval;

}

#pragma endregion

#pragma region context

// the context of the new connections, with a reference for the caller
static CryptoTlsContext *cerver_tls_context_get (CerverTls *tls) {

	(void) pthread_mutex_lock (&tls->mutex);
	CryptoTlsContext *context = cerver_crypto_provider_tls_context_reference (tls->context);
	(void) pthread_mutex_unlock (&tls->mutex);

	return context;

}

// the new context replaces the current one: the connections that are alive keep their reference to the previous one
static void cerver_tls_context_set (CerverTls *tls, CryptoTlsContext *context) {

	(void) pthread_mutex_lock (&tls->mutex);
	CryptoTlsContext *previous = tls->context;
	tls->context = context;
	(void) pthread_mutex_unlock (&tls->mutex);

	cerver_crypto_provider_tls_context_release (previous);

	__atomic_store_n (
		&tls->counters.stats.certificate_expiry_seconds,
		cerver_crypto_provider_tls_context_expiry_seconds (context), __ATOMIC_RELAXED
	);

}

static CryptoTlsContext *cerver_tls_context_build (const Cerver *cerver, CerverTls *tls) {

	char error[TLS_ERROR_SIZE] = { 0 };

	CryptoTlsContext *context = cerver_crypto_provider_tls_context_create (
		tls->settings, true, cerver->type == CERVER_TYPE_WEB, error, sizeof (error)
	);

	if (!context) {
		cerver_log_error ("Cerver %s TLS - %s", cerver->info->name, error);
	}

	return context;

}

u8 cerver_tls_reload (Cerver *cerver) {

	u8 retval = 1;

	if (cerver && cerver->tls) {
		CerverTls *tls = cerver->tls;
		tls_counter_add (&tls->counters.stats.reloads, 1);

		CryptoTlsContext *context = cerver_tls_context_build (cerver, tls);
		if (context) {
			cerver_tls_context_set (tls, context);

			time_t next = 0;
			const u8 stapled = cerver_crypto_provider_tls_context_refresh_ocsp (context, &next);
			if (!stapled) tls_counter_add (&tls->counters.stats.ocsp_refreshes, 1);
			else if (stapled == 1) tls_counter_add (&tls->counters.stats.ocsp_failures, 1);

			cerver_log_success ("Cerver %s reloaded its certificates", cerver->info->name);
			retval = 0;
		}

		else {
			tls_counter_add (&tls->counters.stats.reload_failures, 1);
			cerver_log_warning ("Cerver %s kept its previous certificates", cerver->info->name);
		}
	}

	return retval;

}

#pragma endregion

#pragma region connections

bool cerver_tls_attach (Cerver *cerver, Socket *socket) {

	CerverTls *tls = cerver->tls;

	CryptoTlsContext *context = cerver_tls_context_get (tls);
	TlsConnection *connection = context ? tls_connection_create (
		context, socket->sock_fd, true, tls->settings->ktls, NULL, NULL, &tls->counters
	) : NULL;

	cerver_crypto_provider_tls_context_release (context);

	if (connection) socket->tls = connection;
	else tls_counter_add (&tls->counters.stats.failures[TLS_FAILURE_MEMORY], 1);

	return (connection != NULL);

}

unsigned int cerver_tls_handshake_timeout (const Cerver *cerver) {

	return cerver->tls->settings->handshake_timeout_milliseconds;

}

bool cerver_tls_accept (Cerver *cerver, Socket *socket) {

	bool retval = false;

	if (socket->tls) {
		retval = socket->tls->established;
	}

	else if (cerver_tls_attach (cerver, socket)) {
		retval = (tls_connection_handshake (socket->tls, cerver_tls_handshake_timeout (cerver)) == TLS_FAILURE_NONE);
	}

	return retval;

}

// the job of a handshake of a cerver that is not a web one
typedef struct CerverTlsHandshake {

	Cerver *cerver;
	Connection *connection;

} CerverTlsHandshake;

static void cerver_tls_handshake_work (void *handshake_ptr) {

	CerverTlsHandshake *handshake = (CerverTlsHandshake *) handshake_ptr;

	if (cerver_tls_accept (handshake->cerver, handshake->connection->socket)) {
		cerver_register_new_connection_ready (handshake->cerver, handshake->connection);
	}

	else {
		connection_drop (handshake->cerver, handshake->connection);
	}

	free (handshake);

}

// a handshake that never ran because the cerver stopped
static void cerver_tls_handshake_reject (void *handshake_ptr) {

	CerverTlsHandshake *handshake = (CerverTlsHandshake *) handshake_ptr;

	tls_counter_add (&handshake->cerver->tls->counters.stats.failures[TLS_FAILURE_REJECTED], 1);
	connection_drop (handshake->cerver, handshake->connection);

	free (handshake);

}

u8 cerver_tls_dispatch (Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	CerverTlsHandshake *handshake = (CerverTlsHandshake *) malloc (sizeof (CerverTlsHandshake));
	if (handshake) {
		handshake->cerver = cerver;
		handshake->connection = connection;

		if (!thpool_add_work_with_reject (
			cerver->tls->handshakes, cerver_tls_handshake_work, cerver_tls_handshake_reject, handshake
		)) {
			retval = 0;
		}

		else {
			free (handshake);
		}
	}

	if (retval) tls_counter_add (&cerver->tls->counters.stats.failures[TLS_FAILURE_REJECTED], 1);

	return retval;

}

void cerver_tls_reject (Cerver *cerver) {

	tls_counter_add (&cerver->tls->counters.stats.failures[TLS_FAILURE_REJECTED], 1);

}

#pragma endregion

#pragma region signal

static void cerver_tls_signal_handler (int signal_number) {

	(void) signal_number;

	const int saved = errno;

	const u64 one = 1;
	for (unsigned int index = 0; index < CERVER_TLS_SIGNAL_SLOTS; index++) {
		const int fd = __atomic_load_n (&cerver_tls_signal_fds[index], __ATOMIC_ACQUIRE);
		if (fd >= 0) (void) !write (fd, &one, sizeof (one));
	}

	errno = saved;

}

static bool cerver_tls_signal_register (const int fd, const int signal_number) {

	bool registered = false;

	for (unsigned int index = 0; !registered && (index < CERVER_TLS_SIGNAL_SLOTS); index++) {
		int expected = -1;
		registered = __atomic_compare_exchange_n (
			&cerver_tls_signal_fds[index], &expected, fd, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE
		);
	}

	if (registered) {
		struct sigaction action;
		(void) memset (&action, 0, sizeof (struct sigaction));
		action.sa_handler = cerver_tls_signal_handler;
		action.sa_flags = SA_RESTART;
		(void) sigemptyset (&action.sa_mask);

		registered = !sigaction (signal_number, &action, NULL);

		// the threads of cerver never take the signal, the thread of the application does
		thread_signal_block_add (signal_number);
	}

	return registered;

}

static void cerver_tls_signal_unregister (const int fd) {

	for (unsigned int index = 0; index < CERVER_TLS_SIGNAL_SLOTS; index++) {
		int expected = fd;
		(void) __atomic_compare_exchange_n (
			&cerver_tls_signal_fds[index], &expected, -1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE
		);
	}

}

#pragma endregion

#pragma region watch

// the folders of the certificates and of the keys, a renewal replaces their files
static int cerver_tls_watch_create (const TlsSettings *settings) {

	int fd = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);

	for (size_t index = 0; (fd >= 0) && (index < settings->n_certificates); index++) {
		const TlsCertificatePaths *paths = &settings->certificates[index];
		const char *files[] = { paths->chain, paths->key };

		for (unsigned int file = 0; file < (sizeof (files) / sizeof (files[0])); file++) {
			if (!tls_settings_key_is_uri (files[file])) {
				char *copy = strdup (files[file]);
				if (copy) {
					(void) inotify_add_watch (fd, dirname (copy), CERVER_TLS_WATCH_EVENTS);
					free (copy);
				}
			}
		}
	}

	return fd;

}

// reads the events, returns true when something changed
static bool cerver_tls_watch_drain (const int fd) {

	bool changed = false;

	char buffer[CERVER_TLS_WATCH_BUFFER_SIZE] __attribute__ ((aligned (__alignof__ (struct inotify_event))));
	while (read (fd, buffer, sizeof (buffer)) > 0) changed = true;

	return changed;

}

#pragma endregion

#pragma region maintenance

// what the thread of the maintenance waits for: the stop, a reload, a change of the files and the next OCSP
typedef struct CerverTlsMaintenance {

	Cerver *cerver;
	int watch_fd;
	bool reload_pending;
	struct timespec reload_at;
	time_t ocsp_at;

} CerverTlsMaintenance;

static i64 cerver_tls_now_milliseconds (void) {

	struct timespec now;
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return ((i64) now.tv_sec * CERVER_TLS_MILLISECONDS_PER_SECOND) + (now.tv_nsec / (SOCKET_NANOSECONDS_PER_MILLISECOND));

}

// the milliseconds until the next thing that the maintenance does
static int cerver_tls_maintenance_timeout (const CerverTlsMaintenance *maintenance) {

	bool waits = false;
	i64 timeout = 0;

	if (maintenance->reload_pending) {
		const i64 at = ((i64) maintenance->reload_at.tv_sec * CERVER_TLS_MILLISECONDS_PER_SECOND)
			+ (maintenance->reload_at.tv_nsec / SOCKET_NANOSECONDS_PER_MILLISECOND);
		timeout = at - cerver_tls_now_milliseconds ();
		waits = true;
	}

	if (maintenance->ocsp_at) {
		const i64 ocsp = ((i64) (maintenance->ocsp_at - time (NULL))) * CERVER_TLS_MILLISECONDS_PER_SECOND;
		if (!waits || (ocsp < timeout)) timeout = ocsp;
		waits = true;
	}

	if (timeout < 0) timeout = 0;
	if (timeout > INT_MAX) timeout = INT_MAX;

	return waits ? (int) timeout : CERVER_TLS_WAIT_FOREVER;

}

static void cerver_tls_maintenance_ocsp (CerverTlsMaintenance *maintenance) {

	CerverTls *tls = maintenance->cerver->tls;

	CryptoTlsContext *context = cerver_tls_context_get (tls);

	time_t next = 0;
	const u8 result = cerver_crypto_provider_tls_context_refresh_ocsp (context, &next);
	if (!result) tls_counter_add (&tls->counters.stats.ocsp_refreshes, 1);
	else if (result == 1) tls_counter_add (&tls->counters.stats.ocsp_failures, 1);

	maintenance->ocsp_at = next ? (time (NULL) + next) : 0;

	cerver_crypto_provider_tls_context_release (context);

}

// a change of the files waits for the debounce, so both the certificate and the key are there
static void cerver_tls_maintenance_schedule_reload (CerverTlsMaintenance *maintenance) {

	maintenance->reload_pending = true;
	socket_deadline_create_milliseconds (
		&maintenance->reload_at, maintenance->cerver->tls->reload_debounce_milliseconds
	);

}

static void cerver_tls_maintenance_due (CerverTlsMaintenance *maintenance) {

	if (maintenance->reload_pending && !socket_deadline_remaining (&maintenance->reload_at)) {
		maintenance->reload_pending = false;
		(void) cerver_tls_reload (maintenance->cerver);
	}

	if (maintenance->ocsp_at && (time (NULL) >= maintenance->ocsp_at)) {
		cerver_tls_maintenance_ocsp (maintenance);
	}

}

static void cerver_tls_maintenance_events (CerverTlsMaintenance *maintenance, const struct pollfd *fds) {

	CerverTls *tls = maintenance->cerver->tls;

	if (fds[0].revents & POLLIN) {
		u64 value = 0;
		(void) !read (tls->wake_fd, &value, sizeof (value));

		// a signal reloads at once
		if (!__atomic_load_n (&tls->stopping, __ATOMIC_ACQUIRE)) (void) cerver_tls_reload (maintenance->cerver);
	}

	if ((maintenance->watch_fd >= 0) && (fds[1].revents & POLLIN) && cerver_tls_watch_drain (maintenance->watch_fd)) {
		cerver_tls_maintenance_schedule_reload (maintenance);
	}

}

static void *cerver_tls_maintenance_thread (void *maintenance_ptr) {

	CerverTlsMaintenance *maintenance = (CerverTlsMaintenance *) maintenance_ptr;
	CerverTls *tls = maintenance->cerver->tls;

	while (!__atomic_load_n (&tls->stopping, __ATOMIC_ACQUIRE)) {
		struct pollfd fds[CERVER_TLS_MAINTENANCE_FDS] = {
			{ tls->wake_fd, POLLIN, 0 },
			{ maintenance->watch_fd, POLLIN, 0 }
		};

		// without the watch only the eventfd is waited
		const nfds_t n_fds = (maintenance->watch_fd >= 0) ? CERVER_TLS_MAINTENANCE_FDS : CERVER_TLS_MAINTENANCE_WAKE_FDS;
		const int ready = poll (fds, n_fds, cerver_tls_maintenance_timeout (maintenance));

		if (ready > 0) cerver_tls_maintenance_events (maintenance, fds);

		if (!__atomic_load_n (&tls->stopping, __ATOMIC_ACQUIRE)) cerver_tls_maintenance_due (maintenance);
	}

	if (maintenance->watch_fd >= 0) (void) close (maintenance->watch_fd);
	free (maintenance);

	return NULL;

}

static bool cerver_tls_maintenance_needed (const CerverTls *tls) {

	return tls->reload_signal || tls->reload_watch || tls->settings->ocsp_stapling;

}

static u8 cerver_tls_maintenance_start (Cerver *cerver) {

	u8 retval = 1;

	CerverTls *tls = cerver->tls;

	CerverTlsMaintenance *maintenance = (CerverTlsMaintenance *) calloc (1, sizeof (CerverTlsMaintenance));
	tls->wake_fd = maintenance ? eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC) : -1;

	if (tls->wake_fd >= 0) {
		maintenance->cerver = cerver;
		maintenance->watch_fd = tls->reload_watch ? cerver_tls_watch_create (tls->settings) : -1;

		// the first OCSP answer is asked for at once by the thread: a responder that does not answer never holds the
		// start, and the first handshakes go without a staple
		if (tls->settings->ocsp_stapling) maintenance->ocsp_at = time (NULL);

		if (tls->reload_signal && !cerver_tls_signal_register (tls->wake_fd, tls->reload_signal)) {
			cerver_log_warning ("Cerver %s TLS - the reload signal could not be set", cerver->info->name);
		}

		if (!thread_create_joinable (&tls->maintenance, cerver_tls_maintenance_thread, maintenance)) {
			tls->maintenance_running = true;
			retval = 0;
		}
	}

	if (retval) {
		if (maintenance && (maintenance->watch_fd >= 0)) (void) close (maintenance->watch_fd);
		free (maintenance);
	}

	return retval;

}

static void cerver_tls_maintenance_stop (CerverTls *tls) {

	if (tls->wake_fd >= 0) {
		cerver_tls_signal_unregister (tls->wake_fd);

		__atomic_store_n (&tls->stopping, true, __ATOMIC_RELEASE);

		const u64 one = 1;
		(void) !write (tls->wake_fd, &one, sizeof (one));

		if (tls->maintenance_running) (void) pthread_join (tls->maintenance, NULL);
		tls->maintenance_running = false;

		(void) close (tls->wake_fd);
		tls->wake_fd = -1;
	}

}

#pragma endregion

#pragma region lifecycle

static u8 cerver_tls_handshakes_start (Cerver *cerver) {

	u8 retval = 0;

	CerverTls *tls = cerver->tls;

	// a web cerver runs its handshakes in the threads that receive
	if (cerver->type != CERVER_TYPE_WEB) {
		tls->handshakes = thpool_create (tls->settings->handshake_threads);
		if (tls->handshakes) thpool_set_name (tls->handshakes, CERVER_TLS_HANDSHAKES_NAME);

		retval = !tls->handshakes || thpool_init (tls->handshakes);
	}

	return retval;

}

u8 cerver_tls_start (Cerver *cerver) {

	u8 retval = 0;

	CerverTls *tls = cerver->tls;
	if (tls) {
		retval = 1;

		CryptoTlsContext *context = cerver_tls_context_build (cerver, tls);
		if (context) {
			cerver_tls_context_set (tls, context);

			retval = cerver_tls_handshakes_start (cerver);
			if (!retval && cerver_tls_maintenance_needed (tls)) retval = cerver_tls_maintenance_start (cerver);
		}
	}

	return retval;

}

void cerver_tls_end (Cerver *cerver) {

	CerverTls *tls = cerver->tls;
	if (tls) {
		cerver_tls_maintenance_stop (tls);

		// the handshakes that wait drop their connections, the ones that run end with their deadline
		if (tls->handshakes) {
			(void) thpool_drain (tls->handshakes, false, THPOOL_DRAIN_TIMEOUT_NONE);
			thpool_destroy (tls->handshakes);
			tls->handshakes = NULL;
		}
	}

}

#pragma endregion
