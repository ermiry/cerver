#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <cerver/tls/client.h>
#include <cerver/tls/tls.h>

#include "cerver/tls/internal.h"

#include "../test.h"
#include "tls.h"

#define SETTINGS_WIRE_SIZE				(1 << 9)

static void test_settings_defaults (void) {

	TlsSettings *settings = tls_settings_create ();
	test_check_ptr (settings);

	test_check_int_eq (settings->min_version, TLS_VERSION_1_2, NULL);
	test_check_int_eq (settings->client_verify, TLS_CLIENT_VERIFY_NONE, NULL);
	test_check_true (settings->tickets);
	test_check_false (settings->ktls);
	test_check_false (settings->ocsp_stapling);
	test_check_unsigned_eq (settings->early_data, 0, NULL);
	test_check_unsigned_eq (settings->handshake_timeout_milliseconds, TLS_HANDSHAKE_TIMEOUT_MILLISECONDS_DEFAULT, NULL);
	test_check_unsigned_eq (settings->handshake_threads, TLS_HANDSHAKE_THREADS_DEFAULT, NULL);

	tls_settings_delete (settings);

	// a NULL is ignored by every setter
	tls_settings_delete (NULL);
	tls_settings_set_min_version (NULL, TLS_VERSION_1_3);
	test_check_unsigned_eq (tls_settings_add_certificate (NULL, "a", "b"), 1, NULL);
	test_check_unsigned_eq (tls_settings_set_alpn (NULL, "http/1.1"), 1, NULL);

}

static void test_settings_setters (void) {

	TlsSettings *settings = tls_settings_create ();

	test_check_unsigned_eq (tls_settings_add_certificate (settings, "one.pem", "one.key"), 0, NULL);
	test_check_unsigned_eq (tls_settings_add_certificate (settings, "two.pem", "pkcs11:token=cerver;object=key"), 0, NULL);
	test_check_unsigned_eq ((unsigned int) settings->n_certificates, 2, NULL);
	test_check_str_eq (settings->certificates[1].key, "pkcs11:token=cerver;object=key", NULL);
	test_check_unsigned_eq (tls_settings_add_certificate (settings, NULL, "key"), 1, NULL);

	tls_settings_set_min_version (settings, TLS_VERSION_1_3);
	test_check_int_eq (settings->min_version, TLS_VERSION_1_3, NULL);

	// an older version is never taken
	tls_settings_set_min_version (settings, TLS_VERSION_NONE);
	test_check_int_eq (settings->min_version, TLS_VERSION_1_3, NULL);

	test_check_unsigned_eq (tls_settings_set_ciphers (settings, "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256", NULL), 0, NULL);
	test_check_str_eq (settings->ciphers12, "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256", NULL);
	test_check_null_ptr (settings->ciphers13);

	test_check_unsigned_eq (tls_settings_set_groups (settings, "X25519:P-256"), 0, NULL);
	test_check_unsigned_eq (tls_settings_set_ca (settings, NULL, NULL), 1, NULL);
	test_check_unsigned_eq (tls_settings_set_ca (settings, "ca.pem", NULL), 0, NULL);

	tls_settings_set_client_verify (settings, TLS_CLIENT_VERIFY_REQUIRED);
	test_check_int_eq (settings->client_verify, TLS_CLIENT_VERIFY_REQUIRED, NULL);
	tls_settings_set_client_verify (settings, (TlsClientVerify) 7);
	test_check_int_eq (settings->client_verify, TLS_CLIENT_VERIFY_REQUIRED, NULL);

	test_check_unsigned_eq (tls_settings_set_client_certificate (settings, "client.pem", "client.key"), 0, NULL);
	test_check_unsigned_eq (tls_settings_set_server_name (settings, "cerver.test"), 0, NULL);
	test_check_unsigned_eq (tls_settings_set_ticket_keys (settings, "keys"), 0, NULL);
	test_check_unsigned_eq (tls_settings_set_ocsp_responder (settings, "http://localhost:8888"), 0, NULL);
	test_check_unsigned_eq (tls_settings_set_key_store (settings, "pkcs11", NULL), 0, NULL);

	tls_settings_set_tickets (settings, false);
	tls_settings_set_early_data (settings, 1 << 14);
	tls_settings_set_ocsp_stapling (settings, true);
	tls_settings_set_ktls (settings, true);
	tls_settings_set_handshake_timeout_milliseconds (settings, 0);
	tls_settings_set_handshake_threads (settings, 0);

	test_check_false (settings->tickets);
	test_check_unsigned_eq (settings->early_data, 1 << 14, NULL);
	test_check_true (settings->ocsp_stapling);
	test_check_true (settings->ktls);
	test_check_unsigned_eq (settings->handshake_timeout_milliseconds, TLS_HANDSHAKE_TIMEOUT_MILLISECONDS_DEFAULT, NULL);
	test_check_unsigned_eq (settings->handshake_threads, TLS_HANDSHAKE_THREADS_DEFAULT, NULL);

	tls_settings_delete (settings);

}

static void test_settings_alpn (void) {

	TlsSettings *settings = tls_settings_create ();

	test_check_unsigned_eq (tls_settings_set_alpn (settings, "http/1.1"), 0, NULL);
	test_check_unsigned_eq (tls_settings_set_alpn (settings, "h2,http/1.1"), 0, NULL);
	test_check_str_eq (settings->alpn, "h2,http/1.1", NULL);

	// an empty protocol, or one longer than 255 bytes, is an error that keeps the previous ones
	test_check_unsigned_eq (tls_settings_set_alpn (settings, ","), 1, NULL);
	test_check_unsigned_eq (tls_settings_set_alpn (settings, "h2,,http/1.1"), 1, NULL);

	char long_protocol[SETTINGS_WIRE_SIZE];
	(void) memset (long_protocol, 'a', sizeof (long_protocol) - 1);
	long_protocol[sizeof (long_protocol) - 1] = '\0';
	test_check_unsigned_eq (tls_settings_set_alpn (settings, long_protocol), 1, NULL);

	test_check_str_eq (settings->alpn, "h2,http/1.1", NULL);

	tls_settings_delete (settings);

}

static void test_settings_strings (void) {

	test_check_str_eq (tls_version_to_string (TLS_VERSION_1_3), "TLS 1.3", NULL);
	test_check_str_eq (tls_version_to_string ((TlsVersion) 9), "None", NULL);
	test_check_str_eq (tls_failure_description (TLS_FAILURE_TIMEOUT), "The handshake did not end before its deadline", NULL);
	test_check_str_eq (tls_failure_description ((TlsFailure) 99), "The handshake did not fail", NULL);
	test_check_ptr (tls_client_verify_description (TLS_CLIENT_VERIFY_OPTIONAL));

}

static void test_settings_owners (void) {

	// a client takes the settings even when it fails
	test_check_unsigned_eq (client_set_tls (NULL, tls_settings_create ()), 1, NULL);

	TlsStats stats;
	test_check_unsigned_eq (client_tls_get_stats (NULL, &stats), 1, NULL);

}

void tls_tests_settings (void) {

	(void) printf ("Testing TLS settings...\n");

	test_settings_defaults ();
	test_settings_setters ();
	test_settings_alpn ();
	test_settings_strings ();
	test_settings_owners ();

	(void) printf ("Done!\n");

}
