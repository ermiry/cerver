#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/client/client.h"

#include "cerver/tls/client.h"
#include "cerver/tls/internal.h"
#include "cerver/tls/tls.h"

#include "cerver/utils/log.h"

ClientTls *client_tls_create (TlsSettings *settings) {

	ClientTls *client_tls = (ClientTls *) calloc (1, sizeof (ClientTls));
	if (client_tls) {
		client_tls->settings = settings;
		(void) pthread_mutex_init (&client_tls->mutex, NULL);
	}

	return client_tls;

}

void client_tls_delete (void *client_tls_ptr) {

	ClientTls *client_tls = (ClientTls *) client_tls_ptr;
	if (client_tls) {
		cerver_crypto_provider_tls_context_release (client_tls->context);
		tls_settings_delete (client_tls->settings);

		(void) pthread_mutex_destroy (&client_tls->mutex);

		free (client_tls);
	}

}

// the context is made with the first connection, and kept for the next ones (and their sessions)
static CryptoTlsContext *client_tls_context_get (ClientTls *client_tls, TlsFailure *failure) {

	(void) pthread_mutex_lock (&client_tls->mutex);

	if (!client_tls->context) {
		char error[TLS_ERROR_SIZE] = { 0 };
		client_tls->context = cerver_crypto_provider_tls_context_create (
			client_tls->settings, false, false, error, sizeof (error)
		);

		if (!client_tls->context) cerver_log_error ("Client TLS - %s", error);
	}

	CryptoTlsContext *context = cerver_crypto_provider_tls_context_reference (client_tls->context);

	(void) pthread_mutex_unlock (&client_tls->mutex);

	if (!context) *failure = TLS_FAILURE_CERTIFICATE;

	return context;

}

TlsConnection *client_tls_connect (
	ClientTls *client_tls, const int fd, const char *host, const u16 port, TlsFailure *failure
) {

	*failure = TLS_FAILURE_NONE;

	TlsConnection *connection = NULL;

	CryptoTlsContext *context = client_tls_context_get (client_tls, failure);
	if (context) {
		char destination[TLS_DESTINATION_SIZE] = { 0 };
		(void) snprintf (destination, sizeof (destination), "%s:%u", host, (unsigned int) port);

		const char *server_name = client_tls->settings->server_name ? client_tls->settings->server_name : host;

		connection = tls_connection_create (
			context, fd, false, client_tls->settings->ktls, server_name, destination, &client_tls->counters
		);

		cerver_crypto_provider_tls_context_release (context);

		if (!connection) {
			*failure = TLS_FAILURE_MEMORY;
		}

		else {
			*failure = tls_connection_handshake (connection, client_tls->settings->handshake_timeout_milliseconds);

			if (*failure != TLS_FAILURE_NONE) {
				tls_connection_delete (connection);
				connection = NULL;
			}
		}
	}

	return connection;

}

u8 client_set_tls (struct _Client *client, TlsSettings *settings) {

	u8 retval = 1;

	ClientTls *client_tls = (client && settings) ? client_tls_create (settings) : NULL;
	if (client_tls) {
		client_tls_delete (client->tls);
		client->tls = client_tls;
		retval = 0;
	}

	else {
		tls_settings_delete (settings);
	}

	return retval;

}

u8 client_tls_get_stats (const struct _Client *client, TlsStats *stats) {

	u8 retval = 1;

	if (client && client->tls && stats) {
		tls_counters_copy (&client->tls->counters, stats);
		retval = 0;
	}

	return retval;

}
