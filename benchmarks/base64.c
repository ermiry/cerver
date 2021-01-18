#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/utils/base64.h>

#include "bench.h"

static const int repeat = 32;

const char moby_dick_plain[] =
    "Call me Ishmael. Some years ago--never mind how long precisely--having\n"
    "little or no money in my purse, and nothing particular to interest me on\n"
    "shore, I thought I would sail about a little and see the watery part of\n"
    "the world. It is a way I have of driving off the spleen and regulating\n"
    "the circulation. Whenever I find myself growing grim about the mouth;\n"
    "whenever it is a damp, drizzly November in my soul; whenever I find\n"
    "myself involuntarily pausing before coffin warehouses, and bringing up\n"
    "the rear of every funeral I meet; and especially whenever my hypos get\n"
    "such an upper hand of me, that it requires a strong moral principle to\n"
    "prevent me from deliberately stepping into the street, and methodically\n"
    "knocking people's hats off--then, I account it high time to get to sea\n"
    "as soon as I can. This is my substitute for pistol and ball. With a\n"
    "philosophical flourish Cato throws himself upon his sword; I quietly\n"
    "take to the ship. There is nothing surprising in this. If they but knew\n"
    "it, almost all men in their degree, some time or other, cherish very\n"
    "nearly the same feelings towards the ocean with me.\n";

const char moby_dick_base64[] =
    "Q2FsbCBtZSBJc2htYWVsLiBTb21lIHllYXJzIGFnby0tbmV2ZXIgbWluZCBob3cgbG9uZ"
    "yBwcmVjaXNlbHktLWhhdmluZwpsaXR0bGUgb3Igbm8gbW9uZXkgaW4gbXkgcHVyc2UsIG"
    "FuZCBub3RoaW5nIHBhcnRpY3VsYXIgdG8gaW50ZXJlc3QgbWUgb24Kc2hvcmUsIEkgdGh"
    "vdWdodCBJIHdvdWxkIHNhaWwgYWJvdXQgYSBsaXR0bGUgYW5kIHNlZSB0aGUgd2F0ZXJ5"
    "IHBhcnQgb2YKdGhlIHdvcmxkLiBJdCBpcyBhIHdheSBJIGhhdmUgb2YgZHJpdmluZyBvZ"
    "mYgdGhlIHNwbGVlbiBhbmQgcmVndWxhdGluZwp0aGUgY2lyY3VsYXRpb24uIFdoZW5ldm"
    "VyIEkgZmluZCBteXNlbGYgZ3Jvd2luZyBncmltIGFib3V0IHRoZSBtb3V0aDsKd2hlbmV"
    "2ZXIgaXQgaXMgYSBkYW1wLCBkcml6emx5IE5vdmVtYmVyIGluIG15IHNvdWw7IHdoZW5l"
    "dmVyIEkgZmluZApteXNlbGYgaW52b2x1bnRhcmlseSBwYXVzaW5nIGJlZm9yZSBjb2Zma"
    "W4gd2FyZWhvdXNlcywgYW5kIGJyaW5naW5nIHVwCnRoZSByZWFyIG9mIGV2ZXJ5IGZ1bm"
    "VyYWwgSSBtZWV0OyBhbmQgZXNwZWNpYWxseSB3aGVuZXZlciBteSBoeXBvcyBnZXQKc3V"
    "jaCBhbiB1cHBlciBoYW5kIG9mIG1lLCB0aGF0IGl0IHJlcXVpcmVzIGEgc3Ryb25nIG1v"
    "cmFsIHByaW5jaXBsZSB0bwpwcmV2ZW50IG1lIGZyb20gZGVsaWJlcmF0ZWx5IHN0ZXBwa"
    "W5nIGludG8gdGhlIHN0cmVldCwgYW5kIG1ldGhvZGljYWxseQprbm9ja2luZyBwZW9wbG"
    "UncyBoYXRzIG9mZi0tdGhlbiwgSSBhY2NvdW50IGl0IGhpZ2ggdGltZSB0byBnZXQgdG8"
    "gc2VhCmFzIHNvb24gYXMgSSBjYW4uIFRoaXMgaXMgbXkgc3Vic3RpdHV0ZSBmb3IgcGlz"
    "dG9sIGFuZCBiYWxsLiBXaXRoIGEKcGhpbG9zb3BoaWNhbCBmbG91cmlzaCBDYXRvIHRoc"
    "m93cyBoaW1zZWxmIHVwb24gaGlzIHN3b3JkOyBJIHF1aWV0bHkKdGFrZSB0byB0aGUgc2"
    "hpcC4gVGhlcmUgaXMgbm90aGluZyBzdXJwcmlzaW5nIGluIHRoaXMuIElmIHRoZXkgYnV"
    "0IGtuZXcKaXQsIGFsbW9zdCBhbGwgbWVuIGluIHRoZWlyIGRlZ3JlZSwgc29tZSB0aW1l"
    "IG9yIG90aGVyLCBjaGVyaXNoIHZlcnkKbmVhcmx5IHRoZSBzYW1lIGZlZWxpbmdzIHRvd"
    "2FyZHMgdGhlIG9jZWFuIHdpdGggbWUuCg==";

