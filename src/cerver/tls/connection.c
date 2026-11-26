#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/tls.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "cerver/types/types.h"

#include "cerver/network/socket.h"

#include "cerver/tls/internal.h"
#include "cerver/tls/tls.h"

// the bytes of the header of a record: its type, its version and its length
#define TLS_RECORD_HEADER_SIZE				5
// where the length of a record starts in its header
#define TLS_RECORD_LENGTH_OFFSET			3
// the bits that the high byte of the length is shifted
#define TLS_RECORD_LENGTH_SHIFT				8

// the name of the upper layer protocol of kTLS
#define TLS_KERNEL_ULP_NAME					"tls"

// a poll () that waits forever
#define TLS_POLL_FOREVER					(-1)

// a buffer that runs out of room doubles
#define TLS_BUFFER_GROWTH					2

// the scratch of the receives and of the sends of a thread
static __thread u8 tls_connection_receive_scratch[TLS_INPUT_SIZE];
static __thread u8 tls_connection_send_scratch[TLS_RECORD_PLAINTEXT_SIZE];
static __thread u8 tls_connection_file_scratch[TLS_FILE_BUFFER_SIZE];

#pragma region buffers

static bool tls_buffer_init (TlsBuffer *buffer, const size_t capacity) {

	buffer->bytes = (u8 *) malloc (capacity);
	buffer->start = 0;
	buffer->end = 0;
	buffer->capacity = buffer->bytes ? capacity : 0;

	return (buffer->bytes != NULL);

}

static void tls_buffer_end (TlsBuffer *buffer) {

	free (buffer->bytes);

	buffer->bytes = NULL;
	buffer->start = 0;
	buffer->end = 0;
	buffer->capacity = 0;

}

static size_t tls_buffer_used (const TlsBuffer *buffer) {

	return buffer->end - buffer->start;

}

// the bytes that wait move to the start of the buffer
static void tls_buffer_compact (TlsBuffer *buffer) {

	const size_t used = tls_buffer_used (buffer);

	if (buffer->start) {
		if (used) (void) memmove (buffer->bytes, buffer->bytes + buffer->start, used);

		buffer->start = 0;
		buffer->end = used;
	}

}

// makes room for some more bytes, growing the buffer to the double until they fit
static bool tls_buffer_reserve (TlsBuffer *buffer, const size_t bytes) {

	bool retval = true;

	if ((buffer->capacity - buffer->end) < bytes) {
		tls_buffer_compact (buffer);

		if ((buffer->capacity - buffer->end) < bytes) {
			size_t capacity = buffer->capacity ? buffer->capacity : TLS_OUTPUT_INITIAL_SIZE;
			while ((capacity - buffer->end) < bytes) capacity *= TLS_BUFFER_GROWTH;

			u8 *grown = (u8 *) realloc (buffer->bytes, capacity);
			if (grown) {
				buffer->bytes = grown;
				buffer->capacity = capacity;
			}

			else {
				retval = false;
			}
		}
	}

	return retval;

}

static bool tls_buffer_append (TlsBuffer *buffer, const void *data, const size_t size) {

	bool retval = tls_buffer_reserve (buffer, size);
	if (retval && size) {
		(void) memcpy (buffer->bytes + buffer->end, data, size);
		buffer->end += size;
	}

	return retval;

}

static size_t tls_buffer_take (TlsBuffer *buffer, void *data, const size_t size) {

	const size_t used = tls_buffer_used (buffer);
	const size_t taken = (size < used) ? size : used;

	if (taken) {
		(void) memcpy (data, buffer->bytes + buffer->start, taken);
		buffer->start += taken;
	}

	if (buffer->start == buffer->end) {
		buffer->start = 0;
		buffer->end = 0;
	}

	return taken;

}

// a whole record already arrived, so the provider can decrypt it without receiving
static bool tls_buffer_has_record (const TlsBuffer *buffer) {

	bool retval = false;

	const size_t used = tls_buffer_used (buffer);
	if (used >= TLS_RECORD_HEADER_SIZE) {
		const u8 *header = buffer->bytes + buffer->start;
		const size_t length = ((size_t) header[TLS_RECORD_LENGTH_OFFSET] << TLS_RECORD_LENGTH_SHIFT)
			| (size_t) header[TLS_RECORD_LENGTH_OFFSET + 1];

		retval = (used >= (TLS_RECORD_HEADER_SIZE + length));
	}

	return retval;

}

#pragma endregion

#pragma region create

