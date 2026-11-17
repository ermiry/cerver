/**
 * @file tls/internal.h
 * @internal
 * @brief The settings, the connection and the state of TLS of a cerver and of a client, shared by the parts of tls/
 * and by the provider; never exported by libcerver.so.
 */

#ifndef _CERVER_TLS_INTERNAL_H_
#define _CERVER_TLS_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <pthread.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/crypto/provider.h"

#include "cerver/threads/thpool.h"

#include "cerver/tls/tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @internal @brief The most plaintext of a record: 2^14 (RFC 8446 section 5.1). */
#define TLS_RECORD_PLAINTEXT_SIZE			16384

/** @internal @brief The room of the input of a connection: two records with their overhead, 2^15 + 2^9. */
#define TLS_INPUT_SIZE						33280

/** @internal @brief The first room of the output of a connection: 2^14 + 2^9. */
#define TLS_OUTPUT_INITIAL_SIZE				16896

/** @internal @brief A sender encrypts up to these bytes before it sends them: 2^16. */
#define TLS_OUTPUT_FLUSH_MARK				65536

/** @internal @brief The bytes that a file is read in to be encrypted: 2^16. */
#define TLS_FILE_BUFFER_SIZE				65536

/** @internal @brief The milliseconds that the close_notify of an orderly close can take to be sent: 2^8. */
#define TLS_CLOSE_TIMEOUT_MILLISECONDS		256

/** @internal @brief The size of the reason of an error of a context. */
#define TLS_ERROR_SIZE						512

/** @internal @brief The size of a destination of a client: a host, `:` and a port. */
#define TLS_DESTINATION_SIZE				320

/**
 * @internal
 * @brief A certificate and its key, as their paths or URIs.
 */
typedef struct TlsCertificatePaths {

	/** @internal @brief The PEM file of the certificate and its chain. */
	char *chain;
	/** @internal @brief The PEM file of the key, or its URI in a key store. */
	char *key;

} TlsCertificatePaths;

/**
 * @internal
 * @brief The settings of TLS (see tls_settings_create ()).
 */
struct TlsSettings {

	/** @internal @brief The certificates of a cerver, the first one is the default. */
	TlsCertificatePaths *certificates;
	/** @internal @brief How many certificates. */
	size_t n_certificates;

	/** @internal @brief The certificate that a client presents, empty without one. */
	TlsCertificatePaths client_certificate;

	/** @internal @brief The name of the key store of the provider, NULL without one. */
	char *key_store_name;
	/** @internal @brief Its properties, NULL without them. */
	char *key_store_properties;

	/** @internal @brief The oldest version. */
	TlsVersion min_version;

	/** @internal @brief The ciphers of TLS 1.2 by their IANA names, NULL for the default. */
	char *ciphers12;
	/** @internal @brief The ciphers of TLS 1.3 by their IANA names, NULL for the default. */
	char *ciphers13;
	/** @internal @brief The groups, NULL for the default. */
	char *groups;
	/** @internal @brief The application protocols separated by `,`, NULL without them. */
	char *alpn;

	/** @internal @brief The PEM file of the trusted authorities, NULL for the store of the system. */
	char *ca_file;
	/** @internal @brief The folder of the trusted authorities, NULL without one. */
	char *ca_path;

	/** @internal @brief What a cerver asks of the certificates of its clients. */
	TlsClientVerify client_verify;

	/** @internal @brief The name that a client asks for, NULL for its host. */
	char *server_name;

	/** @internal @brief The session tickets. */
	bool tickets;
	/** @internal @brief The file of shared keys of the tickets, NULL for the keys of cerver. */
	char *ticket_keys_file;

	/** @internal @brief The most bytes of early data, 0 without it. */
	u32 early_data;

	/** @internal @brief The OCSP stapling. */
	bool ocsp_stapling;
	/** @internal @brief The URL of the responder, NULL for the one of the certificate. */
	char *ocsp_responder;

	/** @internal @brief kTLS for what is sent. */
	bool ktls;

	/** @internal @brief The deadline of a handshake. */
	unsigned int handshake_timeout_milliseconds;
	/** @internal @brief The threads of the handshakes of a cerver that is not a web one. */
	unsigned int handshake_threads;

};

/**
 * @internal
 * @brief The bytes that wait in a connection, from start to end.
 */
