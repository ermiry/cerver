#include <stdbool.h>
#include <string.h>

#include "cerver/http/requests/method.h"

#ifdef __cplusplus
	#define REQUEST_METHOD_STATIC_ASSERT(condition, message)		static_assert (condition, message)
#else
	#define REQUEST_METHOD_STATIC_ASSERT(condition, message)		_Static_assert (condition, message)
#endif

// every value of the table indexes the tables of REQUEST_METHOD_SIZE, and the highest one is the last place of them:
// each method is a member of one byte, and of two in the struct of a check when the method is marked by it
#define REQUEST_METHOD_CHECK_SIZE				1
#define REQUEST_METHOD_MARKED_SIZE				2

#define REQUEST_METHOD_MEMBER(num, name, string)		char method_##name[REQUEST_METHOD_CHECK_SIZE];
#define REQUEST_METHOD_OUTSIDE(num, name, string)		\
	char method_##name[((num) >= REQUEST_METHOD_SIZE) ? REQUEST_METHOD_MARKED_SIZE : REQUEST_METHOD_CHECK_SIZE];
#define REQUEST_METHOD_HIGHEST(num, name, string)		\
	char method_##name[((num) == (REQUEST_METHOD_SIZE - 1)) ? REQUEST_METHOD_MARKED_SIZE : REQUEST_METHOD_CHECK_SIZE];

struct RequestMethodMembers { REQUEST_METHOD_MAP (REQUEST_METHOD_MEMBER) };
struct RequestMethodOutside { REQUEST_METHOD_MAP (REQUEST_METHOD_OUTSIDE) };
struct RequestMethodHighest { REQUEST_METHOD_MAP (REQUEST_METHOD_HIGHEST) };

REQUEST_METHOD_STATIC_ASSERT (
	sizeof (struct RequestMethodOutside) == sizeof (struct RequestMethodMembers),
	"a method of REQUEST_METHOD_MAP does not fit in REQUEST_METHOD_SIZE"
);

REQUEST_METHOD_STATIC_ASSERT (
	sizeof (struct RequestMethodHighest) == (sizeof (struct RequestMethodMembers) + 1),
	"REQUEST_METHOD_SIZE is not the highest method of REQUEST_METHOD_MAP + 1"
);

static const char *request_method_undefined = { "Undefined" };

const char *http_request_method_to_string (
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

RequestMethod http_request_method_from_string (const char *method) {

	RequestMethod request_method = REQUEST_METHOD_UNDEFINED;

	// the methods are case sensitive (RFC 9110 section 9.1)
	if (method) {
		#define XX(num, name, string) \
			if ((request_method == REQUEST_METHOD_UNDEFINED) && !strcmp (method, #string)) request_method = REQUEST_METHOD_##name;
		REQUEST_METHOD_MAP(XX)
		#undef XX
	}

	return request_method;

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
