#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <cerver/utils/sha256.h>

#include "../test.h"

struct string_vector {

	const char *input;
	const char *output;

};

static const struct string_vector STRING_VECTORS[] = {
	{
		"",
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
	},
	{
		"abc",
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
	},
	{
		"hola12",
		"049ec1af7c1332193d602986f2fdad5b4d1c2ff90e5cdc65388c794c1f10226b"
	},
	{
		"adios",
		"d8542114d7d40f3c82fc0919efc644df30f4e827c2bd6b83b9dbec8358b2fbc4"
	},
	{
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
		"a8ae6e6ee929abea3afcfc5258c8ccd6f85273e0d4626d26c7279f3250f77c8e"
	},
	{
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde",
		"057ee79ece0b9a849552ab8d3c335fe9a5f1c46ef5f1d9b190c295728628299c"
	},
	{
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
		"2a6ad82f3620d3ebe9d678c812ae12312699d673240d5be8fac0910a70000d93"
	},
	{
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
	},
	{
		"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
		"ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
		"cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"
	}
};

static void string_test (const char input[], const char output[]) {

	char hash_string[SHA256_STRING_LEN] = { 0 };
	sha256_generate (hash_string, input, strlen (input));

	test_check_str_eq (hash_string, output, NULL);

}

void utils_tests_sha256 (void) {

	(void) printf ("Testing UTILS sha256...\n");

	for (size_t i = 0; i < (sizeof STRING_VECTORS / sizeof (struct string_vector)); i++) {
		const struct string_vector *vector = &STRING_VECTORS[i];
		string_test (vector->input, vector->output);
	}

	(void) printf ("Done!\n");

}