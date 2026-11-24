#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "../test.h"
#include "tls.h"

// the seconds of a day
#define CERTIFICATES_DAY						(60L * 60L * 24L)
// the days that a test certificate is valid
#define CERTIFICATES_VALID_DAYS					(1L << 10)
// the days of the one that expires soon, fewer than the warning of cerver
#define CERTIFICATES_EXPIRING_DAYS				(1L << 3)

// the template of the folder of a run
#define CERTIFICATES_DIRECTORY_TEMPLATE			"/tmp/cerver-tls-XXXXXX"

// the characters of the comment of the large certificate, more than a small socket takes at once
#define CERTIFICATES_LARGE_COMMENT_SIZE			(1 << 16)
#define CERTIFICATES_LARGE_COMMENT_CHARACTER	'c'

typedef struct CertificateRequest {

	const char *common_name;
	const char *alternative_names;
	bool authority;
	long not_before_days;
	long not_after_days;
	long serial;

} CertificateRequest;

static EVP_PKEY *certificates_key (void) {

	EVP_PKEY *key = EVP_EC_gen ("P-256");
	test_check_ptr (key);

	return key;

}

static void certificates_extension (X509 *certificate, X509 *issuer, const int nid, const char *value) {

	X509V3_CTX context;
	X509V3_set_ctx_nodb (&context);
	X509V3_set_ctx (&context, issuer, certificate, NULL, NULL, 0);

	X509_EXTENSION *extension = X509V3_EXT_conf_nid (NULL, &context, nid, value);
	test_check_ptr (extension);
	test_check_int_eq (X509_add_ext (certificate, extension, -1), 1, NULL);
	X509_EXTENSION_free (extension);

}

static X509 *certificates_make (
	const CertificateRequest *request, EVP_PKEY *key, X509 *issuer, EVP_PKEY *issuer_key
) {

	X509 *certificate = X509_new ();
	test_check_ptr (certificate);

	(void) X509_set_version (certificate, 2);
	(void) ASN1_INTEGER_set (X509_get_serialNumber (certificate), request->serial);
	(void) X509_gmtime_adj (X509_getm_notBefore (certificate), request->not_before_days * CERTIFICATES_DAY);
	(void) X509_gmtime_adj (X509_getm_notAfter (certificate), request->not_after_days * CERTIFICATES_DAY);
	(void) X509_set_pubkey (certificate, key);

	X509_NAME *name = X509_get_subject_name (certificate);
	(void) X509_NAME_add_entry_by_txt (
		name, "CN", MBSTRING_ASC, (const unsigned char *) request->common_name, -1, -1, 0
	);

	(void) X509_set_issuer_name (certificate, issuer ? X509_get_subject_name (issuer) : name);

	X509 *signer = issuer ? issuer : certificate;
	if (request->authority) {
		certificates_extension (certificate, signer, NID_basic_constraints, "critical,CA:TRUE");
		certificates_extension (certificate, signer, NID_key_usage, "critical,keyCertSign,cRLSign");
	}

	else {
		certificates_extension (certificate, signer, NID_basic_constraints, "critical,CA:FALSE");
	}

	if (request->alternative_names) {
		certificates_extension (certificate, signer, NID_subject_alt_name, request->alternative_names);
	}

	test_check (X509_sign (certificate, issuer_key ? issuer_key : key, EVP_sha256 ()) > 0, NULL);

	return certificate;

}

static void certificates_path (char *path, const char *directory, const char *name) {

	(void) snprintf (path, TEST_CERTIFICATE_PATH_SIZE, "%s/%s", directory, name);

}

static void certificates_write_key (const char *path, EVP_PKEY *key) {

	FILE *file = fopen (path, "w");
	test_check_ptr (file);
	test_check_int_eq (PEM_write_PrivateKey (file, key, NULL, NULL, 0, NULL, NULL), 1, NULL);
	(void) fclose (file);

}

