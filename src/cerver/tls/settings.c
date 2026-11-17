#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/tls/internal.h"
#include "cerver/tls/tls.h"

// a protocol of ALPN has its size in one byte
#define TLS_ALPN_PROTOCOL_MAX			255

// the separator of the protocols of tls_settings_set_alpn ()
#define TLS_ALPN_SEPARATOR				','

// the separator of the scheme of an URI of a key store
#define TLS_URI_SCHEME_SEPARATOR		':'

// a key with it is a path, never an URI
#define TLS_PATH_SEPARATOR				'/'

#pragma region types

const char *tls_version_to_string (const TlsVersion version) {

	switch (version) {
		#define XX(num, name, string) case TLS_VERSION_##name: return #string;
		TLS_VERSION_MAP(XX)
		#undef XX
	}

	return tls_version_to_string (TLS_VERSION_NONE);

}

const char *tls_client_verify_description (const TlsClientVerify verify) {

	switch (verify) {
		#define XX(num, name, description) case TLS_CLIENT_VERIFY_##name: return #description;
		TLS_CLIENT_VERIFY_MAP(XX)
		#undef XX
	}

	return tls_client_verify_description (TLS_CLIENT_VERIFY_NONE);

}

const char *tls_failure_description (const TlsFailure failure) {

	switch (failure) {
		#define XX(num, name, description) case TLS_FAILURE_##name: return #description;
		TLS_FAILURE_MAP(XX)
		#undef XX
	}

	return tls_failure_description (TLS_FAILURE_NONE);

}

#pragma endregion

#pragma region counters

void tls_counter_add (u64 *counter, const u64 value) {

	(void) __atomic_add_fetch (counter, value, __ATOMIC_RELAXED);

}

static u64 tls_counter_get (const u64 *counter) {

	return __atomic_load_n (counter, __ATOMIC_RELAXED);

}

void tls_counters_copy (const TlsCounters *counters, TlsStats *stats) {

	stats->handshakes = tls_counter_get (&counters->stats.handshakes);
	stats->resumed = tls_counter_get (&counters->stats.resumed);

	for (unsigned int index = 0; index < TLS_FAILURE_COUNT; index++) {
		stats->failures[index] = tls_counter_get (&counters->stats.failures[index]);
	}

	stats->ktls_send = tls_counter_get (&counters->stats.ktls_send);
	stats->early_data_accepted = tls_counter_get (&counters->stats.early_data_accepted);
	stats->early_data_rejected = tls_counter_get (&counters->stats.early_data_rejected);
	stats->reloads = tls_counter_get (&counters->stats.reloads);
	stats->reload_failures = tls_counter_get (&counters->stats.reload_failures);
	stats->ocsp_refreshes = tls_counter_get (&counters->stats.ocsp_refreshes);
	stats->ocsp_failures = tls_counter_get (&counters->stats.ocsp_failures);
	stats->certificate_expiry_seconds = __atomic_load_n (
		&counters->stats.certificate_expiry_seconds, __ATOMIC_RELAXED
	);

}

#pragma endregion

#pragma region settings

// replaces a string of the settings with a copy, returns 0 on success
static u8 tls_settings_copy_string (char **target, const char *value) {

	u8 retval = 1;

	char *copy = value ? strdup (value) : NULL;
	if (copy || !value) {
		free (*target);
		*target = copy;
		retval = 0;
	}

	return retval;

}

TlsSettings *tls_settings_create (void) {

	TlsSettings *settings = (TlsSettings *) calloc (1, sizeof (TlsSettings));
	if (settings) {
		settings->min_version = TLS_VERSION_1_2;
		settings->client_verify = TLS_CLIENT_VERIFY_NONE;
		settings->tickets = true;
		settings->handshake_timeout_milliseconds = TLS_HANDSHAKE_TIMEOUT_MILLISECONDS_DEFAULT;
		settings->handshake_threads = TLS_HANDSHAKE_THREADS_DEFAULT;
	}

	return settings;

}

static void tls_certificate_paths_clear (TlsCertificatePaths *paths) {

	free (paths->chain);
	free (paths->key);

	paths->chain = NULL;
	paths->key = NULL;

}

