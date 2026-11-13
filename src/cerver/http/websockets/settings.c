#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/http/headers.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/settings.h"

const char *http_web_socket_error_to_string (const HttpWebSocketError error) {

	switch (error) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_ERROR_##name: return #string;
		HTTP_WEB_SOCKET_ERROR_MAP(XX)
		#undef XX
	}

	return http_web_socket_error_to_string (HTTP_WEB_SOCKET_ERROR_NONE);

}

const char *http_web_socket_error_description (const HttpWebSocketError error) {

	switch (error) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_ERROR_##name: return #description;
		HTTP_WEB_SOCKET_ERROR_MAP(XX)
		#undef XX
	}

	return http_web_socket_error_description (HTTP_WEB_SOCKET_ERROR_NONE);

}

// the headers that cerver writes in a 101, on_upgrade () can not add them again
static bool http_web_socket_upgrade_header_is_reserved (const char *name) {

	bool reserved = false;

	switch (http_header_type_by_string (name)) {
		case HTTP_HEADER_SERVER:
		case HTTP_HEADER_UPGRADE:
		case HTTP_HEADER_CONNECTION:
		case HTTP_HEADER_SEC_WEBSOCKET_ACCEPT:
		case HTTP_HEADER_SEC_WEBSOCKET_PROTOCOL:
		case HTTP_HEADER_SEC_WEBSOCKET_EXTENSIONS:
		case HTTP_HEADER_CONTENT_LENGTH:
		case HTTP_HEADER_TRANSFER_ENCODING:
			reserved = true;
			break;

		default: break;
	}

	return reserved;

}

u8 http_web_socket_upgrade_add_header (
	HttpWebSocketUpgrade *upgrade, const char *name, const char *value
) {

	u8 retval = 1;

	if (
		upgrade && upgrade->headers && name && value
		&& !http_web_socket_upgrade_header_is_reserved (name)
	) {
		// the name must be a token and the value can not have the end of a line
		retval = http_headers_add_by_name (
			upgrade->headers, name, strlen (name), value, strlen (value)
		);
	}

	return retval;

}

void http_web_socket_settings_init (HttpWebSocketSettings *settings) {

	if (settings) {
		(void) memset (settings, 0, sizeof (HttpWebSocketSettings));

		settings->set = 0;

		settings->max_message = HTTP_WEB_SOCKET_DEFAULT_MAX_MESSAGE;

		settings->protocols = NULL;
		settings->n_protocols = 0;

		http_web_socket_deflate_options_init (&settings->compression);

		settings->origin_check = false;

		settings->ping_interval = HTTP_WEB_SOCKET_DEFAULT_PING_INTERVAL;
		settings->pong_timeout = HTTP_WEB_SOCKET_DEFAULT_PONG_TIMEOUT;

		settings->close_timeout = HTTP_WEB_SOCKET_DEFAULT_CLOSE_TIMEOUT;

		settings->send_timeout = 0;
	}

}

static void http_web_socket_settings_protocols_delete (HttpWebSocketSettings *settings) {

	if (settings->protocols) {
		for (unsigned int i = 0; i < settings->n_protocols; i++) {
			free (settings->protocols[i]);
		}

		free (settings->protocols);
	}

	settings->protocols = NULL;
	settings->n_protocols = 0;

}

// a subprotocol is a token (RFC 6455 section 4.1): a name with a separator or with a line end
// would be another element of the list or another line of the handshake
static bool http_web_socket_settings_protocols_are_valid (
	const char * const *protocols, const unsigned int n_protocols
) {

	bool valid = true;

	for (unsigned int i = 0; valid && (i < n_protocols); i++) {
		valid = protocols[i] && http_header_name_is_valid (protocols[i], strlen (protocols[i]));
	}

	return valid;

}

// copies the whole list or nothing of it
static u8 http_web_socket_settings_protocols_copy (
	HttpWebSocketSettings *settings,
	const char * const *protocols, const unsigned int n_protocols
) {

	u8 retval = 1;

	settings->protocols = (char **) calloc (n_protocols, sizeof (char *));
	if (settings->protocols) {
		retval = 0;

		for (unsigned int i = 0; !retval && (i < n_protocols); i++) {
			settings->protocols[i] = strdup (protocols[i]);

			if (settings->protocols[i]) settings->n_protocols = i + 1;
			else retval = 1;
		}

		if (retval) http_web_socket_settings_protocols_delete (settings);
	}

	return retval;

}

void http_web_socket_settings_end (HttpWebSocketSettings *settings) {

	if (settings && (settings->set & HTTP_WEB_SOCKET_SETTING_PROTOCOLS)) {
		http_web_socket_settings_protocols_delete (settings);
	}

}