const char googlelogo_base64[] =
    "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";

const char bingsocialicon_base64[] =
    "iVBORw0KGgoAAAANSUhEUgAAAFEAAAASCAYAAAAjQzL0AAAFEklEQVRYR+WYT4hVVRzH54"
	"02SSozaoFJoOSAJswfMIgwcDAMQrBpUoQ2zkpoMeLSlai4dCFuatcYtlJEc9QEjUHaBCJK"
	"kRGOzsKNlTloTjiDb/p8f+937tx73733vfsqCPrCl3N+/8657/fOOb9zb6UNzM3NLalUKn"
	"+oX4RqtbqBZj98G87AC/Boe3v7L4yxEf5J/0f8nqB/CbaCacZY6v06MMcgzT642eVbNKM8"
	"f5dkcFNEnqyJrYFxO+AndHfCZfA6PMyzTdAmwQ9+EZ6Hb7oqE9g3wccMnAC6n+A1+B18zX"
	"2fu7k0FGsTZgDzaM2rMRjnLE2/h5YCcV3EX9SzOGd9zAew293mgW0lhqfwIXzf1Qngswjb"
	"9xooC9iEr+nukT/9ZpN4He7Efys8LIVibdIUMA3K3gKGfYimgP8ynuEqFO7CIdgDz2sw2h"
	"PuOg+Ur8Df3EFZPw5Xu9mA3Aer8skD5os0tqXoN0wiPqdtcAfyR67PTCJ6raw8jMODcBS/"
	"SdMk0VQi8esk/rKeAU4gr3eT5lciZ+FtVyWBQQmIYwrdl3A3XA3fdX0msOtPeNmHazaJy9"
	"13C7wEb7o+byXWAV8lNpyFEdANhPFiKNza2CtQSVwFd8F1bjIg74D6XeOumgfKhRjfgw/o"
	"1wH9M6itnrsSMd2HUTGg30wSzZ/2kqsMirVBUnBzHFPQEkg7SNxZeIz+GtcNwAiyS58FzB"
	"3Y90Kd7XfgadjrZj3jOngPaoWOuHoeKF+Ft+HvtenKg9hbNO0+ZGESsWlLPIYhifrxkp+6"
	"PW87p1dWtCJcDlBytRL31cQELMFxoNMZGLawFREoaOEsx76edsI01eoF5A4PTQLjmGZoFc"
	"Sf9KEMyEVJHHO3BNB/4Pa8JCaSgqyrjAExrMSs8zAOXY8iICuB38hAe0922m54BJ6EqgUh"
	"gVew1x0dETCuwekHDdYKiN3uQxmQi5Kof/MFd9XcCySj/9jtedtZhSONuoKBTr9FCdeKTO"
	"Ogu8lP15hQhZUoJUx1QAnsxq6EagsLxQkMwOkdqCpXFrqmLPRhDExalERB2yZs53AfszNX"
	"fRskBUxZSZS/isswTGxV5H7ZU7Ak0uoM1LyCVWHaFdDOfqAtHba3zuzGCRRwtpVQForzIS"
	"JocjfnAp8oia4yKNYGSQFT3T0R38Q5iaztHF9tiUWBXW86mnNE80DdA6MqTL8XqqiouKjI"
	"jBCWfQbmgaBDsGECAvD9wkMTaGYMfMomsavmUQN+dibSTVRhMGUBgH46iSFmXPPAIXN0IK"
	"sK63qzCp9OaK/FpUHg6wzyGW0h8NF2WOxhCaAPW6GIIYljKX1mEgVs0YWbfvRujBgKi+wD"
	"rlPS685F2fDTbURbtkeyQP8NqJWpZ7iMa6ebGoOAxVDvz6Ju5VqNqlS5wK7KZUnIAjY9yE"
	"wB9S66xH314+O23CQSkzjnFEuTddlWcUlfiQyyYzuhPq2+Geg369VuEgpXMemDQ/Mg6C34"
	"FfxVAxcBn5/hEN3CZY6Pkqh/Op6cOJVEvbMvhefiepibRIE4FZE0tD2PierXVJmwuyV+us"
	"bYywVtvIhEr65lED6FrYRb6G6D+tylpayK+ww+gjfgmUql8i2cpl8IHuZf+xQm8Ky62nxe"
	"k0rhQ57f3lx4xrU0B6C+Xuk3nsL2KdQnvv8HSGQ/iYjOyCYw6qH/OFqrPv8hkBxtPxUT+8"
	"CArOo8zIrqc/kcfX20zX13/ntoa/sLfuPonSru/8QAAAAASUVORK5CYII=";

