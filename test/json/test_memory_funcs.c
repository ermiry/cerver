#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/json/json.h>

#include "json.h"
#include "errors.h"

static void create_and_free_complex_object (void) {

	json_t *obj = json_pack (
		"{s:i,s:n,s:b,s:b,s:{s:s},s:[i,i,i]}",
		"foo", 42, "bar", "baz", 1,
		"qux", 0, "alice", "bar", "baz", "bob", 9, 8, 7
	);

	json_decref (obj);

}

static void create_and_free_object_with_oom (void) {

	int i = 0;
	char key[4] = { 0 };
	json_t *obj = json_object();

	for (i = 0; i < 10; i++) {
		(void) snprintf (key, sizeof key, "%d", i);
		json_object_set_new (obj, key, json_integer (i));
	}

	json_decref (obj);
}

static void test_simple (void) {

	create_and_free_complex_object();

}

static void test_oom (void) {

	create_and_free_object_with_oom ();

}

void json_tests_memory (void) {

	(void) printf ("Testing JSON memory...\n");

	test_simple ();
	test_oom ();

	(void) printf ("Done!\n");

}