typedef struct TlsBuffer {

	/** @internal @brief The bytes. */
	u8 *bytes;
	/** @internal @brief The first byte that waits. */
	size_t start;
	/** @internal @brief One past the last byte that waits. */
	size_t end;
	/** @internal @brief The size of bytes. */
	size_t capacity;

} TlsBuffer;

/**
 * @internal
 * @brief The stats of TLS of a cerver or a client, written with __atomic.
 */
typedef struct TlsCounters {

	/** @internal @brief The values. */
	TlsStats stats;

} TlsCounters;

/**
 * @internal
 * @brief The TLS of a connection: its session and the records that wait to be decrypted or sent.
 *
 * The provider only touches the two buffers; the socket is read and written by the connection. The mutex is held to
 * use the session and the buffers, and never while waiting for the socket.
 */
struct _TlsConnection {

	/** @internal @brief Guards the session, the buffers and the flags. */
	pthread_mutex_t mutex;

	/** @internal @brief The session of the provider. */
	CryptoTlsSession *session;

	/** @internal @brief The fd of the socket. */
	int fd;
	/** @internal @brief True in a cerver. */
	bool server;

	/** @internal @brief The records that were received and not decrypted yet. */
	TlsBuffer input;
	/** @internal @brief The records that were encrypted and not sent yet. */
	TlsBuffer output;
	/** @internal @brief The bytes that were appended to the output since the connection started. */
	u64 output_appended;
	/** @internal @brief The bytes of the output that were sent since the connection started. */
	u64 output_sent;

	/** @internal @brief The early data that was read in the handshake and not taken yet. */
	TlsBuffer early;
	/** @internal @brief The client sent early data and the cerver took it. */
	bool early_data;

	/** @internal @brief The handshake ended. */
	bool established;
	/** @internal @brief The peer sent its close_notify or closed the socket. */
	bool received_close;
	/** @internal @brief The close_notify was written. */
	bool sent_close;
	/** @internal @brief The session failed, nothing more can be read or sent. */
	bool failed;
	/** @internal @brief The kernel encrypts what is sent. */
	bool ktls_send;
	/** @internal @brief kTLS was asked for in the settings. */
	bool ktls_allowed;

	/** @internal @brief The deadline of the handshake, NULL out of it. */
	const struct timespec *deadline;

	/** @internal @brief The certificate of the peer, made when it is asked for. */
	TlsCertificate *peer;
	/** @internal @brief The certificate of the peer was already asked for. */
	bool peer_checked;

	/** @internal @brief The counters to update, NULL without them. */
	TlsCounters *counters;

};

/** @internal @brief The TLS of a connection, see struct _TlsConnection. */
typedef struct _TlsConnection TlsConnection;

/**
 * @internal
 * @brief The TLS of a cerver: its settings, its current context and the threads of its handshakes and of its
 * maintenance (the reloads, OCSP).
 */
typedef struct _CerverTls {

	/** @internal @brief The settings, owned. */
	TlsSettings *settings;

	/** @internal @brief Guards the context while it is replaced. */
	pthread_mutex_t mutex;
	/** @internal @brief The context of the new connections. */
	CryptoTlsContext *context;

	/** @internal @brief The threads of the handshakes of a cerver that is not a web one. */
	Thpool *handshakes;

	/** @internal @brief The counters. */
	TlsCounters counters;

	/** @internal @brief The signal that reloads the certificates, 0 without one. */
	int reload_signal;
	/** @internal @brief The files are watched to reload them. */
	bool reload_watch;
	/** @internal @brief The milliseconds that the watch waits for the files to settle. */
	unsigned int reload_debounce_milliseconds;

	/** @internal @brief The thread of the maintenance. */
	pthread_t maintenance;
	/** @internal @brief The thread of the maintenance is running. */
	bool maintenance_running;
	/** @internal @brief The eventfd that wakes the maintenance: a reload or the stop. */
	int wake_fd;
	/** @internal @brief The maintenance must end; read and written with __atomic. */
	bool stopping;

} CerverTls;

/**
 * @internal
 * @brief The TLS of a client: its settings and its context, made with the first connection.
 */
typedef struct _ClientTls {

	/** @internal @brief The settings, owned. */
	TlsSettings *settings;

	/** @internal @brief Guards the context. */
	pthread_mutex_t mutex;
	/** @internal @brief The context, NULL until the first connection. */
	CryptoTlsContext *context;

	/** @internal @brief The counters. */
	TlsCounters counters;

} ClientTls;

