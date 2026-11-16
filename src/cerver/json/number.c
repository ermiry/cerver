#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/json/internal.h"
#include "cerver/json/json.h"

// the layout of a double: 52 bits of mantissa, 11 of exponent and its sign
#define JSON_DOUBLE_MANTISSA_BITS			52
#define JSON_DOUBLE_EXPONENT_BITS			11
#define JSON_DOUBLE_HIDDEN_BIT				(UINT64_C (1) << JSON_DOUBLE_MANTISSA_BITS)
#define JSON_DOUBLE_MANTISSA_MASK			(JSON_DOUBLE_HIDDEN_BIT - 1)
#define JSON_DOUBLE_EXPONENT_MASK			((1 << JSON_DOUBLE_EXPONENT_BITS) - 1)
#define JSON_DOUBLE_SIGN_BIT				(UINT64_C (1) << 63)

// the exponent of an infinite and of a NaN
#define JSON_DOUBLE_EXPONENT_SPECIAL		JSON_DOUBLE_EXPONENT_MASK

// the bias of the exponent of a double whose mantissa is taken as a fraction (1.m)
#define JSON_DOUBLE_EXPONENT_BIAS			1023

// the bias of the exponent of a double whose mantissa is taken as an integer of 53 bits
#define JSON_DOUBLE_INTEGER_BIAS			(JSON_DOUBLE_EXPONENT_BIAS + JSON_DOUBLE_MANTISSA_BITS)

#define JSON_DOUBLE_SIGNIFICAND_BITS		(JSON_DOUBLE_MANTISSA_BITS + 1)

#define JSON_WORD_BITS						64

static const u8 JSON_NUMBER_MINUS = '-';
static const u8 JSON_NUMBER_PLUS = '+';
static const u8 JSON_NUMBER_POINT = '.';
static const u8 JSON_NUMBER_ZERO = '0';
static const u8 JSON_NUMBER_EXPONENT_LOWER = 'e';
static const u8 JSON_NUMBER_EXPONENT_UPPER = 'E';

static const u64 JSON_NUMBER_BASE = 10;

#pragma region text

// the digits of a number that always fit in 64 bits
#define JSON_NUMBER_MANTISSA_DIGITS			19

// the digits of the biggest integer of 64 bits
#define JSON_NUMBER_INTEGER_DIGITS			20

// no text has an exponent this big written or this many digits: over it the digits of an exponent are not added
static const i64 JSON_NUMBER_EXPONENT_LIMIT = INT64_C (100000000000000000);

// the magnitude of the smallest integer of 64 bits with sign
static const u64 JSON_NUMBER_INTEGER_MAGNITUDE = UINT64_C (1) << 63;

// what the text of a number has
typedef struct JsonNumberText {

	// the first digit of the integer part, and the byte after the last digit of the fraction
	const u8 *digits;
	const u8 *digits_end;

	// the first significant digits, up to JSON_NUMBER_MANTISSA_DIGITS, and how many they are
	u64 mantissa;
	unsigned int significant;

	// the power of ten of the mantissa, by the digits that it took and the ones that it left out,
	// and the exponent that the text has written
	i64 exponent;
	i64 written;

	size_t integer_digits;

	bool negative;

	// it has a fraction or an exponent
	bool real;

	// digits that are not zero were left out of the mantissa
	bool truncated;

} JsonNumberText;

static inline bool json_number_is_digit (const u8 byte) {

	return (json_characters[byte] & JSON_CHARACTER_DIGIT) != 0;

}

static inline void json_number_text_integer_digit (JsonNumberText *text, const u8 byte) {

	const u64 digit = (u64) (byte - JSON_NUMBER_ZERO);

	if (text->significant < JSON_NUMBER_MANTISSA_DIGITS) {
		text->mantissa = (text->mantissa * JSON_NUMBER_BASE) + digit;
		if (text->mantissa) text->significant += 1;
	}

	else {
		if (digit) text->truncated = true;
		text->exponent += 1;
	}

}

static inline void json_number_text_fraction_digit (JsonNumberText *text, const u8 byte) {

	const u64 digit = (u64) (byte - JSON_NUMBER_ZERO);

	if (text->significant < JSON_NUMBER_MANTISSA_DIGITS) {
		text->mantissa = (text->mantissa * JSON_NUMBER_BASE) + digit;
		if (text->mantissa) text->significant += 1;
		text->exponent -= 1;
	}

	else if (digit) {
		text->truncated = true;
	}

}

// the integer part: a zero alone, or digits that do not start with a zero
static const u8 *json_number_text_integer (JsonNumberText *text, const u8 *position) {

	text->digits = position;

	if (*position == JSON_NUMBER_ZERO) {
		position += 1;
		return json_number_is_digit (*position) ? NULL : position;
	}

	if (!json_number_is_digit (*position)) return NULL;

	while (json_number_is_digit (*position)) {
		json_number_text_integer_digit (text, *position);
		position += 1;
	}

	text->integer_digits = (size_t) (position - text->digits);

	return position;

}

// the fraction, after its point: at least one digit
static const u8 *json_number_text_fraction (JsonNumberText *text, const u8 *position) {

	if (!json_number_is_digit (*position)) return NULL;

	while (json_number_is_digit (*position)) {
		json_number_text_fraction_digit (text, *position);
		position += 1;
	}

	text->real = true;

	return position;

}

