#include <stdbool.h>

#include "cerver/http/requests/method.h"

static const char *request_method_undefined = { "Undefined" };

const char *cerver_http_method_to_string (
	const CerverHttpMethod request_method
) {

	switch (request_method) {
		#define XX(num, name, string) case REQUEST_METHOD_##name: return #string;
		REQUEST_METHOD_MAP(XX)
		#undef XX

		default: break;
	}

	return request_method_undefined;

}

// returns true if the method is one of REQUEST_METHOD_MAP
bool cerver_http_method_is_valid (
	const CerverHttpMethod request_method
) {

	switch (request_method) {
		#define XX(num, name, string) case REQUEST_METHOD_##name: return true;
		REQUEST_METHOD_MAP(XX)
		#undef XX

		default: break;
	}

	return false;

}
