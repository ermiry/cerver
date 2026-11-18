#ifndef _TEST_APP_ROUTES_H_
#define _TEST_APP_ROUTES_H_

#include <cerver/http/core/context.h>

// GET /api/users
extern void main_users_handler (CerverHttpContext *context);

// POST /api/users/login
extern void users_login_handler (CerverHttpContext *context);

// POST /api/users/register
extern void users_register_handler (CerverHttpContext *context);

// GET /api/users/profile
extern void users_profile_handler (CerverHttpContext *context);

#endif