TlsConnection *tls_connection_create (
	CryptoTlsContext *context, const int fd, const bool server, const bool ktls,
	const char *server_name, const char *destination, TlsCounters *counters
) {

	TlsConnection *connection = (TlsConnection *) calloc (1, sizeof (TlsConnection));
	if (connection) {
		(void) pthread_mutex_init (&connection->mutex, NULL);

		connection->fd = fd;
		connection->server = server;
		connection->ktls_allowed = ktls;
		connection->counters = counters;

		// a client never reads early data; a cerver without it ends its early step at once
		connection->early_ended = !server;

		bool created = tls_buffer_init (&connection->input, TLS_INPUT_SIZE)
			&& tls_buffer_init (&connection->output, TLS_OUTPUT_INITIAL_SIZE);

		if (created) {
			connection->session = cerver_crypto_provider_tls_session_create (
				context, connection, server_name, destination
			);

			created = (connection->session != NULL);
		}

		if (!created) {
			tls_connection_delete (connection);
			connection = NULL;
		}
	}

	return connection;

}

void tls_connection_delete (void *connection_ptr) {

	TlsConnection *connection = (TlsConnection *) connection_ptr;
	if (connection) {
		cerver_crypto_provider_tls_session_delete (connection->session);

		tls_buffer_end (&connection->input);
		tls_buffer_end (&connection->output);
		tls_buffer_end (&connection->early);

		tls_certificate_delete (connection->peer);

		(void) pthread_mutex_destroy (&connection->mutex);

		free (connection);
	}

}

int tls_connection_fd (TlsConnection *connection) {

	return __atomic_load_n (&connection->fd, __ATOMIC_ACQUIRE);

}

void tls_connection_detach (TlsConnection *connection) {

	(void) pthread_mutex_lock (&connection->mutex);
	__atomic_store_n (&connection->fd, -1, __ATOMIC_RELEASE);
	(void) pthread_mutex_unlock (&connection->mutex);

}

#pragma endregion

#pragma region wait

// the milliseconds that a poll () can wait until the deadline, forever without one
static int tls_connection_wait_remaining (const struct timespec *deadline) {

	return deadline ? socket_deadline_remaining (deadline) : TLS_POLL_FOREVER;

}

// waits until the socket is ready for the events or the deadline expires, returns true when it is ready
static bool tls_connection_wait (const int fd, const short events, const struct timespec *deadline) {

	bool ready = false;
	bool waiting = true;

	while (waiting) {
		const int remaining = tls_connection_wait_remaining (deadline);
		if (!remaining) break;

		struct pollfd poll_fd = { .fd = fd, .events = events, .revents = 0 };
		const int result = poll (&poll_fd, 1, remaining);

		if (result > 0) {
			ready = true;
			waiting = false;
		}

		else if (!result || (errno != EINTR)) {
			waiting = false;
		}
	}

	return ready;

}

#pragma endregion

#pragma region output

// sends what waits in the output once, without waiting; the mutex is held
// returns the bytes that were sent, -1 with errno
static ssize_t tls_connection_output_send_once (TlsConnection *connection) {

	ssize_t sent = 0;

	const size_t used = tls_buffer_used (&connection->output);
	if (used) {
		do {
			sent = send (
				connection->fd, connection->output.bytes + connection->output.start, used,
				MSG_DONTWAIT | MSG_NOSIGNAL
			);
		} while ((sent < 0) && (errno == EINTR));

		if (sent > 0) {
			connection->output.start += (size_t) sent;
			connection->output_sent += (u64) sent;

			if (connection->output.start == connection->output.end) {
				connection->output.start = 0;
				connection->output.end = 0;
			}
		}
	}

	return sent;

}

static bool tls_connection_would_block (const int error) {

	return (error == EAGAIN) || (error == EWOULDBLOCK);

}

// sends the output until the bytes before target were sent, waiting for the socket until the deadline
// returns 0 on success, 1 with the errno in error
static u8 tls_connection_flush_until (
	TlsConnection *connection, const u64 target, const struct timespec *deadline, int *error
) {

	u8 retval = 1;
	int failure = 0;
	bool sending = true;

	while (sending) {
		(void) pthread_mutex_lock (&connection->mutex);

		const bool done = (connection->output_sent >= target);
		const ssize_t sent = done ? 0 : tls_connection_output_send_once (connection);
		failure = (sent < 0) ? errno : 0;

		(void) pthread_mutex_unlock (&connection->mutex);

		if (done) {
			retval = 0;
			sending = false;
		}

		else if (sent < 0) {
			if (!tls_connection_would_block (failure) || !tls_connection_wait (tls_connection_fd (connection), POLLOUT, deadline)) {
				if (tls_connection_would_block (failure)) failure = EAGAIN;
				sending = false;
			}
		}
	}

	if (error) *error = failure;

	return retval;

}

ssize_t tls_connection_flush (TlsConnection *connection) {

	(void) pthread_mutex_lock (&connection->mutex);

	const ssize_t sent = tls_connection_output_send_once (connection);
	const int failure = errno;
	const ssize_t waiting = (ssize_t) tls_buffer_used (&connection->output);

	(void) pthread_mutex_unlock (&connection->mutex);

	ssize_t retval = waiting;
	if ((sent < 0) && !tls_connection_would_block (failure)) {
		errno = failure;
		retval = -1;
	}

	return retval;

}