// the exponent, after its letter: a sign that is optional and at least one digit
static const u8 *json_number_text_exponent (JsonNumberText *text, const u8 *position) {

	bool negative = false;
	if ((*position == JSON_NUMBER_MINUS) || (*position == JSON_NUMBER_PLUS)) {
		negative = (*position == JSON_NUMBER_MINUS);
		position += 1;
	}

	if (!json_number_is_digit (*position)) return NULL;

	i64 written = 0;
	while (json_number_is_digit (*position)) {
		if (written < JSON_NUMBER_EXPONENT_LIMIT) {
			written = (written * (i64) JSON_NUMBER_BASE) + (i64) (*position - JSON_NUMBER_ZERO);
		}

		position += 1;
	}

	text->written = negative ? -written : written;
	text->real = true;

	return position;

}

// reads the text of a number: the position after it, NULL with the byte of its error in the cursor
static const u8 *json_number_text_read (JsonNumberText *text, JsonCursor *cursor) {

	const u8 *position = cursor->position;
	const u8 *next = NULL;

	if (*position == JSON_NUMBER_MINUS) {
		text->negative = true;
		position += 1;
	}

	next = json_number_text_integer (text, position);
	if (next) {
		position = next;
		text->digits_end = position;

		if (*position == JSON_NUMBER_POINT) {
			position += 1;
			next = json_number_text_fraction (text, position);
			if (next) {
				position = next;
				text->digits_end = position;
			}
		}
	}

	if (next && ((*position == JSON_NUMBER_EXPONENT_LOWER) || (*position == JSON_NUMBER_EXPONENT_UPPER))) {
		position += 1;
		next = json_number_text_exponent (text, position);
		if (next) position = next;
	}

	if (!next) {
		// the byte that the grammar does not have: after a first zero it is the digit that follows it
		cursor->position = (u8 *) position + (
			((*position == JSON_NUMBER_ZERO) && (position == text->digits)) ? 1 : 0
		);
	}

	return next ? position : NULL;

}

#pragma endregion

#pragma region digits

// the decimal digits of a number, for the texts that the fast methods can not decide:
// with this many, and whether more were left out, the closest double is always the right one
#define JSON_DIGITS_SIZE					(1 << 10)

// the most bits that the digits are shifted by at once, so a digit and its carry fit in 64 bits
#define JSON_DIGITS_SHIFT_MAX				60

// the digits that a carry of JSON_DIGITS_SHIFT_MAX bits can have
#define JSON_DIGITS_CARRY_SIZE				20

// a number with its point above this is over the biggest double, and below the other one it is zero
static const i64 JSON_DIGITS_POINT_MAX = 310;
static const i64 JSON_DIGITS_POINT_MIN = -330;

// the bits that take the digits closer to [0.5, 1) by the position of their point,
// and the ones for a point that is further
static const unsigned int JSON_DIGITS_POWERS[] = { 1, 3, 6, 9, 13, 16, 19, 23, 26 };
static const unsigned int JSON_DIGITS_POWERS_FAR = 27;

// the digit of the half, where a number is rounded
static const u8 JSON_DIGITS_HALF = 5;

// the digits of the biggest integer that json_digits_rounded () returns
static const i64 JSON_DIGITS_ROUNDED_MAX = 20;

// a number as 0.d0 d1 d2 ... x 10^point
typedef struct JsonDigits {

	u8 digits[JSON_DIGITS_SIZE];
	size_t count;
	i64 point;
	bool truncated;

} JsonDigits;

static void json_digits_trim (JsonDigits *digits) {

	while (digits->count && !digits->digits[digits->count - 1]) digits->count -= 1;

	if (!digits->count) digits->point = 0;

}

static void json_digits_add (JsonDigits *digits, const u8 digit) {

	if (digits->count < JSON_DIGITS_SIZE) {
		digits->digits[digits->count] = digit;
		digits->count += 1;
	}

	else if (digit) {
		digits->truncated = true;
	}

}

// takes the digits of the text of a number, without the zeros before its first digit
static void json_digits_set (JsonDigits *digits, const JsonNumberText *text) {

	bool fraction = false;
	i64 point = 0;

	digits->count = 0;
	digits->truncated = false;

	for (const u8 *position = text->digits; position < text->digits_end; position++) {
		if (*position == JSON_NUMBER_POINT) {
			fraction = true;
		}

		else {
			const u8 digit = (u8) (*position - JSON_NUMBER_ZERO);
			if (digit || digits->count) {
				json_digits_add (digits, digit);
				if (!fraction) point += 1;
			}

			else if (fraction) {
				point -= 1;
			}
		}
	}

	digits->point = point;

	json_digits_trim (digits);

}

// puts the digits of a carry before the ones that the number has
static void json_digits_prepend (JsonDigits *digits, u64 carry) {

	u8 lead[JSON_DIGITS_CARRY_SIZE];
	size_t size = 0;

	while (carry) {
		lead[size] = (u8) (carry % JSON_NUMBER_BASE);
		carry /= JSON_NUMBER_BASE;
		size += 1;
	}

	if (size) {
		size_t keep = digits->count;
		if (keep > (JSON_DIGITS_SIZE - size)) {
			keep = JSON_DIGITS_SIZE - size;
			for (size_t i = keep; i < digits->count; i++) {
				if (digits->digits[i]) digits->truncated = true;
			}
		}

		(void) memmove (digits->digits + size, digits->digits, keep);

		for (size_t i = 0; i < size; i++) digits->digits[i] = lead[size - 1 - i];

		digits->count = keep + size;
		digits->point += (i64) size;
	}

}

