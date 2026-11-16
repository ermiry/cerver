#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include "cerver/json/internal.h"

#include "../test.h"
#include "json.h"

// the texts of the random numbers
#define NUMBERS_TEST_TEXT_SIZE				(1 << 11)

// how many random numbers each test reads
#define NUMBERS_TEST_RANDOM_COUNT			(1 << 16)

// how many doubles are read from the text of the point between them and the next one
#define NUMBERS_TEST_HALVES_COUNT			(1 << 12)

// the digits that glibc needs to print any point between two doubles exactly
#define NUMBERS_TEST_HALF_DIGITS			800

// the most digits of the mantissa of a random text, over the ones that fit in 64 bits
#define NUMBERS_TEST_MANTISSA_DIGITS		40

// the exponents of the random texts, around the ones that a double has
#define NUMBERS_TEST_EXPONENT_MIN			(-360)
#define NUMBERS_TEST_EXPONENT_RANGE			700

// the powers of ten that are read one by one, from below the smallest double to over the biggest one
#define NUMBERS_TEST_POWER_FIRST			(-350)
#define NUMBERS_TEST_POWER_LAST				320

#define NUMBERS_TEST_SEED					UINT64_C (0x9E3779B97F4A7C15)

// a text that is not a number of the grammar
typedef struct NumbersTestError {

	const char *text;
	CerverJsonErrorCode code;
	size_t offset;

} NumbersTestError;

static u64 numbers_test_state = NUMBERS_TEST_SEED;

// xorshift64: the same numbers in every run
static u64 numbers_test_random (void) {

	numbers_test_state ^= numbers_test_state << 13;
	numbers_test_state ^= numbers_test_state >> 7;
	numbers_test_state ^= numbers_test_state << 17;

	return numbers_test_state;

}

static u64 numbers_test_bits (const double value) {

	u64 bits = 0;
	(void) memcpy (&bits, &value, sizeof (u64));

	return bits;

}

#pragma region helpers

// reads a text that is a real and returns it
static double numbers_test_real (Arena *arena, const char *text) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	arena_reset (arena);

	CerverJsonDocument *document = cerver_json_parse (
		arena, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &error
	);

	if (!document || !cerver_json_is_number (cerver_json_document_root (document))) {
		(void) fprintf (
			stderr, "<%s> was not read as a number: %s at %zu\n",
			text, cerver_json_error_to_string (error.code), error.offset
		);

		exit (1);
	}

	if (cerver_json_type (cerver_json_document_root (document)) != CERVER_JSON_TYPE_REAL) {
		(void) fprintf (stderr, "<%s> was not read as a real\n", text);
		exit (1);
	}

	return cerver_json_real_value (cerver_json_document_root (document));

}

// reads a text of a real and compares it bit by bit with what the C library reads: the
// same double, or a number that does not fit when the library answers an infinite
static void numbers_test_matches (Arena *arena, const char *text) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	const double expected = strtod (text, NULL);

	if (isinf (expected)) {
		arena_reset (arena);

		CerverJsonDocument *document = cerver_json_parse (
			arena, text, strlen (text), CERVER_JSON_FLAGS_NONE, NULL, &error
		);

		if (document || (error.code != CERVER_JSON_ERROR_RANGE)) {
			(void) fprintf (stderr, "<%s> is an infinite and it was not out of range\n", text);
			exit (1);
		}
	}

	else {
		const double value = numbers_test_real (arena, text);

		if (numbers_test_bits (value) != numbers_test_bits (expected)) {
			(void) fprintf (
				stderr, "<%s> was read as %.17g (%016llx), the C library reads %.17g (%016llx)\n",
				text, value, (unsigned long long) numbers_test_bits (value),
				expected, (unsigned long long) numbers_test_bits (expected)
			);

			exit (1);
		}
	}

}

