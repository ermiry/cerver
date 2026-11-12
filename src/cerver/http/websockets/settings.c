#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

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

	if (settings && (protocols || !n_protocols) && (n_protocols <= HTTP_WEB_SOCKET_MAX_PROTOCOLS)) {
		if (settings->set & HTTP_WEB_SOCKET_SETTING_PROTOCOLS) {
			http_web_socket_settings_protocols_delete (settings);
		}

		settings->set |= HTTP_WEB_SOCKET_SETTING_PROTOCOLS;
		retval = 0;

		if (n_protocols) {
			settings->protocols = (char **) calloc (n_protocols, sizeof (char *));
			if (settings->protocols) {
				for (unsigned int i = 0; !retval && (i < n_protocols); i++) {
					settings->protocols[i] = protocols[i] ? strdup (protocols[i]) : NULL;
					settings->n_protocols = i + 1;

					if (!settings->protocols[i]) retval = 1;
				}
			}

			else {
				retval = 1;
			}
		}
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

void http_web_socket_settings_set_ping (
	HttpWebSocketSettings *settings,
	const unsigned int ping_interval, const unsigned int pong_timeout
) {

	if (settings) {
		settings->ping_interval = ping_interval;
		settings->pong_timeout = pong_timeout ? pong_timeout : HTTP_WEB_SOCKET_DEFAULT_PONG_TIMEOUT;
		settings->set |= HTTP_WEB_SOCKET_SETTING_PING;
	}

}

void http_web_socket_settings_set_close_timeout (
	HttpWebSocketSettings *settings, const unsigned int close_timeout
) {

	if (settings) {
		settings->close_timeout = close_timeout ? close_timeout : HTTP_WEB_SOCKET_DEFAULT_CLOSE_TIMEOUT;
		settings->set |= HTTP_WEB_SOCKET_SETTING_CLOSE_TIMEOUT;
	}

}

void http_web_socket_settings_set_send_timeout (
	HttpWebSocketSettings *settings, const unsigned int send_timeout
) {

	if (settings) {
		settings->send_timeout = send_timeout;
		settings->set |= HTTP_WEB_SOCKET_SETTING_SEND_TIMEOUT;
	}

}