// multiplies the number by 2^shift, from its last digit to its first one
static void json_digits_shift_left (JsonDigits *digits, const unsigned int shift) {

	u64 carry = 0;

	for (size_t i = digits->count; i > 0; i--) {
		const u64 value = (((u64) digits->digits[i - 1]) << shift) + carry;
		digits->digits[i - 1] = (u8) (value % JSON_NUMBER_BASE);
		carry = value / JSON_NUMBER_BASE;
	}

	json_digits_prepend (digits, carry);

	json_digits_trim (digits);

}

// divides the number by 2^shift, a long division from its first digit
static void json_digits_shift_right (JsonDigits *digits, const unsigned int shift) {

	const u64 mask = (UINT64_C (1) << shift) - 1;

	size_t read = 0;
	size_t write = 0;
	u64 value = 0;

	// the digits that the first one of the quotient needs
	while (!(value >> shift)) {
		value = (value * JSON_NUMBER_BASE) + ((read < digits->count) ? digits->digits[read] : 0);
		read += 1;
	}

	digits->point -= ((i64) read - 1);

	for (; read < digits->count; read++) {
		digits->digits[write] = (u8) (value >> shift);
		write += 1;
		value = ((value & mask) * JSON_NUMBER_BASE) + digits->digits[read];
	}

	// the digits that are left in the remainder
	while (value) {
		const u8 digit = (u8) (value >> shift);
		if (write < JSON_DIGITS_SIZE) {
			digits->digits[write] = digit;
			write += 1;
		}

		else if (digit) {
			digits->truncated = true;
		}

		value = (value & mask) * JSON_NUMBER_BASE;
	}

	digits->count = write;

	json_digits_trim (digits);

}

// multiplies the number by 2^shift, or divides it for a negative one
static void json_digits_shift (JsonDigits *digits, i64 shift) {

	if (digits->count) {
		while (shift > JSON_DIGITS_SHIFT_MAX) {
			json_digits_shift_left (digits, JSON_DIGITS_SHIFT_MAX);
			shift -= JSON_DIGITS_SHIFT_MAX;
		}

		while (shift < -JSON_DIGITS_SHIFT_MAX) {
			json_digits_shift_right (digits, JSON_DIGITS_SHIFT_MAX);
			shift += JSON_DIGITS_SHIFT_MAX;
		}

		if (shift > 0) json_digits_shift_left (digits, (unsigned int) shift);
		else if (shift < 0) json_digits_shift_right (digits, (unsigned int) -shift);
	}

}

// the integer part goes up when what follows is over the half; at the half exactly,
// when digits were left out (the number is a little more) or to make the integer even
static bool json_digits_rounds_up (const JsonDigits *digits, const i64 point) {

	if ((point < 0) || ((size_t) point >= digits->count)) return false;

	const size_t position = (size_t) point;

	if ((digits->digits[position] == JSON_DIGITS_HALF) && ((position + 1) == digits->count)) {
		if (digits->truncated) return true;

		return position && (digits->digits[position - 1] & 1);
	}

	return digits->digits[position] >= JSON_DIGITS_HALF;

}

// the integer part of the number, rounded to the closest one
static u64 json_digits_rounded (const JsonDigits *digits) {

	u64 value = 0;

	if (digits->point > JSON_DIGITS_ROUNDED_MAX) return UINT64_MAX;

	for (i64 i = 0; i < digits->point; i++) {
		value = (value * JSON_NUMBER_BASE) + (((size_t) i < digits->count) ? digits->digits[i] : 0);
	}

	if (json_digits_rounds_up (digits, digits->point)) value += 1;

	return value;

}

static unsigned int json_digits_power (const i64 point) {

	const size_t size = sizeof (JSON_DIGITS_POWERS) / sizeof (JSON_DIGITS_POWERS[0]);

	return ((size_t) point < size) ? JSON_DIGITS_POWERS[point] : JSON_DIGITS_POWERS_FAR;

}

// the number goes to [0.5, 1) by powers of two, and the exponent counts them
static i64 json_digits_normalize (JsonDigits *digits) {

	i64 exponent = 0;

	while (digits->point > 0) {
		const unsigned int power = json_digits_power (digits->point);
		json_digits_shift (digits, -(i64) power);
		exponent += power;
	}

	while ((digits->point < 0) || (!digits->point && (digits->digits[0] < JSON_DIGITS_HALF))) {
		const unsigned int power = json_digits_power (-digits->point);
		json_digits_shift (digits, (i64) power);
		exponent -= power;
	}

	return exponent;

}

