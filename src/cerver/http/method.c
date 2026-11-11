#include <stdbool.h>

#include "cerver/http/method.h"

static const char *request_method_undefined = { "Undefined" };

const char *http_request_method_str (
	const RequestMethod request_method
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
bool http_request_method_is_valid (
	const RequestMethod request_method
) {

	switch (request_method) {
		#define XX(num, name, string) case REQUEST_METHOD_##name: return true;
		REQUEST_METHOD_MAP(XX)
		#undef XX

		default: break;
	}

	return false;

}
