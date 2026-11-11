#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/types/string.h>

#include "../test.h"

#define STRING_TEST_GROWTH_APPENDS			(1 << 16)
#define STRING_TEST_GROWTH_MAX_MOVES		(1 << 6)
#define STRING_TEST_RESERVE					(1 << 8)

static const char *string_test_byte = "x";

static void test_str_new (void) {

	String *empty = str_new (NULL);
	test_check_ptr (empty);
	test_check_unsigned_eq (empty->max_len, 0, NULL);
	test_check_unsigned_eq (empty->len, 0, NULL);
	test_check_null_ptr (empty->str);

	str_delete (empty);

	const char *small = { "small" };
	String *str = str_new (small);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (small), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, small, NULL);

	str_delete (str);

	const char *large = { "I am a large string to be tested! "};
	str = str_new (large);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (large), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, large, NULL);

	str_delete (str);

}

static void test_str_create (void) {

	const char *name = "Erick Salas";
	const unsigned int old = 22;
	const char *result = { "I am Erick Salas and I am 22 years old!" };

	String *str = str_create ("I am %s and I am %u years old!", name, old);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (result), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, result, NULL);

	str_delete (str);

}

static void test_str_allocate (void) {

	const unsigned int str_size = 1024;

	String *str = str_allocate (str_size);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, str_size, NULL);
	test_check_unsigned_eq (str->len, 0, NULL);
	test_check_ptr (str->str);
	test_check_str_empty (str->str);

	str_delete (str);

}

static void test_str_set (void) {

	const unsigned int str_size = 1024;

	String *str = str_allocate (str_size);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, str_size, NULL);
	test_check_unsigned_eq (str->len, 0, NULL);
	test_check_ptr (str->str);
	test_check_str_empty (str->str);

	const char *name = "Erick Salas";
	const unsigned int old = 22;
	const char *result = { "I am Erick Salas and I am 22 years old!" };

	str_set (str, "I am %s and I am %u years old!", name, old);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, str_size, NULL);
	test_check_unsigned_eq (str->len, strlen (result), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, result, NULL);

	str_delete (str);

}

static void test_str_compare (void) {

	String *one = str_new ("Hola me llamo Erick");
	String *two = str_new ("My name is Erick Salas");
	String *three = str_new ("Hola me llamo Erick");

	test_check_bool_eq ((str_compare (one, two) != 0), true, NULL);
	test_check_bool_eq ((!str_compare (one, three)), true, NULL);

	str_delete (one);
	str_delete (two);
	str_delete (three);

}

static void test_str_comparator (void) {

	String *one = str_new ("Hola me llamo Erick");
	String *two = str_new ("My name is Erick Salas");
	String *three = str_new ("Hola me llamo Erick");

	test_check_bool_eq ((str_comparator (one, two) != 0), true, NULL);
	test_check_bool_eq ((!str_comparator (one, three)), true, NULL);

	str_delete (one);
	str_delete (two);
	str_delete (three);

}

static void test_str_copy (void) {

	const char *name = "Erick Salas";

	String *source = str_new ("Hola me llamo Erick Salas");

	String *empty = str_new (NULL);
	test_check_ptr (empty);
	test_check_unsigned_eq (empty->max_len, 0, NULL);
	test_check_unsigned_eq (empty->len, 0, NULL);
	test_check_null_ptr (empty->str);

	str_copy (empty, source);

	test_check_unsigned_eq (empty->max_len, 0, NULL);
	test_check_unsigned_eq (empty->len, source->len, NULL);
	test_check_ptr (empty->str);
	test_check_str_eq (empty->str, source->str, NULL);

	str_delete (empty);

	String *str = str_new (name);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (name), NULL);
	test_check_ptr (str->str);

	str_copy (str, source);

	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, source->len, NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, source->str, NULL);

	str_delete (str);

	str_delete (source);

}

static void test_str_n_copy (void) {

	const char *name = "Erick Salas";
	const char *full = "Hola me llamo Erick Salas";

	const char *to_copy = "Hola me llamo";
	const size_t to_copy_len = strlen (to_copy);

	String *source = str_new (full);

	String *empty = str_new (NULL);
	test_check_ptr (empty);
	test_check_unsigned_eq (empty->max_len, 0, NULL);
	test_check_unsigned_eq (empty->len, 0, NULL);
	test_check_null_ptr (empty->str);

	str_n_copy (empty, source, to_copy_len);

	test_check_unsigned_eq (empty->max_len, 0, NULL);
	test_check_unsigned_eq (empty->len, to_copy_len, NULL);
	test_check_ptr (empty->str);
	test_check_str_eq (empty->str, to_copy, NULL);

	str_delete (empty);

	String *str = str_new (name);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (name), NULL);
	test_check_ptr (str->str);

	str_n_copy (str, source, to_copy_len);

	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, to_copy_len, NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, to_copy, NULL);

	str_delete (str);

	str_delete (source);

}

