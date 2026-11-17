#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "cerver/network/socket.h"

#include "cerver/http/websockets/internal.h"

#include "cerver/tls/internal.h"

// the socket of the connection, NULL when its stream already ended
static Socket *http_web_socket_transport_socket (const HttpWebSocket *web_socket) {

	return web_socket->socket;

}

static int http_web_socket_transport_socket_fd (const HttpWebSocket *web_socket) {

	const Socket *socket = http_web_socket_transport_socket (web_socket);

	return socket ? socket->sock_fd : -1;

}

static ssize_t http_web_socket_transport_socket_read (
	HttpWebSocket *web_socket, void *buffer, size_t size
) {

	ssize_t received = -1;

	const int sock_fd = http_web_socket_transport_socket_fd (web_socket);
	if (sock_fd >= 0) {
		do {
			received = recv (sock_fd, buffer, size, MSG_DONTWAIT);
		} while ((received < 0) && (errno == EINTR));
	}

	else {
		errno = EBADF;
	}

	return received;

}

static ssize_t http_web_socket_transport_socket_write (
	HttpWebSocket *web_socket, const struct iovec *parts, int n_parts
) {

	ssize_t sent = -1;

	const int sock_fd = http_web_socket_transport_socket_fd (web_socket);
	if (sock_fd >= 0) {
		struct msghdr message;
		(void) memset (&message, 0, sizeof (struct msghdr));
		message.msg_iov = (struct iovec *) parts;
		message.msg_iovlen = (size_t) n_parts;

		do {
			sent = sendmsg (sock_fd, &message, MSG_NOSIGNAL | MSG_DONTWAIT);
		} while ((sent < 0) && (errno == EINTR));
	}

	else {
		errno = EBADF;
	}

	return sent;

}

static void http_web_socket_transport_socket_end_writes (HttpWebSocket *web_socket) {

	const int sock_fd = http_web_socket_transport_socket_fd (web_socket);
	if (sock_fd >= 0) (void) shutdown (sock_fd, SHUT_WR);

}

static void http_web_socket_transport_socket_shutdown (HttpWebSocket *web_socket) {

	Socket *socket = http_web_socket_transport_socket (web_socket);
	if (socket) (void) socket_shutdown (socket);

}

// a plain socket never has bytes that the fd does not show, nor bytes of its own to send
static bool http_web_socket_transport_socket_pending (const HttpWebSocket *web_socket) {

	(void) web_socket;

	return false;

}

static size_t http_web_socket_transport_socket_waiting (const HttpWebSocket *web_socket) {

	(void) web_socket;

	return 0;

}

static ssize_t http_web_socket_transport_socket_flush (HttpWebSocket *web_socket) {

	(void) web_socket;

	return 0;

}

const HttpWebSocketTransport http_web_socket_socket_transport = {
	http_web_socket_transport_socket_read,
	http_web_socket_transport_socket_write,
	http_web_socket_transport_socket_end_writes,
	http_web_socket_transport_socket_shutdown,
	http_web_socket_transport_socket_fd,
	http_web_socket_transport_socket_pending,
	http_web_socket_transport_socket_waiting,
	http_web_socket_transport_socket_flush
};

#pragma region tls

// the TLS of the socket of the connection, NULL when its stream already ended
static TlsConnection *http_web_socket_transport_tls (const HttpWebSocket *web_socket) {

	const Socket *socket = http_web_socket_transport_socket (web_socket);

	return socket ? socket->tls : NULL;

}

static ssize_t http_web_socket_transport_tls_read (
	HttpWebSocket *web_socket, void *buffer, size_t size
) {

	ssize_t received = -1;

	TlsConnection *tls = http_web_socket_transport_tls (web_socket);
	if (tls) received = tls_connection_receive (tls, buffer, size, MSG_DONTWAIT);
	else errno = EBADF;

	return received;

}

static ssize_t http_web_socket_transport_tls_write (
	HttpWebSocket *web_socket, const struct iovec *parts, int n_parts
) {

	ssize_t written = -1;

	TlsConnection *tls = http_web_socket_transport_tls (web_socket);
	if (tls) written = tls_connection_write (tls, parts, n_parts);
	else errno = EBADF;

	return written;

}

// the close_notify goes before the FIN
static void http_web_socket_transport_tls_end_writes (HttpWebSocket *web_socket) {

	socket_end_writes (http_web_socket_transport_socket (web_socket));

}

static bool http_web_socket_transport_tls_pending (const HttpWebSocket *web_socket) {

	TlsConnection *tls = http_web_socket_transport_tls (web_socket);

	return tls ? tls_connection_pending (tls) : false;

}

static size_t http_web_socket_transport_tls_waiting (const HttpWebSocket *web_socket) {

	TlsConnection *tls = http_web_socket_transport_tls (web_socket);

	return tls ? tls_connection_output_waiting (tls) : 0;

}

static ssize_t http_web_socket_transport_tls_flush (HttpWebSocket *web_socket) {

	ssize_t waiting = -1;

	TlsConnection *tls = http_web_socket_transport_tls (web_socket);
	if (tls) waiting = tls_connection_flush (tls);
	else errno = EBADF;

	return waiting;

}

const HttpWebSocketTransport http_web_socket_tls_transport = {
	http_web_socket_transport_tls_read,
	http_web_socket_transport_tls_write,
	http_web_socket_transport_tls_end_writes,
	http_web_socket_transport_socket_shutdown,
	http_web_socket_transport_socket_fd,
	http_web_socket_transport_tls_pending,
	http_web_socket_transport_tls_waiting,
	http_web_socket_transport_tls_flush
};

#pragma endregion