#pragma region counters

/**
 * @internal
 * @brief Adds to a counter, without a lock.
 *
 * @param[in,out] counter the counter
 * @param[in] value what is added
 */
CERVER_HIDDEN void tls_counter_add (u64 *counter, const u64 value);

/**
 * @internal
 * @brief Copies the counters.
 *
 * @param[in] counters the counters
 * @param[out] stats where they are copied
 */
CERVER_HIDDEN void tls_counters_copy (const TlsCounters *counters, TlsStats *stats);

#pragma endregion

#pragma region settings

/**
 * @internal
 * @brief Returns whether the settings have a key that lives in a key store (an URI) or a key store.
 *
 * @param[in] key the key of a certificate
 * @return true for an URI like `pkcs11:`
 */
CERVER_HIDDEN bool tls_settings_key_is_uri (const char *key);

/**
 * @internal
 * @brief Splits the application protocols in the wire format of ALPN (each one with its size before it).
 *
 * @param[in] protocols the protocols separated by `,`
 * @param[out] wire where the bytes are written
 * @param[in] wire_size the size of wire
 * @return the bytes that were written, 0 on an error
 */
CERVER_HIDDEN size_t tls_settings_alpn_wire (const char *protocols, u8 *wire, const size_t wire_size);

#pragma endregion

#pragma region certificate

/**
 * @internal
 * @brief Deletes a certificate made by the provider.
 *
 * @param[in] certificate_ptr the certificate, it can be NULL
 */
CERVER_HIDDEN void tls_certificate_delete (void *certificate_ptr);

#pragma endregion

#pragma region connection

/**
 * @internal
 * @brief Makes the TLS of a connection over its fd.
 *
 * @param[in] context the context, the connection takes a reference
 * @param[in] fd the fd of the socket
 * @param[in] server true in a cerver
 * @param[in] ktls true when kTLS was asked for
 * @param[in] server_name the name that a client asks for, NULL in a cerver
 * @param[in] destination the host and port of a client, NULL in a cerver
 * @param[in] counters the counters to update, it can be NULL
 * @return the connection, NULL without memory
 */
CERVER_HIDDEN TlsConnection *tls_connection_create (
	CryptoTlsContext *context, const int fd, const bool server, const bool ktls,
	const char *server_name, const char *destination, TlsCounters *counters
);

/**
 * @internal
 * @brief Deletes the TLS of a connection, without its close_notify.
 *
 * @param[in] connection_ptr the connection, it can be NULL
 */
CERVER_HIDDEN void tls_connection_delete (void *connection_ptr);

/**
 * @internal
 * @brief Returns the fd of the connection, -1 once its socket was closed.
 *
 * @param[in] connection the connection
 * @return the fd
 */
CERVER_HIDDEN int tls_connection_fd (TlsConnection *connection);

/**
 * @internal
 * @brief Forgets the fd of a socket that was closed, so a thread that still has the connection never reaches the fd
 * that another connection takes.
 *
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void tls_connection_detach (TlsConnection *connection);

/**
 * @internal
 * @brief Runs the handshake, waiting for the socket with poll () until the deadline.
 *
 * @param[in,out] connection the connection
 * @param[in] timeout_milliseconds the deadline of the handshake
 * @return TLS_FAILURE_NONE when it ended, the reason otherwise (counted in the counters)
 */
CERVER_HIDDEN TlsFailure tls_connection_handshake (
	TlsConnection *connection, const unsigned int timeout_milliseconds
);

/**
 * @internal
 * @brief Receives and decrypts, like recv ().
 *
 * @param[in,out] connection the connection
 * @param[out] buffer where the bytes are written
 * @param[in] size the size of the buffer
 * @param[in] flags MSG_DONTWAIT for a read that does not wait; MSG_PEEK is not supported
 * @return the bytes, 0 after the close of the peer, -1 with errno (EAGAIN when nothing came without waiting)
 */
CERVER_HIDDEN ssize_t tls_connection_receive (
	TlsConnection *connection, void *buffer, const size_t size, const int flags
);

/**
 * @internal
 * @brief Returns whether a read gives bytes without waiting for the socket: decrypted bytes, early data, or
 * records that already arrived.
 *
 * @param[in] connection the connection
 * @return true when the poll () of the socket must not be waited
 */
CERVER_HIDDEN bool tls_connection_pending (TlsConnection *connection);