// the bits of the closest double to the digits; an infinite when they are over the biggest one
static u64 json_digits_bits (JsonDigits *digits) {

	const u64 infinite = ((u64) JSON_DOUBLE_EXPONENT_SPECIAL) << JSON_DOUBLE_MANTISSA_BITS;

	if (!digits->count || (digits->point < JSON_DIGITS_POINT_MIN)) return 0;
	if (digits->point > JSON_DIGITS_POINT_MAX) return infinite;

	// a double is in [1, 2), the digits are in [0.5, 1)
	i64 exponent = json_digits_normalize (digits) - 1;

	// a number below the smallest exponent keeps that one, as a subnormal
	if (exponent < (1 - JSON_DOUBLE_EXPONENT_BIAS)) {
		const i64 difference = (1 - JSON_DOUBLE_EXPONENT_BIAS) - exponent;
		json_digits_shift (digits, -difference);
		exponent += difference;
	}

	if ((exponent + JSON_DOUBLE_EXPONENT_BIAS) >= JSON_DOUBLE_EXPONENT_SPECIAL) return infinite;

	json_digits_shift (digits, JSON_DOUBLE_SIGNIFICAND_BITS);

	u64 mantissa = json_digits_rounded (digits);

	// the rounding can add a bit
	if (mantissa == (JSON_DOUBLE_HIDDEN_BIT << 1)) {
		mantissa >>= 1;
		exponent += 1;
		if ((exponent + JSON_DOUBLE_EXPONENT_BIAS) >= JSON_DOUBLE_EXPONENT_SPECIAL) return infinite;
	}

	// a subnormal has no hidden bit and the exponent of zero
	if (!(mantissa & JSON_DOUBLE_HIDDEN_BIT)) exponent = -JSON_DOUBLE_EXPONENT_BIAS;

	return (mantissa & JSON_DOUBLE_MANTISSA_MASK)
		| (((u64) (exponent + JSON_DOUBLE_EXPONENT_BIAS)) << JSON_DOUBLE_MANTISSA_BITS);

}

// the closest double to the text of a number, exact for any amount of digits
static u64 json_number_exact (const JsonNumberText *text) {

	JsonDigits digits;

	json_digits_set (&digits, text);

	digits.point += text->written;

	return json_digits_bits (&digits);

}

#pragma endregion

#pragma region approximation

// the powers of ten that a double has exactly
static const double JSON_NUMBER_EXACT_POWERS[] = {
	1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
	1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};

#define JSON_NUMBER_EXACT_POWERS_LAST		22

// the biggest integer that a double has exactly with every one below it
static const u64 JSON_NUMBER_EXACT_MANTISSA = UINT64_C (1) << JSON_DOUBLE_SIGNIFICAND_BITS;

// the powers of ten of the biggest and of the smallest double that is not zero
#define JSON_NUMBER_POWER_MAX				308
#define JSON_NUMBER_POWER_MIN				(-342)

// the product of the mantissa and a power needs the bits of the mantissa of a double,
// its hidden bit, one to round and one that a shift can lose: the bits below them are these
static const u64 JSON_LEMIRE_PRECISION_MASK = UINT64_MAX >> (JSON_DOUBLE_MANTISSA_BITS + 3);

// the bits that the high word of the product is shifted to leave the mantissa with its round bit
#define JSON_LEMIRE_SHIFT					(JSON_WORD_BITS - JSON_DOUBLE_MANTISSA_BITS - 3)

// floor (log2 (5^q)) is (q * JSON_LEMIRE_LOG_FACTOR) >> JSON_LEMIRE_LOG_SHIFT for the powers of the table
static const i64 JSON_LEMIRE_LOG_FACTOR = 152170 + 65536;
#define JSON_LEMIRE_LOG_SHIFT				16

// the powers whose product has every bit that decides it: outside
// them, a low word of the product with every bit set is not enough
static const i64 JSON_LEMIRE_SAFE_MIN = -27;
static const i64 JSON_LEMIRE_SAFE_MAX = 55;

// the powers of five that fit in a word: with them a number can be exactly half way of two doubles
static const i64 JSON_LEMIRE_EVEN_MIN = -4;
static const i64 JSON_LEMIRE_EVEN_MAX = 23;

static inline double json_number_from_bits (const u64 bits) {

	double value = 0;
	(void) memcpy (&value, &bits, sizeof (double));

	return value;

}

static inline u64 json_number_to_bits (const double value) {

	u64 bits = 0;
	(void) memcpy (&bits, &value, sizeof (u64));

	return bits;

}

// a mantissa and a power of ten that a double has exactly give the closest
// double with one operation (Clinger): the processor rounds it once
static bool json_number_exact_operands (const u64 mantissa, const i64 exponent, double *value) {

	bool retval = false;

	if (
		(mantissa <= JSON_NUMBER_EXACT_MANTISSA)
		&& (exponent >= -JSON_NUMBER_EXACT_POWERS_LAST) && (exponent <= JSON_NUMBER_EXACT_POWERS_LAST)
	) {
		*value = (exponent < 0) ?
			(((double) mantissa) / JSON_NUMBER_EXACT_POWERS[-exponent]) :
			(((double) mantissa) * JSON_NUMBER_EXACT_POWERS[exponent]);

		retval = true;
	}

	return retval;

}

// the 128 high bits of the product of the mantissa and the power of five: the second word
// of the power is only used when the first one leaves the bits that matter undecided
static JsonWide json_lemire_product (const u64 mantissa, const i64 exponent) {

	const u64 *power = json_number_powers[exponent - JSON_NUMBER_POWERS_FIRST];

	JsonWide product = ((JsonWide) mantissa) * power[0];

	if ((((u64) (product >> JSON_WORD_BITS)) & JSON_LEMIRE_PRECISION_MASK) == JSON_LEMIRE_PRECISION_MASK) {
		const JsonWide second = ((JsonWide) mantissa) * power[1];
		product += (second >> JSON_WORD_BITS);
	}

	return product;

}