static const CerverJsonValue *numbers_test_root (Arena *arena, const char *text, const unsigned int flags) {

	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	arena_reset (arena);

	CerverJsonDocument *document = cerver_json_parse (arena, text, strlen (text), flags, NULL, &error);
	if (!document) {
		(void) fprintf (
			stderr, "<%s> was not parsed: %s at %zu\n",
			text, cerver_json_error_to_string (error.code), error.offset
		);

		exit (1);
	}

	return cerver_json_document_root (document);

}

#pragma endregion

#pragma region integers

static void test_numbers_integers (void) {

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	const CerverJsonValue *value = numbers_test_root (arena, "0", CERVER_JSON_FLAGS_NONE);
	test_check_unsigned_eq (cerver_json_type (value), CERVER_JSON_TYPE_INTEGER, NULL);
	test_check_long_int_eq (cerver_json_integer_value (value), 0, NULL);

	// a negative zero written as an integer is the integer zero
	value = numbers_test_root (arena, "-0", CERVER_JSON_FLAGS_NONE);
	test_check_unsigned_eq (cerver_json_type (value), CERVER_JSON_TYPE_INTEGER, NULL);
	test_check_long_int_eq (cerver_json_integer_value (value), 0, NULL);

	value = numbers_test_root (arena, "-1", CERVER_JSON_FLAGS_NONE);
	test_check_long_int_eq (cerver_json_integer_value (value), -1, NULL);

	value = numbers_test_root (arena, "1234567890123456789", CERVER_JSON_FLAGS_NONE);
	test_check_unsigned_eq (cerver_json_type (value), CERVER_JSON_TYPE_INTEGER, NULL);
	test_check_true (cerver_json_integer_value (value) == INT64_C (1234567890123456789));

	// the biggest and the smallest integers with sign
	value = numbers_test_root (arena, "9223372036854775807", CERVER_JSON_FLAGS_NONE);
	test_check_unsigned_eq (cerver_json_type (value), CERVER_JSON_TYPE_INTEGER, NULL);
	test_check_true (cerver_json_integer_value (value) == INT64_MAX);
	test_check_true (cerver_json_unsigned_value (value) == (u64) INT64_MAX);

	value = numbers_test_root (arena, "-9223372036854775808", CERVER_JSON_FLAGS_NONE);
	test_check_unsigned_eq (cerver_json_type (value), CERVER_JSON_TYPE_INTEGER, NULL);
	test_check_true (cerver_json_integer_value (value) == INT64_MIN);
	test_check_true (cerver_json_unsigned_value (value) == 0);

	// one more only fits without sign
	value = numbers_test_root (arena, "9223372036854775808", CERVER_JSON_FLAGS_NONE);
	test_check_unsigned_eq (cerver_json_type (value), CERVER_JSON_TYPE_UNSIGNED, NULL);
	test_check_true (cerver_json_unsigned_value (value) == (((u64) INT64_MAX) + 1));
	test_check_true (cerver_json_integer_value (value) == 0);

	value = numbers_test_root (arena, "10000000000000000000", CERVER_JSON_FLAGS_NONE);
	test_check_unsigned_eq (cerver_json_type (value), CERVER_JSON_TYPE_UNSIGNED, NULL);
	test_check_true (cerver_json_unsigned_value (value) == UINT64_C (10000000000000000000));

	value = numbers_test_root (arena, "18446744073709551615", CERVER_JSON_FLAGS_NONE);
	test_check_unsigned_eq (cerver_json_type (value), CERVER_JSON_TYPE_UNSIGNED, NULL);
	test_check_true (cerver_json_unsigned_value (value) == UINT64_MAX);
	test_check_true (test_double_equal (cerver_json_number_value (value) / 18446744073709551615.0, 1.0));

	arena_delete (arena);

}