/**
 * @internal
 * @brief Encrypts and sends the parts, waiting with poll (POLLOUT) until the deadline.
 *
 * @param[in,out] connection the connection
 * @param[in] parts the parts
 * @param[in] n_parts how many
 * @param[in] deadline the deadline, NULL to wait forever
 * @param[out] sent the plaintext bytes that were taken, it can be NULL
 * @param[out] error the errno of a failure, it can be NULL
 * @retval 0 everything was sent
 * @retval 1 the deadline passed (EAGAIN) or the socket failed
 */
CERVER_HIDDEN u8 tls_connection_send_vector_until (
	TlsConnection *connection, const struct iovec *parts, const int n_parts,
	const struct timespec *deadline, size_t *sent, int *error
);

/**
 * @internal
 * @brief Encrypts the parts into the output and sends what the socket takes now, without waiting.
 *
 * @param[in,out] connection the connection
 * @param[in] parts the parts
 * @param[in] n_parts how many
 * @return the plaintext bytes that were taken (all of them), -1 with errno on an error
 */
CERVER_HIDDEN ssize_t tls_connection_write (
	TlsConnection *connection, const struct iovec *parts, const int n_parts
);

/**
 * @internal
 * @brief Sends what waits in the output without waiting.
 *
 * @param[in,out] connection the connection
 * @return the bytes that still wait, -1 with errno on an error of the socket
 */
CERVER_HIDDEN ssize_t tls_connection_flush (TlsConnection *connection);

/**
 * @internal
 * @brief Returns the bytes that wait in the output.
 *
 * @param[in] connection the connection
 * @return the bytes
 */
CERVER_HIDDEN size_t tls_connection_output_waiting (TlsConnection *connection);

/**
 * @internal
 * @brief Sends a part of a file: with kTLS with sendfile (), otherwise read and encrypted.
 *
 * @param[in,out] connection the connection
 * @param[in] file_fd the file
 * @param[in,out] offset where it starts, advanced by what was sent; NULL for the offset of the file
 * @param[in] size the bytes
 * @param[in] deadline the deadline, NULL to wait forever
 * @param[out] sent the bytes that were sent, it can be NULL
 * @param[out] error the errno of a failure, it can be NULL
 * @retval 0 everything was sent
 * @retval 1 the deadline passed or the socket or the file failed
 */
CERVER_HIDDEN u8 tls_connection_send_file_until (
	TlsConnection *connection, const int file_fd, off_t *offset, const size_t size,
	const struct timespec *deadline, size_t *sent, int *error
);

/**
 * @internal
 * @brief Writes the close_notify once and tries to send it for #TLS_CLOSE_TIMEOUT_MILLISECONDS.
 *
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void tls_connection_close (TlsConnection *connection);

/**
 * @internal
 * @brief Returns the certificate of the peer, made the first time.
 *
 * @param[in,out] connection the connection
 * @return the certificate, owned by the connection; NULL without one
 */
CERVER_HIDDEN TlsCertificate *tls_connection_peer_certificate (TlsConnection *connection);

/**
 * @internal
 * @brief Returns the name that the client asked for.
 *
 * @param[in] connection the connection of a cerver
 * @return the name, NULL without SNI
 */
CERVER_HIDDEN const char *tls_connection_server_name (const TlsConnection *connection);

/**
 * @internal
 * @brief Takes the early data that the handshake read, before any receive.
 *
 * @param[in,out] connection the connection
 * @param[out] buffer where the bytes are written
 * @param[in] size the size of the buffer
 * @return the bytes, 0 without early data left
 */
CERVER_HIDDEN size_t tls_connection_take_early (
	TlsConnection *connection, void *buffer, const size_t size
);

/**
 * @internal
 * @brief The provider reads records from the input (the read of its BIO).
 *
 * @param[in,out] connection the connection
 * @param[out] buffer where the bytes are written
 * @param[in] size the size of the buffer
 * @return the bytes, 0 when the input is empty
 */
CERVER_HIDDEN size_t tls_connection_input_take (
	TlsConnection *connection, void *buffer, const size_t size
);

/**
 * @internal
 * @brief The provider appends records to the output (the write of its BIO).
 *
 * @param[in,out] connection the connection
 * @param[in] data the bytes
 * @param[in] size how many
 * @return true when they were appended, false without memory
 */