size_t tls_connection_output_waiting (TlsConnection *connection) {

	(void) pthread_mutex_lock (&connection->mutex);
	const size_t waiting = tls_buffer_used (&connection->output);
	(void) pthread_mutex_unlock (&connection->mutex);

	return waiting;

}

#pragma endregion

#pragma region provider

size_t tls_connection_input_take (
	TlsConnection *connection, void *buffer, const size_t size
) {

	return tls_buffer_take (&connection->input, buffer, size);

}

bool tls_connection_output_append (
	TlsConnection *connection, const void *data, const size_t size
) {

	const bool appended = tls_buffer_append (&connection->output, data, size);
	if (appended) connection->output_appended += (u64) size;

	return appended;

}

// the bytes of the key of a cipher, in the form of the kernel
static size_t tls_connection_ktls_info_size (const struct tls_crypto_info *info) {

	size_t size = 0;

	switch (info->cipher_type) {
		case TLS_CIPHER_AES_GCM_128: size = sizeof (struct tls12_crypto_info_aes_gcm_128); break;
		case TLS_CIPHER_AES_GCM_256: size = sizeof (struct tls12_crypto_info_aes_gcm_256); break;
		case TLS_CIPHER_CHACHA20_POLY1305: size = sizeof (struct tls12_crypto_info_chacha20_poly1305); break;
		default: break;
	}

	return size;

}

// a socket that did not take what waits: a thread waits for it until the deadline,
// the event loop that advances the handshake never does
static bool tls_connection_ktls_drain_wait (
	const TlsConnection *connection, const struct timespec *deadline
) {

	return !connection->advancing
		&& tls_connection_would_block (errno)
		&& tls_connection_wait (connection->fd, POLLOUT, deadline);

}

// what was encrypted before the key goes to the kernel is sent first, with the mutex held
static bool tls_connection_ktls_drain (TlsConnection *connection) {

	struct timespec close_deadline;
	socket_deadline_create_milliseconds (&close_deadline, TLS_CLOSE_TIMEOUT_MILLISECONDS);
	const struct timespec *deadline = connection->deadline ? connection->deadline : &close_deadline;

	bool drained = true;
	while (drained && tls_buffer_used (&connection->output)) {
		const ssize_t sent = tls_connection_output_send_once (connection);
		if ((sent < 0) && !tls_connection_ktls_drain_wait (connection, deadline)) {
			drained = false;
		}
	}

	return drained;

}

bool tls_connection_ktls_start (
	TlsConnection *connection, const void *crypto_info, const bool transmit
) {

	bool retval = false;

	// what is received stays in the provider: with kTLS the kernel would give the records without their headers
	const struct tls_crypto_info *info = (const struct tls_crypto_info *) crypto_info;
	const size_t size = info ? tls_connection_ktls_info_size (info) : 0;

	if (transmit && connection->ktls_allowed && size && tls_connection_ktls_drain (connection)) {
		if (!connection->ktls_send) {
			(void) setsockopt (connection->fd, SOL_TCP, TCP_ULP, TLS_KERNEL_ULP_NAME, sizeof (TLS_KERNEL_ULP_NAME));
		}

		if (!setsockopt (connection->fd, SOL_TLS, TLS_TX, info, (socklen_t) size)) {
			if (!connection->ktls_send && connection->counters) {
				tls_counter_add (&connection->counters->stats.ktls_send, 1);
			}

			connection->ktls_send = true;
			retval = true;
		}
	}

	return retval;

}

// a record of a type with kTLS: its type goes in a control message
static ssize_t tls_connection_ktls_send_record (
	TlsConnection *connection, const int record_type, const void *data, const size_t size
) {

	char control[CMSG_SPACE (sizeof (unsigned char))];
	(void) memset (control, 0, sizeof (control));

	struct iovec part = { (void *) data, size };

	struct msghdr message;
	(void) memset (&message, 0, sizeof (struct msghdr));
	message.msg_iov = &part;
	message.msg_iovlen = 1;
	message.msg_control = control;
	message.msg_controllen = sizeof (control);

	struct cmsghdr *header = CMSG_FIRSTHDR (&message);
	header->cmsg_level = SOL_TLS;
	header->cmsg_type = TLS_SET_RECORD_TYPE;
	header->cmsg_len = CMSG_LEN (sizeof (unsigned char));
	*CMSG_DATA (header) = (unsigned char) record_type;

	return sendmsg (connection->fd, &message, MSG_NOSIGNAL);

}

