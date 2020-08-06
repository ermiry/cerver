#include "cerver/http/status.h"

const char *http_status_str (enum http_status s) {

	switch (s) {
		#define XX(num, name, string) case HTTP_STATUS_##name: return #string;
		HTTP_STATUS_MAP(XX)
		#undef XX

		default: return "<unknown>";
	}

}