CERVER_HIDDEN bool tls_connection_output_append (
	TlsConnection *connection, const void *data, const size_t size
);

/**
 * @internal
 * @brief The provider asks to send with kTLS: what waits in the output is sent first, and then the key is given
 * to the kernel.
 *
 * @param[in,out] connection the connection
 * @param[in] crypto_info the key in the form of the kernel (a struct tls_crypto_info first)
 * @param[in] transmit true for what is sent; what is received always stays in the provider
 * @return true when the kernel took it
 */
CERVER_HIDDEN bool tls_connection_ktls_start (
	TlsConnection *connection, const void *crypto_info, const bool transmit
);

/**
 * @internal
 * @brief The provider sends bytes with kTLS: a record of a type (a control message) or data.
 *
 * @param[in,out] connection the connection
 * @param[in] record_type the type of the record, 0 for data
 * @param[in] data the bytes
 * @param[in] size how many
 * @return the bytes that were sent, -1 with errno
 */
CERVER_HIDDEN ssize_t tls_connection_ktls_send (
	TlsConnection *connection, const int record_type, const void *data, const size_t size
);

#pragma endregion

#pragma region cerver

struct _Cerver;
struct _Connection;
struct _Socket;

/**
 * @internal
 * @brief Deletes the TLS of a cerver.
 *
 * @param[in] tls_ptr the TLS of a cerver, it can be NULL
 */
CERVER_HIDDEN void cerver_tls_delete (void *tls_ptr);

/**
 * @internal
 * @brief Makes the context of a cerver with TLS and starts the threads of its handshakes and of its maintenance;
 * called by cerver_start ().
 *
 * @param[in,out] cerver the cerver
 * @retval 0 started, or the cerver has no TLS
 * @retval 1 the certificates or the settings failed, or a thread could not start
 */
CERVER_HIDDEN u8 cerver_tls_start (struct _Cerver *cerver);

/**
 * @internal
 * @brief Stops the threads of the TLS of a cerver; the handshakes that wait drop their connections.
 *
 * @param[in,out] cerver the cerver
 */
CERVER_HIDDEN void cerver_tls_end (struct _Cerver *cerver);

/**
 * @internal
 * @brief Runs the handshake of a connection of the cerver, once: the TLS is set in its socket.
 *
 * @param[in,out] cerver the cerver with TLS
 * @param[in,out] socket the socket of the connection
 * @return true when the handshake ended (now or before)
 */
CERVER_HIDDEN bool cerver_tls_accept (struct _Cerver *cerver, struct _Socket *socket);

/**
 * @internal
 * @brief Queues the handshake of a new connection of a cerver that is not a web one; the connection is registered
 * after it ends (cerver_register_new_connection_ready ()) or dropped when it fails.
 *
 * @param[in,out] cerver the cerver with TLS
 * @param[in] connection the new connection
 * @retval 0 queued
 * @retval 1 the queue is full or no memory, the caller drops the connection
 */
CERVER_HIDDEN u8 cerver_tls_dispatch (struct _Cerver *cerver, struct _Connection *connection);

/**
 * @internal
 * @brief Counts a connection that was closed before its handshake because the cerver was full.
 *
 * @param[in,out] cerver the cerver with TLS
 */
CERVER_HIDDEN void cerver_tls_reject (struct _Cerver *cerver);

#pragma endregion

#pragma region client

/**
 * @internal
 * @brief Makes the TLS of a client with its settings.
 *
 * @param[in] settings the settings, the client takes them
 * @return the TLS of the client, NULL without memory
 */
CERVER_HIDDEN ClientTls *client_tls_create (TlsSettings *settings);

/**
 * @internal
 * @brief Deletes the TLS of a client.
 *
 * @param[in] client_tls_ptr the TLS of a client, it can be NULL
 */
CERVER_HIDDEN void client_tls_delete (void *client_tls_ptr);

/**
 * @internal
 * @brief Runs the handshake of a client over a connected fd.
 *
 * @param[in,out] client_tls the TLS of the client
 * @param[in] fd the connected socket
 * @param[in] host the host that it connected to, the name that is verified when the settings have none
 * @param[in] port the port, for the cache of sessions
 * @param[out] failure the reason when it fails
 * @return the TLS of the connection, NULL when it failed
 */
CERVER_HIDDEN TlsConnection *client_tls_connect (
	ClientTls *client_tls, const int fd, const char *host, const u16 port, TlsFailure *failure
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