ssize_t tls_connection_ktls_send (
	TlsConnection *connection, const int record_type, const void *data, const size_t size
) {

	struct timespec deadline;
	socket_deadline_create_milliseconds (&deadline, TLS_CLOSE_TIMEOUT_MILLISECONDS);

	ssize_t sent = -1;
	bool sending = true;
	while (sending) {
		sent = record_type ?
			tls_connection_ktls_send_record (connection, record_type, data, size) :
			send (connection->fd, data, size, MSG_NOSIGNAL);

		sending = (sent < 0) && ((errno == EINTR) || (
			tls_connection_would_block (errno) && tls_connection_wait (connection->fd, POLLOUT, &deadline)
		));
	}

	return sent;

}

#pragma endregion

#pragma region handshake

// counts a failure and returns it
static TlsFailure tls_connection_failed (TlsConnection *connection, const TlsFailure failure) {

	connection->failed = true;

	if (connection->counters) tls_counter_add (&connection->counters->stats.failures[failure], 1);

	return failure;

}

// receives what came, without waiting; returns the bytes, 0 when the peer closed, -1 with errno
static ssize_t tls_connection_receive_records (TlsConnection *connection, const int flags) {

	ssize_t received = -1;

	(void) pthread_mutex_lock (&connection->mutex);
	tls_buffer_compact (&connection->input);
	size_t room = connection->input.capacity - connection->input.end;
	(void) pthread_mutex_unlock (&connection->mutex);

	if (room > sizeof (tls_connection_receive_scratch)) room = sizeof (tls_connection_receive_scratch);

	do {
		received = recv (tls_connection_fd (connection), tls_connection_receive_scratch, room, flags);
	} while ((received < 0) && (errno == EINTR));

	if (received > 0) {
		(void) pthread_mutex_lock (&connection->mutex);
		(void) tls_buffer_append (&connection->input, tls_connection_receive_scratch, (size_t) received);
		(void) pthread_mutex_unlock (&connection->mutex);
	}

	return received;

}

// the early data of a cerver, while the client sends it; returns true when it ended
static bool tls_connection_handshake_early (TlsConnection *connection, TlsFailure *failure) {

	bool ended = false;

	size_t read = 0;
	const CryptoTlsResult result = cerver_crypto_provider_tls_session_read_early (
		connection->session, tls_connection_receive_scratch, sizeof (tls_connection_receive_scratch), &read
	);

	if (read && !tls_buffer_append (&connection->early, tls_connection_receive_scratch, read)) {
		*failure = TLS_FAILURE_MEMORY;
		ended = true;
	}

	else if (result == CRYPTO_TLS_RESULT_FAILED) {
		*failure = TLS_FAILURE_PROTOCOL;
		ended = true;
	}

	else if (result == CRYPTO_TLS_RESULT_DONE) {
		ended = true;
	}

	return ended;

}

// one step of the handshake with the mutex held
static CryptoTlsResult tls_connection_handshake_step (
	TlsConnection *connection, TlsFailure *failure
) {

	CryptoTlsResult result = CRYPTO_TLS_RESULT_WANT_READ;

	(void) pthread_mutex_lock (&connection->mutex);

	if (!connection->early_ended) {
		connection->early_ended = tls_connection_handshake_early (connection, failure);
		if (*failure != TLS_FAILURE_NONE) result = CRYPTO_TLS_RESULT_FAILED;
	}

	if (connection->early_ended && (*failure == TLS_FAILURE_NONE)) {
		result = cerver_crypto_provider_tls_session_handshake (connection->session, failure);
	}

	(void) pthread_mutex_unlock (&connection->mutex);

	return result;

}

// the handshake ended: the counters and what the session tells
static TlsFailure tls_connection_established (TlsConnection *connection) {

	connection->established = true;
	connection->early_data = cerver_crypto_provider_tls_session_early_data_accepted (connection->session)
		&& tls_buffer_used (&connection->early);

	if (connection->counters) {
		tls_counter_add (&connection->counters->stats.handshakes, 1);

		if (cerver_crypto_provider_tls_session_resumed (connection->session)) {
			tls_counter_add (&connection->counters->stats.resumed, 1);
		}
	}

	return TLS_FAILURE_NONE;

}

// waits for more records of the handshake, returns TLS_FAILURE_NONE when some arrived
static TlsFailure tls_connection_handshake_receive (
	TlsConnection *connection, const struct timespec *deadline
) {

	TlsFailure failure = TLS_FAILURE_NONE;

	// a record that already arrived (more early data) is taken without waiting
	(void) pthread_mutex_lock (&connection->mutex);
	const bool arrived = tls_buffer_has_record (&connection->input);
	(void) pthread_mutex_unlock (&connection->mutex);

	if (!arrived && !tls_connection_wait (tls_connection_fd (connection), POLLIN, deadline)) {
		failure = TLS_FAILURE_TIMEOUT;
	}

	else if (!arrived) {
		const ssize_t received = tls_connection_receive_records (connection, MSG_DONTWAIT);

		if (!received) failure = TLS_FAILURE_CLOSED;
		else if ((received < 0) && !tls_connection_would_block (errno)) failure = TLS_FAILURE_SYSTEM;
	}

	return failure;

}