// an integer that does not fit in 64 bits is an error where it starts
static void test_numbers_integers_range (void) {

	json_test_rejects ("18446744073709551616", 20, CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_RANGE, 0);
	json_test_rejects ("99999999999999999999", 20, CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_RANGE, 0);
	json_test_rejects ("100000000000000000000", 21, CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_RANGE, 0);
	json_test_rejects ("-9223372036854775809", 20, CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_RANGE, 0);
	json_test_rejects ("-18446744073709551615", 21, CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_RANGE, 0);
	json_test_rejects ("[1, 18446744073709551616]", 25, CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_RANGE, 4);

	// a real that does not fit in a double is one too, with any flag
	json_test_rejects ("1e309", 5, CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_RANGE, 0);
	json_test_rejects ("-1e309", 6, CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_RANGE, 0);
	json_test_rejects ("1e999", 5, CERVER_JSON_PARSE_BIG_AS_REAL, CERVER_JSON_ERROR_RANGE, 0);
	json_test_rejects ("1.7976931348623159e308", 22, CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_RANGE, 0);
	json_test_rejects ("[1e400]", 7, CERVER_JSON_FLAGS_NONE, CERVER_JSON_ERROR_RANGE, 1);

}

// with its flag an integer that does not fit is read as the closest double
static void test_numbers_integers_as_reals (void) {

	static const char *texts[] = {
		"18446744073709551616", "99999999999999999999", "100000000000000000000",
		"-9223372036854775809", "-18446744073709551615", "123456789012345678901234567890",
		"9007199254740993000000000000000000000001"
	};

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	for (size_t i = 0; i < (sizeof (texts) / sizeof (texts[0])); i++) {
		const CerverJsonValue *value = numbers_test_root (arena, texts[i], CERVER_JSON_PARSE_BIG_AS_REAL);

		test_check_unsigned_eq (cerver_json_type (value), CERVER_JSON_TYPE_REAL, NULL);
		test_check_true (numbers_test_bits (cerver_json_real_value (value)) == numbers_test_bits (strtod (texts[i], NULL)));
	}

	// the ones that fit stay as they are
	const CerverJsonValue *value = numbers_test_root (arena, "18446744073709551615", CERVER_JSON_PARSE_BIG_AS_REAL);
	test_check_unsigned_eq (cerver_json_type (value), CERVER_JSON_TYPE_UNSIGNED, NULL);

	arena_delete (arena);

}

#pragma endregion

#pragma region grammar

static const NumbersTestError numbers_test_errors[] = {
	// a zero before other digits
	{ "01", CERVER_JSON_ERROR_NUMBER, 1 },
	{ "00", CERVER_JSON_ERROR_NUMBER, 1 },
	{ "-01", CERVER_JSON_ERROR_NUMBER, 2 },
	{ "[012]", CERVER_JSON_ERROR_NUMBER, 2 },
	{ "00.5", CERVER_JSON_ERROR_NUMBER, 1 },
	// a minus without digits
	{ "-a", CERVER_JSON_ERROR_NUMBER, 1 },
	{ "--1", CERVER_JSON_ERROR_NUMBER, 1 },
	{ "-.5", CERVER_JSON_ERROR_NUMBER, 1 },
	{ "[-]", CERVER_JSON_ERROR_NUMBER, 2 },
	{ "- 1", CERVER_JSON_ERROR_NUMBER, 1 },
	// a point without digits after it
	{ "1.a", CERVER_JSON_ERROR_NUMBER, 2 },
	{ "1.e5", CERVER_JSON_ERROR_NUMBER, 2 },
	{ "0.e1", CERVER_JSON_ERROR_NUMBER, 2 },
	{ "[1.]", CERVER_JSON_ERROR_NUMBER, 3 },
	{ "1..5", CERVER_JSON_ERROR_NUMBER, 2 },
	// an exponent without digits
	{ "1ea", CERVER_JSON_ERROR_NUMBER, 2 },
	{ "1e+", CERVER_JSON_ERROR_NUMBER, 2 },
	{ "1e-", CERVER_JSON_ERROR_NUMBER, 2 },
	{ "1e+a", CERVER_JSON_ERROR_NUMBER, 2 },
	{ "[1e]", CERVER_JSON_ERROR_NUMBER, 3 },
	{ "1.5e.5", CERVER_JSON_ERROR_NUMBER, 4 },
	// what follows a number is not part of it
	{ "0x10", CERVER_JSON_ERROR_TRAILING, 1 },
	{ "1.5.5", CERVER_JSON_ERROR_TRAILING, 3 },
	{ "1e5e5", CERVER_JSON_ERROR_TRAILING, 3 },
	{ "1e5.5", CERVER_JSON_ERROR_TRAILING, 3 },
	{ "1a", CERVER_JSON_ERROR_TRAILING, 1 },
	{ "1-", CERVER_JSON_ERROR_TRAILING, 1 },
	{ "1+1", CERVER_JSON_ERROR_TRAILING, 1 },
	{ "1_000", CERVER_JSON_ERROR_TRAILING, 1 },
	{ "1,5", CERVER_JSON_ERROR_TRAILING, 1 },
	{ "[1 000]", CERVER_JSON_ERROR_CHARACTER, 3 }
};