static void test_str_clone (void) {

	String *source = str_new ("Hola me llamo Erick Salas");

	String *str = str_clone (source);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, source->len, NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, source->str, NULL);

	str_delete (str);

	str_delete (source);

}

static void test_str_replace (void) {

	const char *name = "Erick Salas";
	const unsigned int old = 22;
	const char *result = { "I am Erick Salas and I am 22 years old!" };

	String *str = str_create ("%s", name);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (name), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, name, NULL);

	str_replace (str, "I am %s and I am %u years old!", name, old);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (result), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, result, NULL);

	str_delete (str);

}

static void test_str_replace_with (void) {

	const char *name = "Erick Salas";
	const char *result = { "I am Erick Salas and I am 22 years old!" };

	String *str = str_create ("%s", name);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (name), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, name, NULL);

	str_replace_with (str, result);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (result), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, result, NULL);

	str_delete (str);

}

static void test_str_n_replace_with (void) {

	const char *name = "Erick Salas";
	const char *replace = { "I am Erick Salas and I am 22 years old!" };
	const char *result = { "I am Erick Salas" };

	String *str = str_create ("%s", name);
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (name), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, name, NULL);

	str_n_replace_with (str, replace, strlen (result));
	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (result), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, result, NULL);

	str_delete (str);

}

static void test_str_concat (void) {

	String *first = str_new ("I am Erick Salas");
	String *second = str_new (" and I am 22 years old!");
	const char *result = { "I am Erick Salas and I am 22 years old!" };

	String *str = str_concat (first, second);

	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (result), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, result, NULL);

	str_delete (str);

	str_delete (first);
	str_delete (second);

}

static void test_str_append_char (void) {

	const char *result = { "I am Erick Salas!" };

	String *str = str_new ("I am Erick Salas");

	str_append_char (str, '!');

	test_check_ptr (str);
	test_check (str->max_len > str->len, NULL);
	test_check_unsigned_eq (str->len, strlen (result), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, result, NULL);

	str_delete (str);

}

static void test_str_append_c_string (void) {

	const char *first = { "I am Erick Salas" };
	const char *second = { " and I am 22 years old!" };
	const char *result = { "I am Erick Salas and I am 22 years old!" };

	String *str = str_new (first);

	str_append_c_string (str, second);

	test_check_ptr (str);
	test_check (str->max_len > str->len, NULL);
	test_check_unsigned_eq (str->len, strlen (result), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, result, NULL);

	str_delete (str);

}

static void test_str_append_n_from_c_string (void) {

	const char *first = { "Hola a todos" };
	const char *second = { " me llamo Erick Salas!" };
	const size_t to_append = 9;
	const char *result = { "Hola a todos me llamo" };

	String *str = str_new (first);

	str_append_n_from_c_string (str, second, to_append);

	test_check_ptr (str);
	test_check (str->max_len > str->len, NULL);
	test_check_unsigned_eq (str->len, strlen (result), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, result, NULL);

	// the terminator must be written on every append
	test_check_int_eq ((int) str->str[str->len], 0, NULL);

	str_delete (str);

}

// a body that arrives in many pieces is built with many appends
static void test_str_append_n_from_c_string_many (void) {

	const char *body = { "{\"name\": \"Erick Salas\", \"value\": 1234567890}" };
	const size_t body_len = strlen (body);

	// start from an empty string, as the HTTP body does
	String *str = str_new (NULL);
	test_check_ptr (str);

	for (size_t i = 0; i < body_len; i += 3) {
		const size_t n = ((body_len - i) < 3) ? (body_len - i) : 3;
		str_append_n_from_c_string (str, body + i, n);

		test_check_unsigned_eq (str->len, i + n, NULL);
		test_check_int_eq ((int) str->str[str->len], 0, NULL);
	}

	test_check_str_eq (str->str, body, NULL);
	test_check_unsigned_eq (str->len, body_len, NULL);

	str_delete (str);

}

