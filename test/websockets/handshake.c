#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/types/types.h>

#include <cerver/http/websockets/handshake.h>

#include "../test.h"

#include "websocket.h"

#define HANDSHAKE_TEST_GENERATED_KEYS		(1 << 6)

// the example of RFC 6455 section 1.3
static void test_handshake_accept (void) {

	char accept[HTTP_WEB_SOCKET_ACCEPT_SIZE];
	test_check_true (http_web_socket_accept_compute ("dGhlIHNhbXBsZSBub25jZQ==", 24, accept));
	test_check_str_eq (accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", NULL);

	test_check_false (http_web_socket_accept_compute ("short", 5, accept));
	test_check_false (http_web_socket_accept_compute (NULL, 24, accept));

}

static bool handshake_test_key (const char *key, const size_t length) {

	return http_web_socket_key_is_valid (key, length);

}

static void test_handshake_keys (void) {

	test_check_true (handshake_test_key ("dGhlIHNhbXBsZSBub25jZQ==", 24));
	test_check_true (handshake_test_key ("AAAAAAAAAAAAAAAAAAAAAA==", 24));
	test_check_true (handshake_test_key ("+/+/+/+/+/+/+/+/+/+/+w==", 24));

	test_check_false (handshake_test_key ("dGhlIHNhbXBsZSBub25jZQ=", 23));
	test_check_false (handshake_test_key ("dGhlIHNhbXBsZSBub25jZQ===", 25));
	test_check_false (handshake_test_key ("dGhlIHNhbXBsZSBub25jZQAA", 24));
	test_check_false (handshake_test_key ("dGhlIHNhbXBsZSBub25jZ===", 24));
	test_check_false (handshake_test_key ("dGhlIHNhbXBsZSBub2!jZQ==", 24));
	test_check_false (handshake_test_key ("dGhlIHNhbXBsZSBub25j Q==", 24));
	test_check_false (handshake_test_key (NULL, 24));

	// a NUL inside the key
	test_check_false (handshake_test_key ("dGhlIHNhbXBs\0SBub25jZQ==", 24));

	char previous[HTTP_WEB_SOCKET_KEY_SIZE] = { 0 };
	for (unsigned int i = 0; i < HANDSHAKE_TEST_GENERATED_KEYS; i++) {
		char key[HTTP_WEB_SOCKET_KEY_SIZE];
		test_check_true (http_web_socket_key_generate (key));
		test_check_unsigned_eq (strlen (key), HTTP_WEB_SOCKET_KEY_LENGTH, NULL);
		test_check_true (handshake_test_key (key, strlen (key)));
		test_check (strcmp (key, previous), "two keys were the same");
		(void) strcpy (previous, key);
	}

}

static bool handshake_test_has (const char *value, const char *token) {

	return http_web_socket_header_has_token (value, strlen (value), token);

}

// the tokens of Connection and Upgrade, in lists, without regard to case
static void test_handshake_tokens (void) {

	test_check_true (handshake_test_has ("Upgrade", "upgrade"));
	test_check_true (handshake_test_has ("keep-alive, Upgrade", "upgrade"));
	test_check_true (handshake_test_has ("keep-alive,Upgrade", "upgrade"));
	test_check_true (handshake_test_has ("  upgrade  ,  close", "upgrade"));
	test_check_true (handshake_test_has (",,upgrade,,", "upgrade"));
	test_check_true (handshake_test_has ("WebSocket", "websocket"));
	test_check_true (handshake_test_has ("h2c, websocket", "websocket"));
	test_check_true (handshake_test_has ("\tupgrade\t", "upgrade"));

	test_check_false (handshake_test_has ("upgraded", "upgrade"));
	test_check_false (handshake_test_has ("keep-alive", "upgrade"));
	test_check_false (handshake_test_has ("", "upgrade"));
	test_check_false (handshake_test_has ("websocket2", "websocket"));
	test_check_false (handshake_test_has ("web socket", "websocket"));
	test_check_false (http_web_socket_header_has_token (NULL, 0, "upgrade"));

}

static const char *handshake_test_select (const char *offered, const char * const *protocols, const unsigned int n) {

	return http_web_socket_protocol_select (offered, strlen (offered), protocols, n);

}

// the first protocol of the server that the client offered, the names are case sensitive
static void test_handshake_protocols (void) {

	const char *protocols[] = { "chat.v2", "chat", "superchat" };

	test_check_str_eq (handshake_test_select ("chat, superchat", protocols, 3), "chat", NULL);
	test_check_str_eq (handshake_test_select ("superchat, chat.v2", protocols, 3), "chat.v2", NULL);
	test_check_str_eq (handshake_test_select (" superchat ", protocols, 3), "superchat", NULL);

	test_check_null_ptr (handshake_test_select ("Chat", protocols, 3));
	test_check_null_ptr (handshake_test_select ("chat.v3, xmpp", protocols, 3));
	test_check_null_ptr (handshake_test_select ("", protocols, 3));
	test_check_null_ptr (handshake_test_select ("chat", protocols, 0));
	test_check_null_ptr (http_web_socket_protocol_select (NULL, 0, protocols, 3));

}

void web_socket_tests_handshake (void) {

	(void) printf ("Testing WebSocket handshake...\n");

	test_run (test_handshake_accept);
	test_run (test_handshake_keys);
	test_run (test_handshake_tokens);
	test_run (test_handshake_protocols);

	(void) printf ("Done!\n");

}