static void test_numbers_grammar (void) {

	const size_t count = sizeof (numbers_test_errors) / sizeof (numbers_test_errors[0]);

	for (size_t i = 0; i < count; i++) {
		const NumbersTestError *expected = &numbers_test_errors[i];

		json_test_rejects (
			expected->text, strlen (expected->text), CERVER_JSON_FLAGS_NONE, expected->code, expected->offset
		);
	}

}

// a fraction or an exponent makes a real, also when its value is an integer
static void test_numbers_reals (void) {

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	test_check_true (numbers_test_bits (numbers_test_real (arena, "1.5")) == numbers_test_bits (1.5));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "1.0")) == numbers_test_bits (1.0));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "1e0")) == numbers_test_bits (1.0));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "1E+2")) == numbers_test_bits (100.0));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "1e-2")) == numbers_test_bits (0.01));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "0.0")) == numbers_test_bits (0.0));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "0e0")) == numbers_test_bits (0.0));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "0E-5")) == numbers_test_bits (0.0));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "0.000e+999")) == numbers_test_bits (0.0));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "123.456e-7")) == numbers_test_bits (123.456e-7));

	// the sign of a real zero is kept
	test_check_true (numbers_test_bits (numbers_test_real (arena, "-0.0")) == numbers_test_bits (-0.0));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "-0e0")) == numbers_test_bits (-0.0));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "-1e-999")) == numbers_test_bits (-0.0));

	// a number below the smallest double is zero, not an error
	test_check_true (numbers_test_bits (numbers_test_real (arena, "1e-999")) == numbers_test_bits (0.0));
	test_check_true (numbers_test_bits (numbers_test_real (arena, "1e-99999999999999999999")) == numbers_test_bits (0.0));

	// the getters of the other types answer nothing for a real
	const CerverJsonValue *value = numbers_test_root (arena, "2.5", CERVER_JSON_FLAGS_NONE);
	test_check_true (cerver_json_integer_value (value) == 0);
	test_check_true (cerver_json_unsigned_value (value) == 0);
	test_check_true (test_double_equal (cerver_json_number_value (value), 2.5));

	value = numbers_test_root (arena, "-7", CERVER_JSON_FLAGS_NONE);
	test_check_true (test_double_equal (cerver_json_real_value (value), 0.0));
	test_check_true (test_double_equal (cerver_json_number_value (value), -7.0));

	arena_delete (arena);

}

#pragma endregion

#pragma region library

