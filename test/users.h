#ifndef _CERVER_TESTS_USERS_H_
#define _CERVER_TESTS_USERS_H_

#include <time.h>

#include <cerver/types/string.h>

typedef struct User {

	String *id;
	String *name;
	String *username;
	String *password;
	String *role;
	time_t iat;

} User;

extern User *user_new (void);

extern void user_delete (void *user_ptr);

extern int user_comparator (const void *a, const void *b);

extern void user_print (User *user);

extern User *user_get_by_username (const char *username);

extern void user_add (User *user);

// {
//   "iat": 1596532954,
//   "id": "5eb2b13f0051f70011e9d3af",
//   "name": "Erick Salas",
//   "role": "common",
//   "username": "erick"
// }
extern void *user_parse_from_json (void *user_json_ptr);

extern void users_init (void);

extern void users_end (void);

#endif