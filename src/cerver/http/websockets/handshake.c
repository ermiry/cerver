#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "cerver/types/types.h"

#include "cerver/utils/base64.h"

#include "cerver/http/websockets/handshake.h"

#define HTTP_WEB_SOCKET_GUID_LENGTH					(sizeof (HTTP_WEB_SOCKET_GUID) - 1)

// the bytes of a SHA-1
#define HTTP_WEB_SOCKET_SHA1_SIZE					20

// a key ends with two '=' because 16 bytes are not a multiple of 3
#define HTTP_WEB_SOCKET_KEY_DATA_LENGTH				22

// the last character of the data of a key has 2 bits of its 16 bytes and 4 that are not used:
// they are zero in the only form that a key has
#define HTTP_WEB_SOCKET_KEY_UNUSED_BITS				0x0F

// the value of a character that is not of base64
#define HTTP_WEB_SOCKET_BASE64_NONE					0xFF

static const char http_web_socket_padding = '=';

static const char *http_web_socket_base64_alphabet = {
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
};

static const char http_web_socket_list_separator = ',';
static const char http_web_socket_space = ' ';
static const char http_web_socket_tab = '\t';

// the 6 bits of a character of base64, HTTP_WEB_SOCKET_BASE64_NONE when it is not one
static unsigned int http_web_socket_base64_value (const char c) {

	const char *found = (c != '\0') ? strchr (http_web_socket_base64_alphabet, c) : NULL;

	return found ? (unsigned int) (found - http_web_socket_base64_alphabet) : HTTP_WEB_SOCKET_BASE64_NONE;

}

bool http_web_socket_key_is_valid (
	const char *key, const size_t length
) {

	bool valid = key && (length == HTTP_WEB_SOCKET_KEY_LENGTH);

	unsigned int value = 0;
	for (size_t i = 0; valid && (i < HTTP_WEB_SOCKET_KEY_DATA_LENGTH); i++) {
		value = http_web_socket_base64_value (key[i]);
		valid = (value != HTTP_WEB_SOCKET_BASE64_NONE);
	}

	// the last character of the data
	if (valid) valid = !(value & HTTP_WEB_SOCKET_KEY_UNUSED_BITS);

	for (size_t i = HTTP_WEB_SOCKET_KEY_DATA_LENGTH; valid && (i < HTTP_WEB_SOCKET_KEY_LENGTH); i++) {
		valid = (key[i] == http_web_socket_padding);
	}

	return valid;

}

bool http_web_socket_key_generate (char *key) {

	bool generated = false;

	u8 bytes[HTTP_WEB_SOCKET_KEY_BYTES];
	if (key && (RAND_bytes (bytes, HTTP_WEB_SOCKET_KEY_BYTES) == 1)) {
		char encoded[base64_encode_len (HTTP_WEB_SOCKET_KEY_BYTES)];
		const size_t length = base64_encode (encoded, (const char *) bytes, HTTP_WEB_SOCKET_KEY_BYTES);
		if (length == HTTP_WEB_SOCKET_KEY_LENGTH) {
			(void) memcpy (key, encoded, HTTP_WEB_SOCKET_KEY_LENGTH);
			key[HTTP_WEB_SOCKET_KEY_LENGTH] = '\0';
			generated = true;
		}
	}

	return generated;

}

bool http_web_socket_accept_compute (
	const char *key, const size_t length, char *accept
) {

	bool computed = false;

	if (key && accept && (length == HTTP_WEB_SOCKET_KEY_LENGTH)) {
		char input[HTTP_WEB_SOCKET_KEY_LENGTH + HTTP_WEB_SOCKET_GUID_LENGTH];
		(void) memcpy (input, key, HTTP_WEB_SOCKET_KEY_LENGTH);
		(void) memcpy (input + HTTP_WEB_SOCKET_KEY_LENGTH, HTTP_WEB_SOCKET_GUID, HTTP_WEB_SOCKET_GUID_LENGTH);

		unsigned char hash[HTTP_WEB_SOCKET_SHA1_SIZE];
		unsigned int hash_length = 0;
		if (
			EVP_Digest (input, sizeof (input), hash, &hash_length, EVP_sha1 (), NULL)
			&& (hash_length == HTTP_WEB_SOCKET_SHA1_SIZE)
		) {
			char encoded[base64_encode_len (HTTP_WEB_SOCKET_SHA1_SIZE)];
			const size_t encoded_length = base64_encode (encoded, (const char *) hash, HTTP_WEB_SOCKET_SHA1_SIZE);
			if (encoded_length == HTTP_WEB_SOCKET_ACCEPT_LENGTH) {
				(void) memcpy (accept, encoded, HTTP_WEB_SOCKET_ACCEPT_LENGTH);
				accept[HTTP_WEB_SOCKET_ACCEPT_LENGTH] = '\0';
				computed = true;
			}
		}
	}

	return computed;

}

static bool http_web_socket_is_space (const char c) {

	return (c == http_web_socket_space) || (c == http_web_socket_tab);

}

// the next element of a list, without its spaces
// returns the position after its separator
static size_t http_web_socket_list_next (
	const char *value, const size_t length, size_t position,
	const char **element, size_t *element_length
) {

	while ((position < length) && http_web_socket_is_space (value[position])) position += 1;

	const size_t start = position;
	while ((position < length) && (value[position] != http_web_socket_list_separator)) position += 1;

	size_t end = position;
	while ((end > start) && http_web_socket_is_space (value[end - 1])) end -= 1;

	*element = value + start;
	*element_length = end - start;

	// skips the separator
	return (position < length) ? (position + 1) : position;

}

bool http_web_socket_header_has_token (
	const char *value, const size_t length, const char *token
) {

	bool found = false;

	if (value && token) {
		const size_t token_length = strlen (token);

		size_t position = 0;
		while (!found && (position < length)) {
			const char *element = NULL;
			size_t element_length = 0;
			position = http_web_socket_list_next (value, length, position, &element, &element_length);

			found = (element_length == token_length) && !strncasecmp (element, token, token_length);
		}
	}

	return found;

}

// returns true when the client offered the protocol (case sensitive, RFC 6455 section 4.1)
static bool http_web_socket_protocol_offered (
	const char *offered, const size_t length, const char *protocol
) {

	bool found = false;

	const size_t protocol_length = strlen (protocol);

	size_t position = 0;
	while (!found && (position < length)) {
		const char *element = NULL;
		size_t element_length = 0;
		position = http_web_socket_list_next (offered, length, position, &element, &element_length);

		found = (element_length == protocol_length) && !strncmp (element, protocol, protocol_length);
	}

	return found;

}

const char *http_web_socket_protocol_select (
	const char *offered, const size_t length,
	const char * const *protocols, const unsigned int n_protocols
) {

	const char *selected = NULL;

	if (offered && protocols) {
		for (unsigned int i = 0; !selected && (i < n_protocols); i++) {
			if (protocols[i] && http_web_socket_protocol_offered (offered, length, protocols[i])) {
				selected = protocols[i];
			}
		}
	}

	return selected;

}