// the texts that are hard to round: at the half of two doubles, around the
// smallest and the biggest ones, with more digits than the ones that fit in 64 bits
static const char *numbers_test_hard[] = {
	"0.1", "0.2", "0.3", "0.30000000000000004", "0.7", "1.1", "2.675", "1e22", "1e23", "8.5e-5",
	"9007199254740991.0", "9007199254740992.0", "9007199254740993.0", "9007199254740994.0",
	"9007199254740995.0", "9007199254740993.00000000000000000001", "9007199254740992.99999999999999999999",
	"9007199254740993e0", "9007199254740993.5e0", "90071992547409930000000000000000e-16",
	"2.2250738585072011e-308", "2.2250738585072012e-308", "2.2250738585072014e-308", "2.225073858507201e-308",
	"2.2250738585072009e-308", "2.2250738585072013e-308", "2.2250738585072016e-308",
	"4.9406564584124654e-324", "4.9e-324", "5e-324", "3e-324", "2.5e-324", "2.4703282292062327e-324",
	"2.4703282292062328e-324", "2.4703282292062329e-324", "2.47e-324", "1e-323", "1e-324", "7.4e-324",
	"7.5e-324", "1.5e-323", "1e-400",
	"1.7976931348623157e308", "1.7976931348623158e308", "1.797693134862315807e308",
	"1.7976931348623157081452742373170435679807056752584499659891747680315726078002853876058955863276687817154045895351438246423432132688946418276846754670353751698604991057655128207624549009038932894407586850845513394230458323690322294816580855933212334827479782620414472316873817718091929988125040402618441e308",
	"1.7976931348623158079372897140530341507993413271003782693617377898044496829276475094664901797758720709633028641669288791094655554785194040263065748867150582068190890200070838367627385484581771153176447573027006985557136695962284291481986083893647529271907416844436551070434271155969950809304288017790417e308",
	"3.1415926535897932384626433832795028841971693993751",
	"123456789012345678901234567890.0", "123456789012345678901234567890e-30",
	"0.000000000000000000000000000000000000000000000000000000000000000000000000000000000001e84",
	"100000000000000000000000000000000000000000000000000000000000000000000000000000000000.0e-83",
	"1.00000000000000011102230246251565404236316680908203125",
	"1.00000000000000011102230246251565404236316680908203124",
	"1.00000000000000011102230246251565404236316680908203126",
	"1.00000000000000033306690738754696212708950042724609375",
	"0.500000000000000166533453693773481063544750213623046875",
	"6.9294956446009195e15", "7.0420557077594588e-305", "5.0844431842073320e-308", "3.5844466002796428e298",
	"8.41e21", "1.2345678901234567e-320", "9.5e-322", "1e16", "1.0000000000000002e16",
	"4503599627370496.5", "4503599627370497.5", "0.99999999999999999", "0.999999999999999944488848768742172978818416595458984375",
	"0.99999999999999988897769753748434595763683319091796875",
	"17.5e-1"
};

static void test_numbers_hard (void) {

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	const size_t count = sizeof (numbers_test_hard) / sizeof (numbers_test_hard[0]);

	for (size_t i = 0; i < count; i++) numbers_test_matches (arena, numbers_test_hard[i]);

	arena_delete (arena);

}

// each power of ten, alone and with digits before it
static void test_numbers_powers (void) {

	char text[NUMBERS_TEST_TEXT_SIZE];

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	for (int power = NUMBERS_TEST_POWER_FIRST; power <= NUMBERS_TEST_POWER_LAST; power++) {
		(void) snprintf (text, sizeof (text), "1e%d", power);
		numbers_test_matches (arena, text);

		(void) snprintf (text, sizeof (text), "9.999999999999999e%d", power);
		numbers_test_matches (arena, text);

		(void) snprintf (text, sizeof (text), "18446744073709551615e%d", power);
		numbers_test_matches (arena, text);

		(void) snprintf (text, sizeof (text), "5e%d", power);
		numbers_test_matches (arena, text);
	}

	arena_delete (arena);

}

// random doubles written with the digits that always read them back
static void test_numbers_random_doubles (void) {

	char text[NUMBERS_TEST_TEXT_SIZE];

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	for (size_t i = 0; i < NUMBERS_TEST_RANDOM_COUNT; i++) {
		double value = 0;
		const u64 bits = numbers_test_random ();
		(void) memcpy (&value, &bits, sizeof (double));

		if (isfinite (value)) {
			(void) snprintf (text, sizeof (text), "%.17e", value);

			const double read = numbers_test_real (arena, text);
			if (numbers_test_bits (read) != bits) {
				(void) fprintf (stderr, "<%s> was read as %.17e\n", text, read);
				exit (1);
			}
		}
	}

	arena_delete (arena);

}