TlsFailure tls_connection_handshake (
	TlsConnection *connection, const unsigned int timeout_milliseconds
) {

	struct timespec deadline;
	socket_deadline_create_milliseconds (&deadline, timeout_milliseconds);
	connection->deadline = &deadline;

	TlsFailure failure = TLS_FAILURE_NONE;
	bool running = true;
	while (running) {
		const CryptoTlsResult result = tls_connection_handshake_step (connection, &failure);

		// every flight is sent before waiting for the next one
		int error = 0;
		if (tls_connection_flush_until (connection, connection->output_appended, &deadline, &error)) {
			if (failure == TLS_FAILURE_NONE) failure = (error == EAGAIN) ? TLS_FAILURE_TIMEOUT : TLS_FAILURE_SYSTEM;
		}

		if (failure != TLS_FAILURE_NONE) {
			running = false;
		}

		else if (result == CRYPTO_TLS_RESULT_DONE) {
			failure = tls_connection_established (connection);
			running = false;
		}

		else if (result == CRYPTO_TLS_RESULT_CLOSED) {
			failure = TLS_FAILURE_CLOSED;
			running = false;
		}

		else if (result == CRYPTO_TLS_RESULT_FAILED) {
			failure = TLS_FAILURE_PROTOCOL;
			running = false;
		}

		else {
			failure = tls_connection_handshake_receive (connection, &deadline);
			running = (failure == TLS_FAILURE_NONE);
		}
	}

	connection->deadline = NULL;

	if (failure != TLS_FAILURE_NONE) (void) tls_connection_failed (connection, failure);

	return failure;

}

// the handshake ended: what it wrote last (the Finished of a cerver of TLS 1.2, the tickets) can still wait
static TlsHandshakeStep tls_connection_handshake_done (TlsConnection *connection, const ssize_t waiting) {

	if (!connection->established) (void) tls_connection_established (connection);

	return waiting ? TLS_HANDSHAKE_STEP_WRITE : TLS_HANDSHAKE_STEP_DONE;

}

// what a step of the handshake that never waits needs next, once what it wrote was sent once
static TlsHandshakeStep tls_connection_handshake_next (
	TlsConnection *connection, const CryptoTlsResult result, const ssize_t waiting, TlsFailure *failure
) {

	TlsHandshakeStep step = TLS_HANDSHAKE_STEP_FAILED;

	if (*failure != TLS_FAILURE_NONE) step = TLS_HANDSHAKE_STEP_FAILED;
	else if (waiting < 0) *failure = TLS_FAILURE_SYSTEM;
	else if (result == CRYPTO_TLS_RESULT_DONE) step = tls_connection_handshake_done (connection, waiting);
	else if (result == CRYPTO_TLS_RESULT_CLOSED) *failure = TLS_FAILURE_CLOSED;
	else if (result == CRYPTO_TLS_RESULT_FAILED) *failure = TLS_FAILURE_PROTOCOL;
	else step = waiting ? TLS_HANDSHAKE_STEP_WRITE : TLS_HANDSHAKE_STEP_READ;

	return step;

}

// takes the records of the peer that already came, without waiting for them
// returns true when the handshake can advance with them
static bool tls_connection_handshake_take (TlsConnection *connection, TlsFailure *failure) {

	(void) pthread_mutex_lock (&connection->mutex);
	const bool arrived = tls_buffer_has_record (&connection->input);
	(void) pthread_mutex_unlock (&connection->mutex);

	bool advances = arrived;

	if (!arrived) {
		const ssize_t received = tls_connection_receive_records (connection, MSG_DONTWAIT);
		advances = (received > 0);

		if (!received) *failure = TLS_FAILURE_CLOSED;
		else if ((received < 0) && !tls_connection_would_block (errno)) *failure = TLS_FAILURE_SYSTEM;
	}

	return advances;

}

TlsHandshakeStep tls_connection_handshake_advance (TlsConnection *connection) {

	TlsFailure failure = TLS_FAILURE_NONE;
	TlsHandshakeStep step = TLS_HANDSHAKE_STEP_READ;

	// what the provider asks meanwhile (the key of kTLS) does not wait for the socket either
	connection->advancing = true;

	bool advancing = true;
	while (advancing) {
		const CryptoTlsResult result = connection->established ?
			CRYPTO_TLS_RESULT_DONE : tls_connection_handshake_step (connection, &failure);

		// every flight is sent before the next one is read
		const ssize_t waiting = tls_connection_flush (connection);

		step = tls_connection_handshake_next (connection, result, waiting, &failure);

		// the records that came are read until the socket has none: its poller would not report them again
		advancing = (step == TLS_HANDSHAKE_STEP_READ) && tls_connection_handshake_take (connection, &failure);
	}

	connection->advancing = false;

	if (failure != TLS_FAILURE_NONE) {
		(void) tls_connection_failed (connection, failure);
		step = TLS_HANDSHAKE_STEP_FAILED;
	}

	return step;

}

