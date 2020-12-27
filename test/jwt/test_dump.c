#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>

#include <cerver/http/jwt/jwt.h>

#include "../test.h"

void test_jwt_dump_fp (void) {

	FILE *out = NULL;
	jwt_t *jwt = NULL;
	int ret = 0;

	ret = jwt_new (&jwt);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant(jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int(jwt, "iat", (long)time(NULL));
	test_check_int_eq (ret, 0, NULL);

	out = fopen ("/dev/null", "w");
	test_check (out != NULL, NULL);

	ret = jwt_dump_fp (jwt, out, 1);
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_dump_fp (jwt, out, 0);
	test_check_int_eq (ret, 0, NULL);

	(void) fclose (out);

	jwt_free (jwt);

}

void test_jwt_dump_str (void) {

	jwt_t *jwt = NULL;
	int ret = 0;
	char *out = NULL;
	const char *val = NULL;

	ret = jwt_new (&jwt);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", (long) time (NULL));
	test_check_int_eq (ret, 0, NULL);

	/* Test 'typ' header: should not be present,
	cause 'alg' is JWT_ALG_NONE. */
	val = jwt_get_header (jwt, "typ");
	test_check (val == NULL, NULL);

	out = jwt_dump_str (jwt, 1);
	test_check (out != NULL, NULL);

	/* Test 'typ' header: should not be present,
	cause 'alg' is JWT_ALG_NONE. */
	val = jwt_get_header (jwt, "typ");
	test_check (val == NULL, NULL);

	jwt_free_str (out);

	out = jwt_dump_str (jwt, 0);
	test_check (out != NULL, NULL);

	/* Test 'typ' header: should not be present,
	cause 'alg' is JWT_ALG_NONE. */
	val = jwt_get_header (jwt, "typ");
	test_check (val == NULL, NULL);

	jwt_free_str(out);

	jwt_free(jwt);

}

void test_jwt_dump_str_alg_default_typ_header (void) {

	jwt_t *jwt = NULL;
	const char key[] = "My Passphrase";
	int ret = 0;
	char *out = NULL;
	const char *val = NULL;

	ret = jwt_new(&jwt);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", (long)time(NULL));
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_set_alg (
		jwt, JWT_ALG_HS256,
		(unsigned char *) key, strlen(key)
	);

	test_check_int_eq (ret, 0, NULL);

	/* Test 'typ' header: should not be present, cause jwt's header has not been touched yet
	 * by jwt_write_head, this is only called as a result of calling jwt_dump* methods. */
	val = jwt_get_header (jwt, "typ");
	test_check (val == NULL, NULL);

	out = jwt_dump_str(jwt, 1);
	test_check (out != NULL, NULL);

	/* Test 'typ' header: should be added with default value of 'JWT', cause 'alg' is set explicitly
	 * and jwt's header has been processed by jwt_write_head. */
	val = jwt_get_header (jwt, "typ");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "JWT", NULL);

	jwt_free_str (out);

	out = jwt_dump_str (jwt, 0);
	test_check (out != NULL, NULL);

	/* Test 'typ' header: should be added with default value of 'JWT', cause 'alg' is set explicitly
	 * and jwt's header has been processed by jwt_write_head. */
	val = jwt_get_header (jwt, "typ");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "JWT", NULL);

	jwt_free_str (out);

	jwt_free (jwt);

}

void test_jwt_dump_str_alg_custom_typ_header (void) {

	jwt_t *jwt = NULL;
	const char key[] = "My Passphrase";
	int ret = 0;
	char *out;
	const char *val = NULL;

	ret = jwt_new (&jwt);
	test_check_int_eq (ret, 0, NULL);
	test_check (jwt != NULL, NULL);

	ret = jwt_add_grant (jwt, "iss", "files.cyphre.com");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "sub", "user0");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant (jwt, "ref", "XXXX-YYYY-ZZZZ-AAAA-CCCC");
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_grant_int (jwt, "iat", (long)time(NULL));
	test_check_int_eq (ret, 0, NULL);

	ret = jwt_add_header (jwt, "typ", "favourite");
	test_check_int_eq (ret, 0, NULL);

	/* Test that 'typ' header has been added. */
	val = jwt_get_header (jwt, "typ");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "favourite", NULL);

	ret = jwt_set_alg (
		jwt, JWT_ALG_HS256,
		(unsigned char *) key, strlen (key)
	);

	test_check_int_eq (ret, 0, NULL);

	/* Test 'typ' header: should be left untouched. */
	val = jwt_get_header( jwt, "typ");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "favourite", NULL);

	out = jwt_dump_str (jwt, 1);
	test_check (out != NULL, NULL);

	/* Test 'typ' header: should be left untouched. */
	val = jwt_get_header (jwt, "typ");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "favourite", NULL);

	jwt_free_str (out);

	out = jwt_dump_str (jwt, 0);
	test_check (out != NULL, NULL);

	/* Test 'typ' header: should be left untouched. */
	val = jwt_get_header (jwt, "typ");
	test_check (val != NULL, NULL);
	test_check_str_eq (val, "favourite", NULL);

	jwt_free_str(out);

	jwt_free(jwt);

}

void jwt_tests_dump (void) {

	(void) printf ("Testing JWT dump...\n");

	test_jwt_dump_fp ();

	test_jwt_dump_str ();

	test_jwt_dump_str_alg_default_typ_header ();

	test_jwt_dump_str_alg_custom_typ_header ();

	(void) printf ("Done!\n");

}