void tls_settings_delete (void *settings_ptr) {

	TlsSettings *settings = (TlsSettings *) settings_ptr;
	if (settings) {
		for (size_t index = 0; index < settings->n_certificates; index++) {
			tls_certificate_paths_clear (&settings->certificates[index]);
		}

		free (settings->certificates);
		tls_certificate_paths_clear (&settings->client_certificate);

		free (settings->key_store_name);
		free (settings->key_store_properties);
		free (settings->ciphers12);
		free (settings->ciphers13);
		free (settings->groups);
		free (settings->alpn);
		free (settings->ca_file);
		free (settings->ca_path);
		free (settings->server_name);
		free (settings->ticket_keys_file);
		free (settings->ocsp_responder);

		free (settings);
	}

}

// fills the paths with copies, returns 0 on success
static u8 tls_certificate_paths_set (
	TlsCertificatePaths *paths, const char *chain, const char *key
) {

	u8 retval = 1;

	char *chain_copy = strdup (chain);
	char *key_copy = strdup (key);
	if (chain_copy && key_copy) {
		tls_certificate_paths_clear (paths);
		paths->chain = chain_copy;
		paths->key = key_copy;
		retval = 0;
	}

	else {
		free (chain_copy);
		free (key_copy);
	}

	return retval;

}

u8 tls_settings_add_certificate (
	TlsSettings *settings, const char *certificate_chain, const char *private_key
) {

	u8 retval = 1;

	if (settings && certificate_chain && private_key) {
		TlsCertificatePaths *certificates = (TlsCertificatePaths *) realloc (
			settings->certificates, (settings->n_certificates + 1) * sizeof (TlsCertificatePaths)
		);

		if (certificates) {
			settings->certificates = certificates;

			TlsCertificatePaths *paths = &certificates[settings->n_certificates];
			paths->chain = NULL;
			paths->key = NULL;

			if (!tls_certificate_paths_set (paths, certificate_chain, private_key)) {
				settings->n_certificates += 1;
				retval = 0;
			}
		}
	}

	return retval;

}

u8 tls_settings_set_key_store (
	TlsSettings *settings, const char *name, const char *properties
) {

	u8 retval = 1;

	if (settings && name) {
		retval = tls_settings_copy_string (&settings->key_store_name, name)
			|| tls_settings_copy_string (&settings->key_store_properties, properties);
	}

	return retval;

}

void tls_settings_set_min_version (TlsSettings *settings, const TlsVersion version) {

	if (settings && ((version == TLS_VERSION_1_2) || (version == TLS_VERSION_1_3))) {
		settings->min_version = version;
	}

}

u8 tls_settings_set_ciphers (
	TlsSettings *settings, const char *tls12, const char *tls13
) {

	u8 retval = 1;

	if (settings) {
		retval = tls_settings_copy_string (&settings->ciphers12, tls12)
			|| tls_settings_copy_string (&settings->ciphers13, tls13);
	}

	return retval;

}

u8 tls_settings_set_groups (TlsSettings *settings, const char *groups) {

	return (settings && groups) ? tls_settings_copy_string (&settings->groups, groups) : 1;

}

u8 tls_settings_set_alpn (TlsSettings *settings, const char *protocols) {

	u8 retval = 1;

	u8 wire[TLS_ALPN_PROTOCOL_MAX + 1];
	if (settings && protocols && tls_settings_alpn_wire (protocols, wire, sizeof (wire))) {
		retval = tls_settings_copy_string (&settings->alpn, protocols);
	}

	return retval;

}

u8 tls_settings_set_ca (
	TlsSettings *settings, const char *ca_file, const char *ca_path
) {

	u8 retval = 1;

	if (settings && (ca_file || ca_path)) {
		retval = tls_settings_copy_string (&settings->ca_file, ca_file)
			|| tls_settings_copy_string (&settings->ca_path, ca_path);
	}

	return retval;

}

void tls_settings_set_client_verify (TlsSettings *settings, const TlsClientVerify verify) {

	if (settings && (verify >= TLS_CLIENT_VERIFY_NONE) && (verify <= TLS_CLIENT_VERIFY_REQUIRED)) {
		settings->client_verify = verify;
	}

}