void tls_connection_handshake_expired (TlsConnection *connection) {

	(void) pthread_mutex_lock (&connection->mutex);
	(void) tls_connection_failed (connection, TLS_FAILURE_TIMEOUT);
	(void) pthread_mutex_unlock (&connection->mutex);

}

#pragma endregion

#pragma region receive

size_t tls_connection_take_early (
	TlsConnection *connection, void *buffer, const size_t size
) {

	(void) pthread_mutex_lock (&connection->mutex);
	const size_t taken = tls_buffer_take (&connection->early, buffer, size);
	(void) pthread_mutex_unlock (&connection->mutex);

	return taken;

}

// one decryption with the mutex held; what the provider wrote while it read (an alert, a ticket, a key update)
// is sent if the socket takes it now, the rest waits for the next send
static CryptoTlsResult tls_connection_receive_step (
	TlsConnection *connection, void *buffer, const size_t size, size_t *read
) {

	CryptoTlsResult result = CRYPTO_TLS_RESULT_FAILED;

	(void) pthread_mutex_lock (&connection->mutex);

	*read = tls_buffer_take (&connection->early, buffer, size);
	if (*read) {
		result = CRYPTO_TLS_RESULT_DONE;
	}

	else if (connection->received_close) {
		result = CRYPTO_TLS_RESULT_CLOSED;
	}

	else if (!connection->failed) {
		result = cerver_crypto_provider_tls_session_read (connection->session, buffer, size, read);

		if (result == CRYPTO_TLS_RESULT_CLOSED) connection->received_close = true;
		else if (result == CRYPTO_TLS_RESULT_FAILED) connection->failed = true;

		(void) tls_connection_output_send_once (connection);
	}

	(void) pthread_mutex_unlock (&connection->mutex);

	return result;

}

ssize_t tls_connection_receive (
	TlsConnection *connection, void *buffer, const size_t size, const int flags
) {

	ssize_t retval = -1;
	bool receiving = true;

	while (receiving) {
		size_t read = 0;
		const CryptoTlsResult result = tls_connection_receive_step (connection, buffer, size, &read);

		if (result == CRYPTO_TLS_RESULT_DONE) {
			retval = (ssize_t) read;
			receiving = false;
		}

		else if (result == CRYPTO_TLS_RESULT_CLOSED) {
			retval = 0;
			receiving = false;
		}

		else if (result == CRYPTO_TLS_RESULT_FAILED) {
			errno = ECONNRESET;
			receiving = false;
		}

		else {
			const ssize_t received = tls_connection_receive_records (connection, flags & MSG_DONTWAIT);

			// a close without close_notify ends the stream as a close of the socket
			if (!received) {
				(void) pthread_mutex_lock (&connection->mutex);
				connection->received_close = true;
				(void) pthread_mutex_unlock (&connection->mutex);

				retval = 0;
				receiving = false;
			}

			else if (received < 0) {
				receiving = false;
			}
		}
	}

	return retval;

}

bool tls_connection_pending (TlsConnection *connection) {

	(void) pthread_mutex_lock (&connection->mutex);

	const bool pending = tls_buffer_used (&connection->early)
		|| connection->received_close
		|| cerver_crypto_provider_tls_session_pending (connection->session)
		|| tls_buffer_has_record (&connection->input);

	(void) pthread_mutex_unlock (&connection->mutex);

	return pending;

}

#pragma endregion

#pragma region send

// encrypts one piece with the mutex held, or keeps it as it is for the kernel with kTLS
static bool tls_connection_encrypt_locked (TlsConnection *connection, const void *data, const size_t size) {

	bool retval = false;

	if (!connection->failed && !connection->sent_close) {
		retval = connection->ktls_send ?
			tls_connection_output_append (connection, data, size) :
			(cerver_crypto_provider_tls_session_write (connection->session, data, size) == CRYPTO_TLS_RESULT_DONE);

		if (!retval) connection->failed = true;
	}

	return retval;

}

static bool tls_connection_encrypt (TlsConnection *connection, const void *data, const size_t size) {

	(void) pthread_mutex_lock (&connection->mutex);
	const bool retval = tls_connection_encrypt_locked (connection, data, size);
	(void) pthread_mutex_unlock (&connection->mutex);

	return retval;

}