static void test_str_create_from_bytes (void) {

	const char bytes[] = { 'a', '\0', 'b', '\0', 'c' };

	String *str = str_create_from_bytes (bytes, sizeof (bytes));
	test_check_ptr (str);
	test_check_ptr (str->str);
	test_check_unsigned_eq (str->len, sizeof (bytes), NULL);
	test_check_int_eq (memcmp (str->str, bytes, sizeof (bytes)), 0, NULL);
	test_check_int_eq ((int) str->str[str->len], 0, NULL);
	str_delete (str);

	// an empty chunk creates an empty but valid string
	String *empty = str_create_from_bytes (bytes, 0);
	test_check_ptr (empty);
	test_check_ptr (empty->str);
	test_check_unsigned_eq (empty->len, 0, NULL);
	test_check_str_eq (empty->str, "", NULL);
	str_delete (empty);

}

static void test_str_append_bytes (void) {

	const char first[] = { 'x', '\0', 'y' };
	const char second[] = { '\0', 'z' };
	const char result[] = { 'x', '\0', 'y', '\0', 'z' };

	String *str = str_create_from_bytes (first, sizeof (first));
	test_check_ptr (str);

	test_check_int_eq (str_append_bytes (str, second, sizeof (second)), 0, NULL);
	test_check_unsigned_eq (str->len, sizeof (result), NULL);
	test_check_int_eq (memcmp (str->str, result, sizeof (result)), 0, NULL);
	test_check_int_eq ((int) str->str[str->len], 0, NULL);

	// appending nothing keeps the string unchanged
	test_check_int_eq (str_append_bytes (str, NULL, 0), 0, NULL);
	test_check_unsigned_eq (str->len, sizeof (result), NULL);

	// bad arguments
	test_check_int_eq (str_append_bytes (NULL, second, sizeof (second)), 1, NULL);
	test_check_int_eq (str_append_bytes (str, NULL, 1), 1, NULL);

	str_delete (str);

}

// appends that fit in the capacity never move the string
static void test_str_append_bytes_growth (void) {

	String *str = str_new (NULL);
	test_check_ptr (str);

	size_t moves = 0;
	const char *previous = NULL;
	for (size_t i = 0; i < STRING_TEST_GROWTH_APPENDS; i++) {
		test_check_int_eq (str_append_bytes (str, string_test_byte, 1), 0, NULL);
		test_check (str->max_len > str->len, NULL);

		if (str->str != previous) moves++;
		previous = str->str;
	}

	test_check_unsigned_eq (str->len, STRING_TEST_GROWTH_APPENDS, NULL);
	test_check_int_eq ((int) str->str[str->len], 0, NULL);

	// a geometric growth, not one realloc for every append
	test_check (moves < STRING_TEST_GROWTH_MAX_MOVES, "the string was reallocated in too many appends");

	str_delete (str);

}

static void test_str_reserve (void) {

	String *str = str_new (NULL);
	test_check_ptr (str);

	test_check_int_eq (str_reserve (str, STRING_TEST_RESERVE), 0, NULL);
	test_check_ptr (str->str);
	test_check_unsigned_eq (str->max_len, STRING_TEST_RESERVE, NULL);
	test_check_unsigned_eq (str->len, 0, NULL);
	test_check_str_eq (str->str, "", NULL);

	// appends inside the reserved bytes keep the same buffer
	const char *reserved = str->str;
	for (size_t i = 0; i < (STRING_TEST_RESERVE - 1); i++) {
		test_check_int_eq (str_append_bytes (str, string_test_byte, 1), 0, NULL);
	}

	test_check_ptr_eq (str->str, reserved);
	test_check_unsigned_eq (str->max_len, STRING_TEST_RESERVE, NULL);

	// it never shrinks
	test_check_int_eq (str_reserve (str, 1), 0, NULL);
	test_check_unsigned_eq (str->max_len, STRING_TEST_RESERVE, NULL);

	test_check_int_eq (str_reserve (NULL, STRING_TEST_RESERVE), 1, NULL);

	str_delete (str);

}