static void test_encode (
	const char *data, size_t datalength, bool verbose
) {

	if (verbose) (void) printf ("encode a base64 input of  %zu bytes, ",datalength);
	char *buffer = (char *) malloc (datalength * 2);
	size_t expected = chromium_base64_encode (buffer, data, datalength);
	if (verbose) (void) printf ("encoded size = %zu \n", expected);
	// BEST_TIME (memcpy (buffer, data, datalength), repeat, datalength, verbose);
	BEST_TIME (chromium_base64_encode (buffer, data, datalength), (int) expected, repeat, datalength, verbose);
	BEST_TIME (fast_avx2_base64_encode (buffer, data, datalength), (int) expected, repeat, datalength, verbose);

	free (buffer);

	if (verbose) (void) printf ("\n");

}

static void test_decode (
	const char * data, size_t datalength, bool verbose
) {

	if (verbose) (void) printf ("decoding a base64 input of  %zu bytes, ", datalength);

	if (datalength < 4 || (datalength % 4 != 0)) {
		(void) printf ("size should be divisible by 4 bytes.\n");
		return;
	}

	char *buffer = malloc (datalength * 2);
	size_t expected = chromium_base64_decode (buffer, data, datalength);
	if (verbose) (void) printf ("original size = %zu \n", expected);

	// BEST_TIME (memcpy (buffer, data, datalength), repeat, datalength, verbose);
	BEST_TIME (chromium_base64_decode (buffer, data, datalength), (int) expected, repeat, datalength, verbose);
	BEST_TIME (fast_avx2_base64_decode (buffer, data, datalength), (int) expected, repeat, datalength, verbose);

	free (buffer);
	if (verbose) (void) printf("\n");

}

// note sources should be compiled with optmization flags to get the best results
int main (int argc, const char **argv) {

	RDTSC_SET_OVERHEAD (rdtsc_overhead_func (1), repeat);

	(void) printf ("moby_dick ENCODE [text]\n");
	test_encode (moby_dick_base64, sizeof (moby_dick_base64) - 1, true);
	(void) printf ("google logo ENCODE [png]\n");
	test_encode (googlelogo_base64, sizeof (googlelogo_base64) - 1, true);
	(void) printf ("bing.com social icons ENCODE [png]\n");
	test_encode (bingsocialicon_base64, sizeof (bingsocialicon_base64) - 1, true);

	(void) printf ("moby_dick DECODE [text]\n");
	test_decode (moby_dick_base64, sizeof (moby_dick_base64) - 1, true);
	(void) printf ("google logo DECODE [png]\n");
	test_decode (googlelogo_base64, sizeof (googlelogo_base64) - 1, true);
	(void) printf ("bing.com social icons DECODE [png]\n");
	test_decode (bingsocialicon_base64, sizeof (bingsocialicon_base64) - 1, true);

	return 0;

}