// a number below the smallest exponent: its mantissa is shifted to that exponent and rounded
static u64 json_lemire_subnormal (u64 mantissa, const i64 exponent) {

	const i64 shift = 1 - exponent;

	if (shift >= JSON_WORD_BITS) return 0;

	mantissa >>= shift;
	mantissa += (mantissa & 1);
	mantissa >>= 1;

	// a number that rounds up to the smallest normal double is its hidden bit alone,
	// that in the bits of a double is the exponent of that one
	return mantissa;

}

// the closest double to mantissa x 10^exponent (Eisel and Lemire), for a mantissa that is
// not zero and a power of the table; false when the product can not decide it
static bool json_lemire_bits (u64 mantissa, const i64 exponent, u64 *bits) {

	const int zeros = __builtin_clzll (mantissa);
	mantissa <<= zeros;

	const JsonWide product = json_lemire_product (mantissa, exponent);
	const u64 high = (u64) (product >> JSON_WORD_BITS);
	const u64 low = (u64) product;

	if ((low == UINT64_MAX) && ((exponent < JSON_LEMIRE_SAFE_MIN) || (exponent > JSON_LEMIRE_SAFE_MAX))) return false;

	const int upper = (int) (high >> (JSON_WORD_BITS - 1));
	const int shift = upper + JSON_LEMIRE_SHIFT;

	u64 result = high >> shift;

	i64 power = (((JSON_LEMIRE_LOG_FACTOR * exponent) >> JSON_LEMIRE_LOG_SHIFT) + (JSON_WORD_BITS - 1))
		+ upper - zeros + JSON_DOUBLE_EXPONENT_BIAS;

	if (power <= 0) {
		*bits = json_lemire_subnormal (result, power);
		return true;
	}

	// exactly half way of two doubles: the zeros that the shift left out say it, and the even one is taken
	if (
		(low <= 1) && (exponent >= JSON_LEMIRE_EVEN_MIN) && (exponent <= JSON_LEMIRE_EVEN_MAX)
		&& ((result & 3) == 1) && ((result << shift) == high)
	) {
		result &= ~UINT64_C (1);
	}

	result += (result & 1);
	result >>= 1;

	if (result >= (JSON_DOUBLE_HIDDEN_BIT << 1)) {
		result = JSON_DOUBLE_HIDDEN_BIT;
		power += 1;
	}

	result &= ~JSON_DOUBLE_HIDDEN_BIT;

	if (power >= JSON_DOUBLE_EXPONENT_SPECIAL) {
		power = JSON_DOUBLE_EXPONENT_SPECIAL;
		result = 0;
	}

	*bits = result | (((u64) power) << JSON_DOUBLE_MANTISSA_BITS);

	return true;

}

// the closest double to a text whose mantissa has its first digits: when digits were left
// out the number is between the mantissa and the next one, and it is decided when both agree
static bool json_number_approximate (const JsonNumberText *text, const i64 exponent, u64 *bits) {

	bool retval = json_lemire_bits (text->mantissa, exponent, bits);

	if (retval && text->truncated) {
		u64 next = 0;
		retval = json_lemire_bits (text->mantissa + 1, exponent, &next) && (next == *bits);
	}

	return retval;

}

#pragma endregion

#pragma region read

// the bits of the closest double to the text of a number, without its sign
static u64 json_number_real_bits (const JsonNumberText *text) {

	u64 bits = 0;
	double value = 0;

	const i64 exponent = text->exponent + text->written;

	// a mantissa of zero with digits left out is not zero: it never happens,
	// the mantissa takes the first digits that are not zero
	if (!text->mantissa) return 0;

	// a mantissa that left digits out has the 19 that fit: it is over the ones that are exact
	if (json_number_exact_operands (text->mantissa, exponent, &value)) {
		return json_number_to_bits (value);
	}

	if (exponent < JSON_NUMBER_POWER_MIN) return 0;

	if (exponent > JSON_NUMBER_POWER_MAX) return ((u64) JSON_DOUBLE_EXPONENT_SPECIAL) << JSON_DOUBLE_MANTISSA_BITS;

	if (json_number_approximate (text, exponent, &bits)) return bits;

	return json_number_exact (text);

}

static CerverJsonErrorCode json_number_real (const JsonNumberText *text, JsonNumber *number) {

	const u64 bits = json_number_real_bits (text);

	if (((bits >> JSON_DOUBLE_MANTISSA_BITS) & JSON_DOUBLE_EXPONENT_MASK) == JSON_DOUBLE_EXPONENT_SPECIAL) {
		return CERVER_JSON_ERROR_RANGE;
	}

	number->type = CERVER_JSON_TYPE_REAL;
	number->value.real = json_number_from_bits (bits | (text->negative ? JSON_DOUBLE_SIGN_BIT : 0));

	return CERVER_JSON_ERROR_NONE;

}

