#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/utils/utils.h>

#include "../test.h"

#define STRING_LEN		32

static void test_c_string_copy (void) {

	char string[STRING_LEN] = { 0 };

	c_string_copy (string, "hola");
	test_check_int_eq ((int) strlen (string), 4, NULL);
	test_check_str_eq (string, "hola", NULL);

	c_string_copy (string, "holayadios");
	test_check_int_eq ((int) strlen (string), 10, NULL);
	test_check_str_eq (string, "holayadios", NULL);

	c_string_copy (string, "holayadiosholayadiosholayadios1");
	test_check_int_eq ((int) strlen (string), 31, NULL);
	test_check_str_eq (string, "holayadiosholayadiosholayadios1", NULL);

	// this causes an overflow
	c_string_copy (string, "holayadiosholayadiosholayadios1234567890");
	test_check_int_eq ((int) strlen (string), 40, NULL);

}

static void test_c_string_n_copy (void) {

	char string[STRING_LEN] = { 0 };

	c_string_n_copy (string, "hola", STRING_LEN);
	test_check_int_eq ((int) strlen (string), 4, NULL);
	test_check_str_eq (string, "hola", NULL);

	c_string_n_copy (string, "holayadios", STRING_LEN);
	test_check_int_eq ((int) strlen (string), 10, NULL);
	test_check_str_eq (string, "holayadios", NULL);

	c_string_n_copy (string, "holayadiosholayadiosholayadios12", STRING_LEN);
	test_check_int_eq ((int) strlen (string), 31, NULL);
	test_check_str_eq (string, "holayadiosholayadiosholayadios1", NULL);

	// this causes an overflow
	c_string_n_copy (string, "holayadiosholayadiosholayadios1234567890", STRING_LEN);
	test_check_int_eq ((int) strlen (string), 31, NULL);
	test_check_str_eq (string, "holayadiosholayadiosholayadios1", NULL);

	c_string_n_copy (string, "1234567890", STRING_LEN);
	test_check_int_eq ((int) strlen (string), 10, NULL);
	test_check_str_eq (string, "1234567890", NULL);

}

static void test_c_string_concat (void) {

	char s1[STRING_LEN] = { 0 };
	char s2[STRING_LEN] = { 0 };
	size_t res_len = 0;

	c_string_n_copy (s1, "hola", STRING_LEN);
	c_string_n_copy (s2, "adios", STRING_LEN);

	char *result = c_string_concat (s1, s2, &res_len);
	test_check (result != NULL, NULL);
	test_check_int_eq ((int) strlen (result), (int) res_len, NULL);
	test_check_str_eq (result, "holaadios", NULL);

	test_check_str_eq (s1, "hola", NULL);
	test_check_str_eq (s2, "adios", NULL);

	free (result);

}

static void test_c_string_concat_safe (void) {

	char s1[STRING_LEN] = { 0 };
	char s2[STRING_LEN] = { 0 };
	char result[STRING_LEN] = { 0 };

	c_string_n_copy (s1, "holayadiosholayadiosholayadios1", STRING_LEN);
	c_string_n_copy (s2, "holayadiosholayadiosholayadios1", STRING_LEN);
	size_t res_len = c_string_concat_safe (s1, s2, result, STRING_LEN);
	test_check_int_eq ((int) strlen (result), 0, NULL);
	
	c_string_n_copy (s1, "holay", STRING_LEN);
	c_string_n_copy (s2, "adios", STRING_LEN);
	res_len = c_string_concat_safe (s1, s2, result, STRING_LEN);
	test_check_int_eq ((int) strlen (result), res_len, NULL);
	test_check_str_eq (result, "holayadios", NULL);

	test_check_str_eq (s1, "holay", NULL);
	test_check_str_eq (s2, "adios", NULL);

}

static void test_c_string_create (void) {

	int integer = 16;
	char *s1 = { "hola" };
	char *s2 = { "y" };
	char *s3 = { "adios" };

	char *created = c_string_create ("%d-%s%s%s", integer, s1, s2, s3);
	test_check (created != NULL, NULL);
	test_check_str_eq (created, "16-holayadios", NULL);
	test_check_int_eq ((int) strlen (created), 13, NULL);

	free (created);

}