void http_web_socket_settings_inherit (
	HttpWebSocketSettings *resolved, const HttpWebSocketSettings *source
) {

	if (resolved && source) {
		const unsigned int missing = source->set & ~resolved->set;

		if (missing & HTTP_WEB_SOCKET_SETTING_HANDLERS) resolved->handlers = source->handlers;

		if (missing & HTTP_WEB_SOCKET_SETTING_MAX_MESSAGE) resolved->max_message = source->max_message;

		if (missing & HTTP_WEB_SOCKET_SETTING_PROTOCOLS) {
			resolved->protocols = source->protocols;
			resolved->n_protocols = source->n_protocols;
		}

		if (missing & HTTP_WEB_SOCKET_SETTING_COMPRESSION) resolved->compression = source->compression;

		if (missing & HTTP_WEB_SOCKET_SETTING_ORIGIN) resolved->origin_check = source->origin_check;

		if (missing & HTTP_WEB_SOCKET_SETTING_PING) {
			resolved->ping_interval = source->ping_interval;
			resolved->pong_timeout = source->pong_timeout;
		}

		if (missing & HTTP_WEB_SOCKET_SETTING_CLOSE_TIMEOUT) resolved->close_timeout = source->close_timeout;

		if (missing & HTTP_WEB_SOCKET_SETTING_SEND_TIMEOUT) resolved->send_timeout = source->send_timeout;

		resolved->set |= missing;
	}

}

void http_web_socket_settings_set_handlers (
	HttpWebSocketSettings *settings, const HttpWebSocketHandlers *handlers
) {

	if (settings && handlers) {
		settings->handlers = *handlers;
		settings->set |= HTTP_WEB_SOCKET_SETTING_HANDLERS;
	}

}

void http_web_socket_settings_set_max_message (
	HttpWebSocketSettings *settings, const size_t max_message
) {

	if (settings) {
		settings->max_message = max_message ? max_message : HTTP_WEB_SOCKET_DEFAULT_MAX_MESSAGE;
		settings->set |= HTTP_WEB_SOCKET_SETTING_MAX_MESSAGE;
	}

}

u8 http_web_socket_settings_set_protocols (
	HttpWebSocketSettings *settings,
	const char * const *protocols, const unsigned int n_protocols
) {

	u8 retval = 1;

	// a list that is rejected does not change the settings
	if (
		settings && (protocols || !n_protocols) && (n_protocols <= HTTP_WEB_SOCKET_MAX_PROTOCOLS)
		&& http_web_socket_settings_protocols_are_valid (protocols, n_protocols)
	) {
		if (settings->set & HTTP_WEB_SOCKET_SETTING_PROTOCOLS) {
			http_web_socket_settings_protocols_delete (settings);
		}

		settings->set |= HTTP_WEB_SOCKET_SETTING_PROTOCOLS;

		retval = n_protocols ? http_web_socket_settings_protocols_copy (settings, protocols, n_protocols) : 0;
	}

	return retval;

}

void http_web_socket_settings_set_compression (
	HttpWebSocketSettings *settings, const HttpWebSocketDeflateOptions *compression
) {

	if (settings && compression) {
		settings->compression = *compression;
		settings->set |= HTTP_WEB_SOCKET_SETTING_COMPRESSION;
	}

}

void http_web_socket_settings_set_origin_check (
	HttpWebSocketSettings *settings, const bool origin_check
) {

	if (settings) {
		settings->origin_check = origin_check;
		settings->set |= HTTP_WEB_SOCKET_SETTING_ORIGIN;
	}

}

void http_web_socket_settings_set_ping_milliseconds (
	HttpWebSocketSettings *settings,
	const unsigned int ping_interval, const unsigned int pong_timeout
) {

	if (settings) {
		settings->ping_interval = ping_interval;
		settings->pong_timeout = pong_timeout ? pong_timeout : HTTP_WEB_SOCKET_DEFAULT_PONG_TIMEOUT;
		settings->set |= HTTP_WEB_SOCKET_SETTING_PING;
	}

}

void http_web_socket_settings_set_close_timeout_milliseconds (
	HttpWebSocketSettings *settings, const unsigned int close_timeout
) {

	if (settings) {
		settings->close_timeout = close_timeout ? close_timeout : HTTP_WEB_SOCKET_DEFAULT_CLOSE_TIMEOUT;
		settings->set |= HTTP_WEB_SOCKET_SETTING_CLOSE_TIMEOUT;
	}

}

void http_web_socket_settings_set_send_timeout_milliseconds (
	HttpWebSocketSettings *settings, const unsigned int send_timeout
) {

	if (settings) {
		settings->send_timeout = send_timeout;
		settings->set |= HTTP_WEB_SOCKET_SETTING_SEND_TIMEOUT;
	}

}