// the chain is the certificate and the one of its authority, in order
static void certificates_write_chain (const char *path, X509 *certificate, X509 *authority) {

	FILE *file = fopen (path, "w");
	test_check_ptr (file);
	test_check_int_eq (PEM_write_X509 (file, certificate), 1, NULL);
	if (authority) test_check_int_eq (PEM_write_X509 (file, authority), 1, NULL);
	(void) fclose (file);

}

// a certificate signed by the authority, written with its key
static void certificates_leaf (
	const TestCertificates *certificates, const CertificateRequest *request,
	X509 *authority, EVP_PKEY *authority_key, char *chain, char *key_path, const char *name
) {

	char chain_name[TEST_CERTIFICATE_PATH_SIZE];
	char key_name[TEST_CERTIFICATE_PATH_SIZE];
	(void) snprintf (chain_name, sizeof (chain_name), "%s.pem", name);
	(void) snprintf (key_name, sizeof (key_name), "%s.key", name);

	certificates_path (chain, certificates->directory, chain_name);
	certificates_path (key_path, certificates->directory, key_name);

	EVP_PKEY *key = certificates_key ();
	X509 *certificate = certificates_make (request, key, authority, authority_key);

	certificates_write_chain (chain, certificate, authority);
	certificates_write_key (key_path, key);

	X509_free (certificate);
	EVP_PKEY_free (key);

}

// the certificate of the server with a large comment, signed again with it
static void certificates_large (
	TestCertificates *certificates, const CertificateRequest *request, X509 *authority, EVP_PKEY *authority_key
) {

	certificates_path (certificates->large_chain, certificates->directory, "large.pem");
	certificates_path (certificates->large_key, certificates->directory, "large.key");

	char *comment = (char *) malloc (CERTIFICATES_LARGE_COMMENT_SIZE + 1);
	test_check_ptr (comment);
	(void) memset (comment, CERTIFICATES_LARGE_COMMENT_CHARACTER, CERTIFICATES_LARGE_COMMENT_SIZE);
	comment[CERTIFICATES_LARGE_COMMENT_SIZE] = '\0';

	EVP_PKEY *key = certificates_key ();
	X509 *certificate = certificates_make (request, key, authority, authority_key);
	certificates_extension (certificate, authority, NID_netscape_comment, comment);
	test_check (X509_sign (certificate, authority_key, EVP_sha256 ()) > 0, NULL);

	certificates_write_chain (certificates->large_chain, certificate, authority);
	certificates_write_key (certificates->large_key, key);

	X509_free (certificate);
	EVP_PKEY_free (key);
	free (comment);

}