u8 tls_settings_set_client_certificate (
	TlsSettings *settings, const char *certificate_chain, const char *private_key
) {

	return (settings && certificate_chain && private_key) ?
		tls_certificate_paths_set (&settings->client_certificate, certificate_chain, private_key) : 1;

}

u8 tls_settings_set_server_name (TlsSettings *settings, const char *server_name) {

	return (settings && server_name) ? tls_settings_copy_string (&settings->server_name, server_name) : 1;

}

void tls_settings_set_tickets (TlsSettings *settings, const bool tickets) {

	if (settings) settings->tickets = tickets;

}

u8 tls_settings_set_ticket_keys (TlsSettings *settings, const char *keys_file) {

	return (settings && keys_file) ? tls_settings_copy_string (&settings->ticket_keys_file, keys_file) : 1;

}

void tls_settings_set_early_data (TlsSettings *settings, const u32 max_bytes) {

	if (settings) settings->early_data = max_bytes;

}

void tls_settings_set_ocsp_stapling (TlsSettings *settings, const bool stapling) {

	if (settings) settings->ocsp_stapling = stapling;

}

u8 tls_settings_set_ocsp_responder (TlsSettings *settings, const char *url) {

	return (settings && url) ? tls_settings_copy_string (&settings->ocsp_responder, url) : 1;

}

void tls_settings_set_ktls (TlsSettings *settings, const bool ktls) {

	if (settings) settings->ktls = ktls;

}

void tls_settings_set_handshake_timeout_milliseconds (
	TlsSettings *settings, const unsigned int milliseconds
) {

	if (settings && milliseconds) settings->handshake_timeout_milliseconds = milliseconds;

}

void tls_settings_set_handshake_threads (TlsSettings *settings, const unsigned int threads) {

	if (settings && threads) settings->handshake_threads = threads;

}

bool tls_settings_key_is_uri (const char *key) {

	bool retval = false;

	// a scheme of letters before ':' (pkcs11:...), never a path
	if (key) {
		const char *separator = strchr (key, TLS_URI_SCHEME_SEPARATOR);
		if (separator && (separator != key) && !strchr (key, TLS_PATH_SEPARATOR)) retval = true;
	}

	return retval;

}

// appends one protocol with its size, returns the bytes that it took, 0 on an error
static size_t tls_settings_alpn_wire_protocol (
	const char *protocol, const size_t length, u8 *wire, const size_t room
) {

	size_t retval = 0;

	if (length && (length <= TLS_ALPN_PROTOCOL_MAX) && ((length + 1) <= room)) {
		wire[0] = (u8) length;
		(void) memcpy (wire + 1, protocol, length);
		retval = length + 1;
	}

	return retval;

}

size_t tls_settings_alpn_wire (const char *protocols, u8 *wire, const size_t wire_size) {

	size_t written = 0;
	bool valid = (protocols != NULL);

	const char *start = protocols;
	while (valid && *start) {
		const char *separator = strchr (start, TLS_ALPN_SEPARATOR);
		const size_t length = separator ? (size_t) (separator - start) : strlen (start);

		const size_t taken = tls_settings_alpn_wire_protocol (start, length, wire + written, wire_size - written);
		valid = (taken > 0);
		written += taken;

		start = separator ? (separator + 1) : (start + length);
	}

	return valid ? written : 0;

}

#pragma endregion

#pragma region certificate

const char *tls_certificate_get_pem (TlsCertificate *certificate) {

	const char *retval = NULL;

	if (certificate) {
		if (!certificate->pem && certificate->provider_certificate) {
			certificate->pem = cerver_crypto_provider_tls_certificate_pem (certificate->provider_certificate);
		}

		retval = certificate->pem;
	}

	return retval;

}

void tls_certificate_delete (void *certificate_ptr) {

	TlsCertificate *certificate = (TlsCertificate *) certificate_ptr;
	if (certificate) {
		free (certificate->subject);
		free (certificate->issuer);
		free (certificate->serial);
		free (certificate->alternative_names);
		free (certificate->pem);

		cerver_crypto_provider_tls_certificate_release (certificate->provider_certificate);

		free (certificate);
	}

}

#pragma endregion