// the encrypted bytes that wait over the mark are sent before more are encrypted
static u8 tls_connection_flush_over_mark (
	TlsConnection *connection, const struct timespec *deadline, int *error
) {

	u8 retval = 0;

	(void) pthread_mutex_lock (&connection->mutex);
	const size_t waiting = tls_buffer_used (&connection->output);
	const u64 target = connection->output_sent + ((waiting > TLS_OUTPUT_FLUSH_MARK) ? (waiting - TLS_OUTPUT_FLUSH_MARK) : 0);
	(void) pthread_mutex_unlock (&connection->mutex);

	if (waiting > TLS_OUTPUT_FLUSH_MARK) retval = tls_connection_flush_until (connection, target, deadline, error);

	return retval;

}

// the state of a send: the scratch where the small parts are joined in one record
typedef struct TlsSend {

	TlsConnection *connection;
	const struct timespec *deadline;
	size_t joined;
	size_t taken;
	bool blocking;
	int error;
	bool failed;

} TlsSend;

static void tls_send_join_flush (TlsSend *send) {

	if (send->joined && !send->failed) {
		send->failed = !tls_connection_encrypt (send->connection, tls_connection_send_scratch, send->joined);
		send->joined = 0;

		if (send->failed) send->error = EPIPE;
		else if (send->blocking && tls_connection_flush_over_mark (send->connection, send->deadline, &send->error)) {
			send->failed = true;
		}
	}

}

// a part is joined to the scratch, or encrypted from where it is when it fills whole records
static void tls_send_part (TlsSend *send, const u8 *data, size_t size) {

	while (size && !send->failed) {
		if (!send->joined && (size >= TLS_RECORD_PLAINTEXT_SIZE)) {
			send->failed = !tls_connection_encrypt (send->connection, data, TLS_RECORD_PLAINTEXT_SIZE);
			if (send->failed) send->error = EPIPE;
			else if (send->blocking && tls_connection_flush_over_mark (send->connection, send->deadline, &send->error)) {
				send->failed = true;
			}

			data += TLS_RECORD_PLAINTEXT_SIZE;
			size -= TLS_RECORD_PLAINTEXT_SIZE;
			if (!send->failed) send->taken += TLS_RECORD_PLAINTEXT_SIZE;
		}

		else {
			const size_t room = TLS_RECORD_PLAINTEXT_SIZE - send->joined;
			const size_t piece = (size < room) ? size : room;

			(void) memcpy (tls_connection_send_scratch + send->joined, data, piece);
			send->joined += piece;
			send->taken += piece;

			data += piece;
			size -= piece;

			if (send->joined == TLS_RECORD_PLAINTEXT_SIZE) tls_send_join_flush (send);
		}
	}

}

static void tls_send_parts (TlsSend *send, const struct iovec *parts, const int n_parts) {

	for (int index = 0; (index < n_parts) && !send->failed; index++) {
		tls_send_part (send, (const u8 *) parts[index].iov_base, parts[index].iov_len);
	}

	tls_send_join_flush (send);

}

u8 tls_connection_send_vector_until (
	TlsConnection *connection, const struct iovec *parts, const int n_parts,
	const struct timespec *deadline, size_t *sent, int *error
) {

	TlsSend send = { connection, deadline, 0, 0, true, 0, false };
	tls_send_parts (&send, parts, n_parts);

	if (!send.failed) {
		(void) pthread_mutex_lock (&connection->mutex);
		const u64 target = connection->output_appended;
		(void) pthread_mutex_unlock (&connection->mutex);

		send.failed = (tls_connection_flush_until (connection, target, deadline, &send.error) != 0);
	}

	if (sent) *sent = send.failed ? 0 : send.taken;
	if (error) *error = send.error;

	return send.failed ? 1 : 0;

}

ssize_t tls_connection_write (
	TlsConnection *connection, const struct iovec *parts, const int n_parts
) {

	TlsSend send = { connection, NULL, 0, 0, false, 0, false };
	tls_send_parts (&send, parts, n_parts);

	ssize_t retval = (ssize_t) send.taken;
	if (send.failed || (tls_connection_flush (connection) < 0)) {
		errno = send.failed ? send.error : errno;
		retval = -1;
	}

	return retval;

}