static void test_c_string_remove_spaces (void) {

	char string[STRING_LEN] = { 0 };

	c_string_n_copy (string, "hola y adios a todos", STRING_LEN);
	c_string_remove_spaces (string);
	test_check_str_eq (string, "holayadiosatodos", NULL);
	test_check_int_eq ((int) strlen (string), 16, NULL);

}

static void test_c_string_remove_line_breaks (void) {

	char string[STRING_LEN] = { 0 };

	c_string_n_copy (string, "hola\ny\nadios\na\ntodos", STRING_LEN);
	c_string_remove_line_breaks (string);
	test_check_str_eq (string, "holayadiosatodos", NULL);
	test_check_int_eq ((int) strlen (string), 16, NULL);

}

static void test_c_string_remove_spaces_and_line_breaks (void) {

	char string[STRING_LEN] = { 0 };

	c_string_n_copy (string, "hola y\nadios\na todos ", STRING_LEN);
	c_string_remove_spaces_and_line_breaks (string);
	test_check_str_eq (string, "holayadiosatodos", NULL);
	test_check_int_eq ((int) strlen (string), 16, NULL);

}

static void test_c_string_count_tokens (void) {

	char string[STRING_LEN] = { 0 };

	c_string_n_copy (string, "hola,y,adios,a,todos", STRING_LEN);
	size_t count = c_string_count_tokens (string, ',');

	test_check_str_eq (string, "hola,y,adios,a,todos", NULL);
	test_check_int_eq ((int) strlen (string), 20, NULL);
	test_check_int_eq ((int) count, 5, NULL);

}

static void test_c_string_split (void) {

	char string[STRING_LEN] = { 0 };

	c_string_n_copy (string, "hola,y,adios,a,todos", STRING_LEN);

	size_t count = 0;
	char **tokens = c_string_split (string, ',', &count);

	test_check (tokens != NULL, NULL);
	test_check_str_eq (string, "hola,y,adios,a,todos", NULL);
	test_check_int_eq ((int) strlen (string), 20, NULL);
	test_check_int_eq ((int) count, 5, NULL);

	test_check_str_eq (tokens[0], "hola", NULL);
	test_check_str_eq (tokens[1], "y", NULL);
	test_check_str_eq (tokens[2], "adios", NULL);
	test_check_str_eq (tokens[3], "a", NULL);
	test_check_str_eq (tokens[4], "todos", NULL);

	for (size_t i = 0; i < count; i ++)
		free (tokens[i]);

	free (tokens);

}

static void test_c_string_reverse (void) {

	char string[STRING_LEN] = { 0 };

	c_string_n_copy (string, "hola y adios a todos", STRING_LEN);

	char *result = c_string_reverse (string);
	test_check (result != NULL, NULL);
	test_check_str_eq (string, "hola y adios a todos", NULL);
	test_check_str_eq (result, "sodot a soida y aloh", NULL);

	free (result);

}

static void test_c_string_remove_char (void) {

	char string[STRING_LEN] = { 0 };

	c_string_n_copy (string, "hola,y,adios,a,todos", STRING_LEN);
	c_string_remove_char (string, ',');

	test_check_str_eq (string, "holayadiosatodos", NULL);
	test_check_int_eq ((int) strlen (string), 16, NULL);

}

static void test_c_string_remove_sub (void) {

	char string[STRING_LEN] = { 0 };

	c_string_n_copy (string, "holayadiosatodos", STRING_LEN);
	char *result = c_string_remove_sub (string, "adios");

	test_check (result != NULL, NULL);
	test_check_str_eq (result, "holayatodos", NULL);
	test_check_int_eq ((int) strlen (result), 11, NULL);

	test_check_str_eq (string, "holayadiosatodos", NULL);
	test_check_int_eq ((int) strlen (string), 16, NULL);

	free (result);

}

void utils_tests_c_strings (void) {

	(void) printf ("Testing UTILS c strings...\n");

	test_c_string_copy ();
	test_c_string_n_copy ();
	test_c_string_concat ();
	test_c_string_concat_safe ();
	test_c_string_create ();
	test_c_string_remove_spaces ();
	test_c_string_remove_line_breaks ();
	test_c_string_remove_spaces_and_line_breaks ();
	test_c_string_count_tokens ();
	test_c_string_split ();
	test_c_string_reverse ();
	test_c_string_remove_char ();
	test_c_string_remove_sub ();

	(void) printf ("Done!\n");

}