// the magnitude of an integer; false when it does not fit in 64 bits
static bool json_number_magnitude (const JsonNumberText *text, u64 *magnitude) {

	bool retval = false;

	if (text->integer_digits <= JSON_NUMBER_MANTISSA_DIGITS) {
		*magnitude = text->mantissa;
		retval = true;
	}

	else if (text->integer_digits == JSON_NUMBER_INTEGER_DIGITS) {
		// the mantissa has the first digits and the last one is added, if it fits
		const u64 last = (u64) (text->digits[JSON_NUMBER_MANTISSA_DIGITS] - JSON_NUMBER_ZERO);
		if (text->mantissa <= ((UINT64_MAX - last) / JSON_NUMBER_BASE)) {
			*magnitude = (text->mantissa * JSON_NUMBER_BASE) + last;
			retval = true;
		}
	}

	return retval;

}

static CerverJsonErrorCode json_number_integer (
	const JsonNumberText *text, const unsigned int flags, JsonNumber *number
) {

	u64 magnitude = 0;

	const bool fits = json_number_magnitude (text, &magnitude)
		&& (!text->negative || (magnitude <= JSON_NUMBER_INTEGER_MAGNITUDE));

	if (!fits) {
		return (flags & CERVER_JSON_PARSE_BIG_AS_REAL) ?
			json_number_real (text, number) : CERVER_JSON_ERROR_RANGE;
	}

	if (text->negative) {
		number->type = CERVER_JSON_TYPE_INTEGER;
		number->value.integer = (magnitude == JSON_NUMBER_INTEGER_MAGNITUDE) ? INT64_MIN : -((i64) magnitude);
	}

	else if (magnitude < JSON_NUMBER_INTEGER_MAGNITUDE) {
		number->type = CERVER_JSON_TYPE_INTEGER;
		number->value.integer = (i64) magnitude;
	}

	else {
		number->type = CERVER_JSON_TYPE_UNSIGNED;
		number->value.natural = magnitude;
	}

	return CERVER_JSON_ERROR_NONE;

}

CerverJsonErrorCode json_number_read (JsonCursor *cursor, JsonNumber *number) {

	CerverJsonErrorCode error = CERVER_JSON_ERROR_NUMBER;

	JsonNumberText text = CERVER_ZERO_INITIALIZER;

	u8 *start = cursor->position;

	const u8 *end = json_number_text_read (&text, cursor);
	if (end) {
		error = text.real ?
			json_number_real (&text, number) : json_number_integer (&text, cursor->flags, number);

		// a number that does not fit is reported where it starts
		cursor->position = (error == CERVER_JSON_ERROR_NONE) ? (u8 *) end : start;
	}

	return error;

}

#pragma endregion

#pragma region integers

// the two digits of each number from 0 to 99
static const char JSON_NUMBER_PAIRS[] =
	"00010203040506070809" "10111213141516171819" "20212223242526272829" "30313233343536373839"
	"40414243444546474849" "50515253545556575859" "60616263646566676869" "70717273747576777879"
	"80818283848586878889" "90919293949596979899";

static const u64 JSON_NUMBER_PAIR_BASE = 100;
static const size_t JSON_NUMBER_PAIR_SIZE = 2;

size_t json_number_write_unsigned (char *buffer, u64 value) {

	char digits[JSON_NUMBER_INTEGER_DIGITS];
	size_t position = JSON_NUMBER_INTEGER_DIGITS;

	while (value >= JSON_NUMBER_PAIR_BASE) {
		const u64 pair = value % JSON_NUMBER_PAIR_BASE;
		value /= JSON_NUMBER_PAIR_BASE;

		position -= JSON_NUMBER_PAIR_SIZE;
		(void) memcpy (digits + position, JSON_NUMBER_PAIRS + (pair * JSON_NUMBER_PAIR_SIZE), JSON_NUMBER_PAIR_SIZE);
	}

	if (value >= JSON_NUMBER_BASE) {
		position -= JSON_NUMBER_PAIR_SIZE;
		(void) memcpy (digits + position, JSON_NUMBER_PAIRS + (value * JSON_NUMBER_PAIR_SIZE), JSON_NUMBER_PAIR_SIZE);
	}

	else {
		position -= 1;
		digits[position] = (char) (JSON_NUMBER_ZERO + value);
	}

	const size_t size = JSON_NUMBER_INTEGER_DIGITS - position;
	(void) memcpy (buffer, digits + position, size);

	return size;

}

size_t json_number_write_integer (char *buffer, const i64 value) {

	if (value < 0) {
		buffer[0] = (char) JSON_NUMBER_MINUS;

		// the magnitude of the smallest integer does not fit with sign
		return 1 + json_number_write_unsigned (buffer + 1, UINT64_C (0) - (u64) value);
	}

	return json_number_write_unsigned (buffer, (u64) value);

}

#pragma endregion

#pragma region shortest

// floor (log10 (2^e)) is (e * JSON_SHORTEST_LOG10_FACTOR) >> JSON_SHORTEST_LOG10_SHIFT, and with the
// correction the one of three quarters of 2^e; floor (log2 (10^e)) is the same with the other pair
static const i32 JSON_SHORTEST_LOG10_FACTOR = 1262611;
static const i32 JSON_SHORTEST_LOG10_CORRECTION = 524031;
#define JSON_SHORTEST_LOG10_SHIFT			22
static const i32 JSON_SHORTEST_LOG2_FACTOR = 1741647;
#define JSON_SHORTEST_LOG2_SHIFT			19

