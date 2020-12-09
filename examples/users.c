#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include <cerver/types/string.h>

#include <cerver/collections/dlist.h>

#include <cerver/http/json/json.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

#include "users.h"

static DoubleList *users = NULL;

User *user_new (void) {

	User *user = (User *) malloc (sizeof (User));
	if (user) {
		user->id = NULL;
		user->name = NULL;
		user->username = NULL;
		user->password = NULL;
		user->role = NULL;
		user->iat = 0;
	}

	return user;

}

void user_delete (void *user_ptr) {

	if (user_ptr) {
		User *user = (User *) user_ptr;

		str_delete (user->id);
		str_delete (user->name);
		str_delete (user->username);
		str_delete (user->password);
		str_delete (user->role);

		free (user_ptr);

		printf ("user_delete () - User has been deleted!\n");
	}

}

int user_comparator (const void *a, const void *b) {

	return strcmp (((User *) a)->username->str, ((User *) b)->username->str);

}

void user_print (User *user) {

	if (user) {
		(void) printf ("id: %s\n", user->id->str);
		(void) printf ("name: %s\n", user->name->str);
		(void) printf ("username: %s\n", user->username->str);
		(void) printf ("role: %s\n", user->role->str);
	}

}

User *user_get_by_username (const char *username) {

	User *retval = NULL;

	for (ListElement *le = dlist_start (users); le; le = le->next) {
		if (!strcmp (((User *) le->data)->username->str, username)) {
			retval = (User *) le->data;
			break;
		}
	}

	return retval;

}

void user_add (User *user) {

	dlist_insert_after (users, dlist_end (users), user);

}

// {
//   "iat": 1596532954,
//   "id": "5eb2b13f0051f70011e9d3af",
//   "name": "Erick Salas",
//   "role": "common",
//   "username": "erick"
// }
void *user_parse_from_json (void *user_json_ptr) {

	json_t *user_json = (json_t *) user_json_ptr;

	User *user = user_new ();
	if (user) {
		const char *id = NULL;
		const char *name = NULL;
		const char *role = NULL;
		const char *username = NULL;

		if (!json_unpack (
			user_json,
			"{s:i, s:s, s:s, s:s, s:s}",
			"iat", &user->iat,
			"id", &id,
			"name", &name,
			"role", &role,
			"username", &username
		)) {
			user->id = str_new (id);
			user->name = str_new (name);
			user->username = str_new (username);
			user->role = str_new (role);

			user_print (user);
		}

		else {
			cerver_log_error (
				"user_parse_from_json () - json_unpack () has failed!"
			);

			user_delete (user);
			user = NULL;
		}
	}

	return user;

}

void users_init (void) {

	users = dlist_init (user_delete, user_comparator);

}

void users_end (void) {

	dlist_delete (users);

}