// random texts: from 1 digit to more than the ones that fit, with a point and an exponent anywhere
static void test_numbers_random_texts (void) {

	char text[NUMBERS_TEST_TEXT_SIZE];

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	for (size_t i = 0; i < NUMBERS_TEST_RANDOM_COUNT; i++) {
		const size_t digits = 1 + (size_t) (numbers_test_random () % NUMBERS_TEST_MANTISSA_DIGITS);
		const size_t point = (size_t) (numbers_test_random () % digits);
		const int exponent = NUMBERS_TEST_EXPONENT_MIN + (int) (numbers_test_random () % NUMBERS_TEST_EXPONENT_RANGE);

		size_t length = 0;
		for (size_t j = 0; j < digits; j++) {
			// the first digit is not a zero, unless it is the only one before the point
			const u64 digit = numbers_test_random () % 10;
			text[length++] = (char) ('0' + ((!j && !digit && point) ? 1 : digit));

			if (j == point) text[length++] = '.';
		}

		// a point needs a digit after it
		if (point == (digits - 1)) text[length++] = '5';

		(void) snprintf (text + length, sizeof (text) - length, "e%d", exponent);

		numbers_test_matches (arena, text);
	}

	arena_delete (arena);

}

// the exact text of the point between a double and the next one, where the even one of the two
// is the right one, and the texts a little below and a little over it
static void test_numbers_halves (void) {

	char text[NUMBERS_TEST_TEXT_SIZE];

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	for (size_t i = 0; i < NUMBERS_TEST_HALVES_COUNT; i++) {
		double value = 0;
		const u64 bits = numbers_test_random () & (UINT64_MAX >> 1);
		(void) memcpy (&value, &bits, sizeof (double));

		const double next = nextafter (value, (double) INFINITY);

		if (isfinite (value) && isfinite (next)) {
			// a long double has the bit that the point between them needs
			const long double half = ((long double) value) + ((((long double) next) - ((long double) value)) / 2);

			const int length = snprintf (text, sizeof (text), "%.*Le", NUMBERS_TEST_HALF_DIGITS, half);
			test_check_true ((length > 0) && ((size_t) length < sizeof (text)));

			numbers_test_matches (arena, text);

			// a digit that is not zero after the last one of the mantissa: over the half
			char *exponent = strchr (text, 'e');
			test_check_ptr (exponent);

			(void) memmove (exponent + 1, exponent, strlen (exponent) + 1);
			*exponent = '1';

			numbers_test_matches (arena, text);
		}
	}

	arena_delete (arena);

}

// numbers with thousands of digits: the ones over what the reader keeps only say that they are there
static void test_numbers_long (void) {

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	char *text = (char *) malloc (NUMBERS_TEST_TEXT_SIZE * 4);
	test_check_ptr (text);

	// a one, zeros and a point: 1e1500 written out, taken back by its exponent
	size_t length = 0;
	text[length++] = '1';
	(void) memset (text + length, '0', 1500);
	length += 1500;
	(void) snprintf (text + length, 32, ".0e-1500");
	numbers_test_matches (arena, text);

	// zeros after the point and a one, taken back by its exponent
	length = 0;
	text[length++] = '0';
	text[length++] = '.';
	(void) memset (text + length, '0', 1500);
	length += 1500;
	(void) snprintf (text + length, 32, "1e1501");
	numbers_test_matches (arena, text);

	// the half of two doubles, with a digit that decides it after thousands of zeros
	length = (size_t) snprintf (text, 64, "9007199254740993.");
	(void) memset (text + length, '0', 4000);
	length += 4000;
	(void) snprintf (text + length, 32, "1");
	numbers_test_matches (arena, text);

	text[length] = '0';
	numbers_test_matches (arena, text);

	// thousands of nines
	length = 0;
	text[length++] = '0';
	text[length++] = '.';
	(void) memset (text + length, '9', 4000);
	length += 4000;
	text[length] = '\0';
	numbers_test_matches (arena, text);

	arena_delete (arena);
	free (text);

}

