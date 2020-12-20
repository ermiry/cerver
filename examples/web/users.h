#ifndef _USERS_ROUTES_H_
#define _USERS_ROUTES_H_

struct _HttpReceive;
struct _HttpRequest;

// GET /api/users
extern void main_users_handler (
	const struct _HttpReceive *http_receive,
	const struct _HttpRequest *request
);

// POST /api/users/login
extern void users_login_handler (
	const struct _HttpReceive *http_receive,
	const struct _HttpRequest *request
);

// POST /api/users/register
extern void users_register_handler (
	const struct _HttpReceive *http_receive,
	const struct _HttpRequest *request
);

// GET /api/users/profile
extern void users_profile_handler (
	const struct _HttpReceive *http_receive,
	const struct _HttpRequest *request
);

#endif