// the powers of five from 5^0 to this one are exact in the table; the ones from 5^-27 to 5^-1
// are rounded up there, and every other one is cut: its ceiling is the next integer
static const i32 JSON_SHORTEST_EXACT_LAST = 55;
static const i32 JSON_SHORTEST_CEILING_FIRST = -27;

// the interval of a double is measured in quarters of the distance to the next one
#define JSON_SHORTEST_SCALE					4
#define JSON_SHORTEST_HALF					2

// the least digits that read back a double: digits x 10^exponent
typedef struct JsonShortest {

	u64 digits;
	i32 exponent;

} JsonShortest;

// the ceiling of the 128 bits of 10^power
static JsonWide json_shortest_power (const i32 power) {

	const u64 *words = json_number_powers[power - JSON_NUMBER_POWERS_FIRST];

	JsonWide value = (((JsonWide) words[0]) << JSON_WORD_BITS) | words[1];

	if ((power < JSON_SHORTEST_CEILING_FIRST) || (power > JSON_SHORTEST_EXACT_LAST)) value += 1;

	return value;

}

// the high word of the product of a power and a value, with its last
// bit set when any bit below was: enough to compare it without them
static u64 json_shortest_round_to_odd (const JsonWide power, const u64 value) {

	const JsonWide low = ((JsonWide) value) * ((u64) power);
	const JsonWide high = (((JsonWide) value) * ((u64) (power >> JSON_WORD_BITS))) + ((u64) (low >> JSON_WORD_BITS));

	return ((u64) (high >> JSON_WORD_BITS)) | (((u64) high) > 1);

}

// the least digits of the interval of a double whose values round to it (Schubfach, by Giulietti):
// significand x 2^exponent, with the double below at half of the distance when it is a power of two
static JsonShortest json_shortest_interval (const u64 significand, const i32 exponent, const bool closer) {

	JsonShortest shortest = { 0, 0 };

	// the limits of an interval are part of it when its double is even
	const bool even = !(significand & 1);

	const u64 lower_value = (JSON_SHORTEST_SCALE * significand) - JSON_SHORTEST_HALF + (closer ? 1 : 0);
	const u64 value = JSON_SHORTEST_SCALE * significand;
	const u64 upper_value = (JSON_SHORTEST_SCALE * significand) + JSON_SHORTEST_HALF;

	const i32 power = (
		(exponent * JSON_SHORTEST_LOG10_FACTOR) - (closer ? JSON_SHORTEST_LOG10_CORRECTION : 0)
	) >> JSON_SHORTEST_LOG10_SHIFT;

	const i32 shift = exponent + ((-power * JSON_SHORTEST_LOG2_FACTOR) >> JSON_SHORTEST_LOG2_SHIFT) + 1;

	const JsonWide scale = json_shortest_power (-power);

	const u64 scaled_lower = json_shortest_round_to_odd (scale, lower_value << shift);
	const u64 scaled = json_shortest_round_to_odd (scale, value << shift);
	const u64 scaled_upper = json_shortest_round_to_odd (scale, upper_value << shift);

	const u64 lower = scaled_lower + (even ? 0 : 1);
	const u64 upper = scaled_upper - (even ? 0 : 1);

	const u64 digits = scaled / JSON_SHORTEST_SCALE;

	// with one digit less: at most one of the two candidates is inside
	if (digits >= JSON_NUMBER_BASE) {
		const u64 shorter = digits / JSON_NUMBER_BASE;
		const u64 step = JSON_SHORTEST_SCALE * JSON_NUMBER_BASE;
		const bool down_inside = lower <= (step * shorter);
		const bool up_inside = ((step * shorter) + step) <= upper;

		if (down_inside != up_inside) {
			shortest.digits = shorter + (up_inside ? 1 : 0);
			shortest.exponent = power + 1;
			return shortest;
		}
	}

	const bool down_inside = lower <= (JSON_SHORTEST_SCALE * digits);
	const bool up_inside = ((JSON_SHORTEST_SCALE * digits) + JSON_SHORTEST_SCALE) <= upper;

	shortest.exponent = power;

	if (down_inside != up_inside) {
		shortest.digits = digits + (up_inside ? 1 : 0);
	}

	else {
		// both or none: the closest one, the even one at the half
		const u64 middle = (JSON_SHORTEST_SCALE * digits) + JSON_SHORTEST_HALF;
		const bool up = (scaled > middle) || ((scaled == middle) && (digits & 1));

		shortest.digits = digits + (up ? 1 : 0);
	}

	return shortest;

}

// the least digits that read back a double that is not zero, by its mantissa and its exponent fields
static JsonShortest json_shortest_digits (const u64 mantissa, const i32 biased) {

	JsonShortest shortest = { 0, 0 };

	if (!biased) return json_shortest_interval (mantissa, 1 - JSON_DOUBLE_INTEGER_BIAS, false);

	const u64 significand = JSON_DOUBLE_HIDDEN_BIT | mantissa;
	const i32 exponent = biased - JSON_DOUBLE_INTEGER_BIAS;

	// an integer that a double has exactly is its own digits
	if (
		(exponent <= 0) && (exponent > -JSON_DOUBLE_SIGNIFICAND_BITS)
		&& !(significand & ((UINT64_C (1) << -exponent) - 1))
	) {
		shortest.digits = significand >> -exponent;
		return shortest;
	}

	return json_shortest_interval (significand, exponent, !mantissa && (biased > 1));

}