void test_certificates_create (TestCertificates *certificates) {

	(void) memset (certificates, 0, sizeof (TestCertificates));

	(void) snprintf (certificates->directory, sizeof (certificates->directory), CERTIFICATES_DIRECTORY_TEMPLATE);
	test_check_ptr (mkdtemp (certificates->directory));

	const CertificateRequest authority_request = { "cerver test authority", NULL, true, -1, CERTIFICATES_VALID_DAYS, 1 };
	EVP_PKEY *authority_key = certificates_key ();
	X509 *authority = certificates_make (&authority_request, authority_key, NULL, NULL);

	certificates_path (certificates->authority, certificates->directory, "authority.pem");
	certificates_write_chain (certificates->authority, authority, NULL);

	// the responder of OCSP signs with it
	certificates_path (certificates->authority_key, certificates->directory, "authority.key");
	certificates_write_key (certificates->authority_key, authority_key);

	const CertificateRequest server = { "localhost", "DNS:localhost,IP:127.0.0.1", false, -1, CERTIFICATES_VALID_DAYS, 2 };
	certificates_leaf (certificates, &server, authority, authority_key, certificates->server_chain, certificates->server_key, "server");
	certificates_large (certificates, &server, authority, authority_key);

	const CertificateRequest other = { "other.test", "DNS:other.test", false, -1, CERTIFICATES_VALID_DAYS, 3 };
	certificates_leaf (certificates, &other, authority, authority_key, certificates->other_chain, certificates->other_key, "other");

	const CertificateRequest wildcard = { "wild.test", "DNS:*.wild.test", false, -1, CERTIFICATES_VALID_DAYS, 4 };
	certificates_leaf (certificates, &wildcard, authority, authority_key, certificates->wildcard_chain, certificates->wildcard_key, "wildcard");

	const CertificateRequest client = { "cerver test client", "DNS:client.test", false, -1, CERTIFICATES_VALID_DAYS, 5 };
	certificates_leaf (certificates, &client, authority, authority_key, certificates->client_chain, certificates->client_key, "client");

	const CertificateRequest expired = { "localhost", "DNS:localhost", false, -2, -1, 6 };
	certificates_leaf (certificates, &expired, authority, authority_key, certificates->expired_chain, certificates->expired_key, "expired");

	const CertificateRequest expiring = { "localhost", "DNS:localhost,IP:127.0.0.1", false, -1, CERTIFICATES_EXPIRING_DAYS, 8 };
	certificates_leaf (certificates, &expiring, authority, authority_key, certificates->expiring_chain, certificates->expiring_key, "expiring");

	// a key that is not the one of the server
	certificates_path (certificates->mismatch_key, certificates->directory, "mismatch.key");
	EVP_PKEY *mismatch = certificates_key ();
	certificates_write_key (certificates->mismatch_key, mismatch);
	EVP_PKEY_free (mismatch);

	// a client certificate of another authority, that never verifies
	const CertificateRequest stranger = { "stranger", "DNS:stranger.test", false, -1, CERTIFICATES_VALID_DAYS, 7 };
	char stranger_chain_name[TEST_CERTIFICATE_PATH_SIZE];
	certificates_path (stranger_chain_name, certificates->directory, "stranger.pem");
	certificates_path (certificates->stranger_key, certificates->directory, "stranger.key");
	EVP_PKEY *stranger_key = certificates_key ();
	X509 *stranger_certificate = certificates_make (&stranger, stranger_key, NULL, NULL);
	certificates_write_chain (stranger_chain_name, stranger_certificate, NULL);
	certificates_write_key (certificates->stranger_key, stranger_key);
	(void) memcpy (certificates->stranger_chain, stranger_chain_name, sizeof (stranger_chain_name));
	X509_free (stranger_certificate);
	EVP_PKEY_free (stranger_key);

	X509_free (authority);
	EVP_PKEY_free (authority_key);

}

static void certificates_unlink (const char *path) {

	if (path[0]) (void) unlink (path);

}

void test_certificates_delete (TestCertificates *certificates) {

	const char *files[] = {
		certificates->authority, certificates->authority_key,
		certificates->server_chain, certificates->server_key,
		certificates->other_chain, certificates->other_key,
		certificates->wildcard_chain, certificates->wildcard_key,
		certificates->client_chain, certificates->client_key,
		certificates->expired_chain, certificates->expired_key,
		certificates->expiring_chain, certificates->expiring_key,
		certificates->mismatch_key,
		certificates->stranger_chain, certificates->stranger_key,
		certificates->large_chain, certificates->large_key
	};

	for (size_t index = 0; index < (sizeof (files) / sizeof (files[0])); index++) certificates_unlink (files[index]);

	(void) rmdir (certificates->directory);

}

// copies a file over another one with rename (), like a renewal does
void test_certificates_replace (const char *source, const char *target) {

	char temporary[TEST_CERTIFICATE_PATH_SIZE + 8];
	(void) snprintf (temporary, sizeof (temporary), "%s.new", target);

	FILE *in = fopen (source, "r");
	FILE *out = fopen (temporary, "w");
	test_check_ptr (in);
	test_check_ptr (out);

	char buffer[1 << 12];
	size_t read = 0;
	while ((read = fread (buffer, 1, sizeof (buffer), in)) > 0) {
		test_check_unsigned_eq ((unsigned int) fwrite (buffer, 1, read, out), (unsigned int) read, NULL);
	}

	(void) fclose (in);
	(void) fclose (out);

	test_check_int_eq (rename (temporary, target), 0, NULL);

}