// a new buffer resets the capacity, so the next append can not overflow it
static void test_str_capacity_reset (void) {

	String *str = str_new (NULL);
	test_check_int_eq (str_reserve (str, STRING_TEST_RESERVE), 0, NULL);
	str_replace_with (str, "Erick Salas");
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_int_eq (str_append_bytes (str, string_test_byte, 1), 0, NULL);
	test_check_str_eq (str->str, "Erick Salasx", NULL);
	str_delete (str);

	str = str_new (NULL);
	test_check_int_eq (str_reserve (str, STRING_TEST_RESERVE), 0, NULL);
	str_n_replace_with (str, "Erick Salas", 5);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	str_delete (str);

	str = str_new (NULL);
	test_check_int_eq (str_reserve (str, STRING_TEST_RESERVE), 0, NULL);
	str_replace (str, "%s", "Erick Salas");
	test_check_unsigned_eq (str->max_len, 0, NULL);
	str_delete (str);

	String *from = str_new ("Erick Salas");
	str = str_new (NULL);
	test_check_int_eq (str_reserve (str, STRING_TEST_RESERVE), 0, NULL);
	str_copy (str, from);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_int_eq (str_reserve (str, STRING_TEST_RESERVE), 0, NULL);
	str_n_copy (str, from, 5);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_int_eq (str_reserve (str, STRING_TEST_RESERVE), 0, NULL);
	str_remove_last_char (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_str_eq (str->str, "Eric", NULL);
	str_delete (str);
	str_delete (from);

}

static void test_str_to_upper (void) {

	const char *up = "HOLA ME LLAMO ERICK SALAS";

	String *str = str_new ("Hola me llamo Erick Salas");

	str_to_upper (str);

	test_check_ptr (str->str);
	test_check_str_eq (str->str, up, NULL);
	test_check_str_len (str->str, strlen (up), NULL);
	test_check_unsigned_eq (str->len, strlen (up), NULL);

	str_delete (str);

}

static void test_str_to_lower (void) {

	const char *low = "hola me llamo erick salas";

	String *str = str_new ("Hola me llamo Erick Salas");

	str_to_lower (str);

	test_check_ptr (str->str);
	test_check_str_eq (str->str, low, NULL);
	test_check_str_len (str->str, strlen (low), NULL);
	test_check_unsigned_eq (str->len, strlen (low), NULL);

	str_delete (str);

}

static void test_str_remove_char (void) {

	const char garbage = 'a';
	const char *clean = "Hol me llmo Erick Sls";

	String *str = str_new ("Hola me llamo Erick Salas");
	str_remove_char (str, garbage);

	test_check_ptr (str->str);
	test_check_str_eq (str->str, clean, NULL);
	test_check_str_len (str->str, strlen (clean), NULL);
	test_check_unsigned_eq (str->len, strlen (clean), NULL);

	str_delete (str);

}

static void test_str_remove_last_char (void) {

	const char *result = { "I am Erick Salas" };

	String *str = str_new ("I am Erick Salas!");

	str_remove_last_char (str);

	test_check_ptr (str);
	test_check_unsigned_eq (str->max_len, 0, NULL);
	test_check_unsigned_eq (str->len, strlen (result), NULL);
	test_check_ptr (str->str);
	test_check_str_eq (str->str, result, NULL);

	str_delete (str);

}

static void test_str_contains (void) {

	const char *to_find = "Erick Salas";

	String *one = str_new ("Hola");
	test_check_int_eq (str_contains (one, to_find), -1, NULL);
	str_delete (one);

	String *two = str_new ("Erick Salas");
	test_check_int_eq (str_contains (two, to_find), 0, NULL);
	str_delete (two);

	String *three = str_new ("Hola me llamo Erick");
	test_check_int_eq (str_contains (three, to_find), 1, NULL);
	str_delete (three);

	String *four = str_new ("Hola me llamo Erick Salas");
	test_check_int_eq (str_contains (four, to_find), 0, NULL);
	str_delete (four);

	// String *five = str_new ("Hola a todos como estan?");
	// test_check_int_eq (str_contains (five, to_find), -1, NULL);
	// str_delete (five);

}

void types_tests_string (void) {

	(void) printf ("Testing TYPES string...\n");

	test_str_new ();
	test_str_create ();
	test_str_allocate ();
	test_str_set ();
	test_str_compare ();
	test_str_comparator ();
	test_str_copy ();
	test_str_n_copy ();
	test_str_clone ();
	test_str_replace ();
	test_str_replace_with ();
	test_str_n_replace_with ();
	test_str_concat ();
	test_str_append_char ();
	test_str_append_c_string ();
	test_str_append_n_from_c_string ();
	test_str_append_n_from_c_string_many ();
	test_str_create_from_bytes ();
	test_str_append_bytes ();
	test_str_append_bytes_growth ();
	test_str_reserve ();
	test_str_capacity_reset ();
	test_str_to_upper ();
	test_str_to_lower ();
	test_str_remove_char ();
	test_str_remove_last_char ();
	test_str_contains ();

	(void) printf ("Done!\n");

}