#pragma endregion

#pragma region table

// an integer of any size for the test, in words of 32 bits from the lowest one
#define NUMBERS_TEST_WORDS					64
#define NUMBERS_TEST_WORD_BITS				32

// the powers of five up to this one fit in the 128 bits of the table
#define NUMBERS_TEST_EXACT_LAST				55

// the negative powers from this one are rounded up in the table, the ones below are cut
#define NUMBERS_TEST_ROUNDED_FIRST			(-27)

#define NUMBERS_TEST_POWER_BITS				128

#define NUMBERS_TEST_FIVE					5

typedef struct NumbersTestInteger {

	u32 words[NUMBERS_TEST_WORDS];

} NumbersTestInteger;

static void numbers_test_integer_set (NumbersTestInteger *integer, const u64 high, const u64 low) {

	(void) memset (integer, 0, sizeof (NumbersTestInteger));

	integer->words[0] = (u32) low;
	integer->words[1] = (u32) (low >> NUMBERS_TEST_WORD_BITS);
	integer->words[2] = (u32) high;
	integer->words[3] = (u32) (high >> NUMBERS_TEST_WORD_BITS);

}

static void numbers_test_integer_multiply (
	NumbersTestInteger *result, const NumbersTestInteger *first, const NumbersTestInteger *second
) {

	(void) memset (result, 0, sizeof (NumbersTestInteger));

	for (size_t i = 0; i < NUMBERS_TEST_WORDS; i++) {
		u64 carry = 0;
		for (size_t j = 0; (i + j) < NUMBERS_TEST_WORDS; j++) {
			const u64 value = (((u64) first->words[i]) * second->words[j]) + result->words[i + j] + carry;
			result->words[i + j] = (u32) value;
			carry = value >> NUMBERS_TEST_WORD_BITS;
		}
	}

}

// how many bits the integer has: the position of its highest one
static size_t numbers_test_integer_bits (const NumbersTestInteger *integer) {

	for (size_t i = NUMBERS_TEST_WORDS; i > 0; i--) {
		if (integer->words[i - 1]) {
			return (i * NUMBERS_TEST_WORD_BITS) - (size_t) __builtin_clz (integer->words[i - 1]);
		}
	}

	return 0;

}

static bool numbers_test_integer_bit (const NumbersTestInteger *integer, const size_t bit) {

	return (integer->words[bit / NUMBERS_TEST_WORD_BITS] >> (bit % NUMBERS_TEST_WORD_BITS)) & 1;

}

// compares the integer with 2^power: below, the same or over it
static int numbers_test_integer_compare_power (const NumbersTestInteger *integer, const size_t power) {

	const size_t bits = numbers_test_integer_bits (integer);

	if (bits != (power + 1)) return (bits < (power + 1)) ? -1 : 1;

	for (size_t bit = 0; bit < power; bit++) {
		if (numbers_test_integer_bit (integer, bit)) return 1;
	}

	return 0;

}

// the next power of five
static void numbers_test_integer_next_power (NumbersTestInteger *integer) {

	u64 carry = 0;
	for (size_t i = 0; i < NUMBERS_TEST_WORDS; i++) {
		const u64 value = (((u64) integer->words[i]) * NUMBERS_TEST_FIVE) + carry;
		integer->words[i] = (u32) value;
		carry = value >> NUMBERS_TEST_WORD_BITS;
	}

}