// with kTLS the kernel encrypts the file: what waits is sent first, and then sendfile () without copies
static u8 tls_connection_send_file_ktls (
	TlsConnection *connection, const int file_fd, off_t *offset, const size_t size,
	const struct timespec *deadline, size_t *sent, int *error
) {

	(void) pthread_mutex_lock (&connection->mutex);
	const u64 target = connection->output_appended;
	(void) pthread_mutex_unlock (&connection->mutex);

	u8 retval = tls_connection_flush_until (connection, target, deadline, error);
	if (!retval) {
		size_t total = 0;
		while (!retval && (total < size)) {
			if (!tls_connection_wait (tls_connection_fd (connection), POLLOUT, deadline)) {
				if (error) *error = EAGAIN;
				retval = 1;
			}

			else {
				(void) pthread_mutex_lock (&connection->mutex);
				const ssize_t result = sendfile (connection->fd, file_fd, offset, size - total);
				const int failure = errno;
				(void) pthread_mutex_unlock (&connection->mutex);

				if (result > 0) total += (size_t) result;
				else if (!result || ((failure != EINTR) && !tls_connection_would_block (failure))) {
					if (error) *error = result ? failure : 0;
					retval = 1;
				}
			}
		}

		if (sent) *sent = total;
	}

	return retval;

}

// reads a piece of the file, from the offset when there is one
static ssize_t tls_connection_file_read (const int file_fd, off_t *offset, const size_t size) {

	ssize_t result = -1;

	do {
		result = offset ?
			pread (file_fd, tls_connection_file_scratch, size, *offset) :
			read (file_fd, tls_connection_file_scratch, size);
	} while ((result < 0) && (errno == EINTR));

	if ((result > 0) && offset) *offset += (off_t) result;

	return result;

}

u8 tls_connection_send_file_until (
	TlsConnection *connection, const int file_fd, off_t *offset, const size_t size,
	const struct timespec *deadline, size_t *sent, int *error
) {

	u8 retval = 0;

	if (connection->ktls_send) {
		retval = tls_connection_send_file_ktls (connection, file_fd, offset, size, deadline, sent, error);
	}

	else {
		size_t total = 0;
		int failure = 0;

		while (!retval && (total < size)) {
			const size_t piece = ((size - total) < TLS_FILE_BUFFER_SIZE) ? (size - total) : TLS_FILE_BUFFER_SIZE;
			const ssize_t read = tls_connection_file_read (file_fd, offset, piece);

			if (read <= 0) {
				failure = read ? errno : 0;
				retval = 1;
			}

			else {
				struct iovec part = { tls_connection_file_scratch, (size_t) read };
				size_t taken = 0;
				retval = tls_connection_send_vector_until (connection, &part, 1, deadline, &taken, &failure);
				total += taken;
			}
		}

		if (sent) *sent = total;
		if (error) *error = failure;
	}

	return retval;

}

#pragma endregion

#pragma region close

void tls_connection_close (TlsConnection *connection) {

	struct timespec deadline;
	socket_deadline_create_milliseconds (&deadline, TLS_CLOSE_TIMEOUT_MILLISECONDS);

	(void) pthread_mutex_lock (&connection->mutex);
	const bool closing = connection->established && !connection->sent_close && !connection->failed;
	if (closing) connection->sent_close = true;
	u64 target = connection->output_appended;
	(void) pthread_mutex_unlock (&connection->mutex);

	if (closing) {
		// with kTLS the data that waits goes before the alert, that the provider sends at once
		int error = 0;
		if (connection->ktls_send) (void) tls_connection_flush_until (connection, target, &deadline, &error);

		(void) pthread_mutex_lock (&connection->mutex);
		cerver_crypto_provider_tls_session_close (connection->session);
		target = connection->output_appended;
		(void) pthread_mutex_unlock (&connection->mutex);

		(void) tls_connection_flush_until (connection, target, &deadline, &error);
	}

}

// the close_notify with the mutex held; with kTLS the alert leaves at once, so it only goes after the data
// that waits, and a socket that does not take that data now gets no alert
static void tls_connection_close_notify_now (TlsConnection *connection) {

	if (connection->ktls_send) (void) tls_connection_output_send_once (connection);

	if (!connection->ktls_send || !tls_buffer_used (&connection->output)) {
		cerver_crypto_provider_tls_session_close (connection->session);
	}

}

void tls_connection_close_now (TlsConnection *connection) {

	(void) pthread_mutex_lock (&connection->mutex);

	const bool closing = connection->established && !connection->sent_close && !connection->failed;
	if (closing) {
		connection->sent_close = true;

		tls_connection_close_notify_now (connection);
	}

	(void) tls_connection_output_send_once (connection);

	(void) pthread_mutex_unlock (&connection->mutex);

}

#pragma endregion

#pragma region peer

TlsCertificate *tls_connection_peer_certificate (TlsConnection *connection) {

	(void) pthread_mutex_lock (&connection->mutex);

	if (!connection->peer_checked && connection->established) {
		connection->peer = cerver_crypto_provider_tls_session_peer_certificate (connection->session);
		connection->peer_checked = true;
	}

	TlsCertificate *peer = connection->peer;

	(void) pthread_mutex_unlock (&connection->mutex);

	return peer;

}

const char *tls_connection_server_name (const TlsConnection *connection) {

	return cerver_crypto_provider_tls_session_server_name (connection->session);

}

#pragma endregion