#pragma endregion

#pragma region write

// a real is written with its digits and its point while the exponent of its first digit is
// in this range, as "%.17g" does, and with an exponent outside it
static const i32 JSON_REAL_FIXED_MIN = -4;
static const i32 JSON_REAL_FIXED_MAX = 17;

static const char JSON_REAL_EXPONENT = 'e';

// what a real has after its digits when it has no fraction, so it is read back as a real
static const char JSON_REAL_INTEGER_END[] = ".0";

// what a real below one has before its digits
static const char JSON_REAL_FRACTION_START[] = "0.";

// d.ddde[-]x
static size_t json_number_write_exponential (
	char *buffer, const char *digits, const size_t count, const i32 scientific
) {

	size_t length = 0;

	buffer[length] = digits[0];
	length += 1;

	if (count > 1) {
		buffer[length] = (char) JSON_NUMBER_POINT;
		length += 1;

		(void) memcpy (buffer + length, digits + 1, count - 1);
		length += count - 1;
	}

	buffer[length] = JSON_REAL_EXPONENT;
	length += 1;

	if (scientific < 0) {
		buffer[length] = (char) JSON_NUMBER_MINUS;
		length += 1;
	}

	length += json_number_write_unsigned (buffer + length, (u64) ((scientific < 0) ? -scientific : scientific));

	return length;

}

// ddd000.0
static size_t json_number_write_whole (
	char *buffer, const char *digits, const size_t count, const i32 exponent
) {

	size_t length = count;

	(void) memcpy (buffer, digits, count);

	(void) memset (buffer + length, JSON_NUMBER_ZERO, (size_t) exponent);
	length += (size_t) exponent;

	(void) memcpy (buffer + length, JSON_REAL_INTEGER_END, sizeof (JSON_REAL_INTEGER_END) - 1);
	length += sizeof (JSON_REAL_INTEGER_END) - 1;

	return length;

}

// dd.ddd
static size_t json_number_write_point (
	char *buffer, const char *digits, const size_t count, const size_t whole
) {

	(void) memcpy (buffer, digits, whole);

	buffer[whole] = (char) JSON_NUMBER_POINT;

	(void) memcpy (buffer + whole + 1, digits + whole, count - whole);

	return count + 1;

}

// 0.000ddd
static size_t json_number_write_fraction (
	char *buffer, const char *digits, const size_t count, const size_t zeros
) {

	size_t length = sizeof (JSON_REAL_FRACTION_START) - 1;

	(void) memcpy (buffer, JSON_REAL_FRACTION_START, length);

	(void) memset (buffer + length, JSON_NUMBER_ZERO, zeros);
	length += zeros;

	(void) memcpy (buffer + length, digits, count);

	return length + count;

}

static size_t json_number_write_shortest (char *buffer, JsonShortest shortest) {

	char digits[JSON_NUMBER_INTEGER_DIGITS];

	// the zeros at the end are part of the exponent
	while (!(shortest.digits % JSON_NUMBER_BASE)) {
		shortest.digits /= JSON_NUMBER_BASE;
		shortest.exponent += 1;
	}

	const size_t count = json_number_write_unsigned (digits, shortest.digits);

	// the exponent of the first digit
	const i32 scientific = shortest.exponent + (i32) count - 1;

	if ((scientific < JSON_REAL_FIXED_MIN) || (scientific >= JSON_REAL_FIXED_MAX)) {
		return json_number_write_exponential (buffer, digits, count, scientific);
	}

	if (shortest.exponent >= 0) return json_number_write_whole (buffer, digits, count, shortest.exponent);

	if (scientific >= 0) return json_number_write_point (buffer, digits, count, (size_t) scientific + 1);

	return json_number_write_fraction (buffer, digits, count, (size_t) (-scientific - 1));

}

size_t json_number_write_real (char *buffer, const double value) {

	const u64 bits = json_number_to_bits (value);
	const u64 mantissa = bits & JSON_DOUBLE_MANTISSA_MASK;
	const i32 biased = (i32) ((bits >> JSON_DOUBLE_MANTISSA_BITS) & JSON_DOUBLE_EXPONENT_MASK);

	size_t length = 0;

	if (biased == JSON_DOUBLE_EXPONENT_SPECIAL) return 0;

	if (bits & JSON_DOUBLE_SIGN_BIT) {
		buffer[length] = (char) JSON_NUMBER_MINUS;
		length += 1;
	}

	if (!mantissa && !biased) {
		buffer[length] = (char) JSON_NUMBER_ZERO;
		(void) memcpy (buffer + length + 1, JSON_REAL_INTEGER_END, sizeof (JSON_REAL_INTEGER_END) - 1);

		return length + sizeof (JSON_REAL_INTEGER_END);
	}

	return length + json_number_write_shortest (buffer + length, json_shortest_digits (mantissa, biased));

}

void cerver_json_number_power (const int power, u64 *high, u64 *low) {

	*high = json_number_powers[power - JSON_NUMBER_POWERS_FIRST][0];
	*low = json_number_powers[power - JSON_NUMBER_POWERS_FIRST][1];

}

#pragma endregion