// a power that is not negative: its 128 highest bits as they are, exact up to 5^55
static void numbers_test_power_positive (
	const NumbersTestInteger *power, const int exponent, const u64 high, const u64 low
) {

	const size_t bits = numbers_test_integer_bits (power);

	u64 expected_high = 0;
	u64 expected_low = 0;
	bool exact = true;

	for (size_t i = 0; i < bits; i++) {
		const size_t bit = bits - 1 - i;
		const bool set = numbers_test_integer_bit (power, bit);

		if (i < (NUMBERS_TEST_POWER_BITS / 2)) expected_high |= ((u64) set) << ((NUMBERS_TEST_POWER_BITS / 2) - 1 - i);
		else if (i < NUMBERS_TEST_POWER_BITS) expected_low |= ((u64) set) << (NUMBERS_TEST_POWER_BITS - 1 - i);
		else if (set) exact = false;
	}

	test_check_true (high == expected_high);
	test_check_true (low == expected_low);
	test_check_bool_eq (exact, (exponent <= NUMBERS_TEST_EXACT_LAST), NULL);

}

// a negative power is 2^b / 5^-exponent for the b that gives it 128 bits: the ones from
// 5^-27 are rounded up, the ones below are cut, and none of them is exact
static void numbers_test_power_negative (
	const NumbersTestInteger *power, const int exponent, const u64 high, const u64 low
) {

	NumbersTestInteger value;
	NumbersTestInteger next;
	NumbersTestInteger product;

	const size_t dividend = numbers_test_integer_bits (power) + NUMBERS_TEST_POWER_BITS - 1;
	const bool rounded = exponent >= NUMBERS_TEST_ROUNDED_FIRST;

	// what the division gives without its fraction is the value of the ones that are cut, and one less for the others
	const JsonWide entry = (((JsonWide) high) << (NUMBERS_TEST_POWER_BITS / 2)) | low;
	const JsonWide cut = rounded ? (entry - 1) : entry;
	const JsonWide above = cut + 1;

	test_check_true ((high >> ((NUMBERS_TEST_POWER_BITS / 2) - 1)) == 1);

	numbers_test_integer_set (&value, (u64) (cut >> (NUMBERS_TEST_POWER_BITS / 2)), (u64) cut);
	numbers_test_integer_set (&next, (u64) (above >> (NUMBERS_TEST_POWER_BITS / 2)), (u64) above);

	// cut x 5^-exponent < 2^b < (cut + 1) x 5^-exponent
	numbers_test_integer_multiply (&product, &value, power);
	test_check_int_eq (numbers_test_integer_compare_power (&product, dividend), -1, NULL);

	numbers_test_integer_multiply (&product, &next, power);
	test_check_int_eq (numbers_test_integer_compare_power (&product, dividend), 1, NULL);

}

// every power of the table against its definition, computed here with whole integers
static void test_numbers_table (void) {

	NumbersTestInteger power;
	numbers_test_integer_set (&power, 0, 1);

	for (int exponent = 0; (exponent <= JSON_NUMBER_POWERS_LAST) || (-exponent >= JSON_NUMBER_POWERS_FIRST); exponent++) {
		u64 high = 0;
		u64 low = 0;

		if (exponent <= JSON_NUMBER_POWERS_LAST) {
			cerver_json_number_power (exponent, &high, &low);
			numbers_test_power_positive (&power, exponent, high, low);
		}

		if (exponent && (-exponent >= JSON_NUMBER_POWERS_FIRST)) {
			cerver_json_number_power (-exponent, &high, &low);
			numbers_test_power_negative (&power, -exponent, high, low);
		}

		numbers_test_integer_next_power (&power);
	}

}

#pragma endregion

void json_tests_numbers (void) {

	(void) printf ("\nTesting JSON numbers...\n");

	test_run (test_numbers_integers);
	test_run (test_numbers_integers_range);
	test_run (test_numbers_integers_as_reals);

	test_run (test_numbers_grammar);
	test_run (test_numbers_reals);

	test_run (test_numbers_table);

	test_run (test_numbers_hard);
	test_run (test_numbers_powers);
	test_run (test_numbers_random_doubles);
	test_run (test_numbers_random_texts);
	test_run (test_numbers_halves);
	test_run (test_numbers_long);

}
