#include "cerver/http/status.h"

const char *http_status_string (const http_status status) {

	switch (status) {
		#define XX(num, name, string) case HTTP_STATUS_##name: return #string;
		HTTP_STATUS_MAP(XX)
		#undef XX
	}

	return http_status_string (HTTP_STATUS_NONE);

}