#include "cerver/http/responses/status.h"

const char *cerver_http_status_to_string (const CerverHttpStatus status) {

	switch (status) {
		#define XX(num, name, string) case HTTP_STATUS_##name: return #string;
		HTTP_STATUS_MAP(XX)
		#undef XX
	}

	return cerver_http_status_to_string (HTTP_STATUS_NONE);

}
