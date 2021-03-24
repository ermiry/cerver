#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/system.h>

static void test_system_get_available_space (void) {

	unsigned long available_space = 0;
	(void) system_get_available_space ("/", &available_space);
	(void) printf ("Available: %lu\n\n", available_space);

}

static void test_system_get_used_space (void) {

	unsigned long used_space = 0;
	(void) system_get_available_space ("/", &used_space);
	(void) printf ("Used: %lu\n\n", used_space);

}

static void test_system_get_stats (void) {

	FileSystemStats stats = { 0 };
	(void) system_get_stats ("/", &stats);
	system_print_stats (&stats);

}

int main (int argc, char **argv) {

	(void) printf ("Testing SYSTEM...\n");

	test_system_get_available_space ();

	test_system_get_used_space ();

	test_system_get_stats ();

	(void) printf ("\nDone with SYSTEM tests!\n\n");

	return 0;

}