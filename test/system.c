#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/system.h>

#include "test.h"

#define SYSTEM_TEST_PATH				"/"
#define SYSTEM_TEST_MISSING_PATH		"/cerver-system-test-missing"
#define SYSTEM_TEST_PERCENTAGE			100.0
#define SYSTEM_TEST_MIN_OPEN_FILES		3
#define SYSTEM_TEST_HOSTNAME_SHORT		2
#define SYSTEM_TEST_BUSY_SECONDS		0.032
#define SYSTEM_TEST_FILE_PATH			"/proc/self/status"

static void test_system_get_available_space (void) {

	unsigned long available_space = 0;
	test_check_true (system_get_available_space (SYSTEM_TEST_PATH, &available_space));
	test_check_unsigned_gt (available_space, 0);

	test_check_false (system_get_available_space (SYSTEM_TEST_MISSING_PATH, &available_space));

}

static void test_system_get_used_space (void) {

	unsigned long used_space = 0;
	test_check_true (system_get_used_space (SYSTEM_TEST_PATH, &used_space));
	test_check_unsigned_gt (used_space, 0);

	test_check_false (system_get_used_space (SYSTEM_TEST_MISSING_PATH, &used_space));

}

static void test_system_get_stats (void) {

	FileSystemStats stats = CERVER_ZERO_INITIALIZER;
	test_check_true (system_get_stats (SYSTEM_TEST_PATH, &stats));
	test_check (stats.total > 0, "the file system has no space");
	test_check (stats.available <= stats.total, "more space is available than the total");
	test_check ((stats.used_percentage >= 0) && (stats.used_percentage <= SYSTEM_TEST_PERCENTAGE), NULL);

	system_print_stats (&stats);

}

static void test_system_get_memory (void) {

	SystemMemory memory = CERVER_ZERO_INITIALIZER;
	test_check_true (system_get_memory (&memory));
	test_check_unsigned_gt (memory.total, 0);
	test_check (memory.free <= memory.total, "more memory is free than the total");
	test_check (memory.available <= memory.total, "more memory is available than the total");
	test_check (memory.swap_free <= memory.swap_total, "more swap is free than the total");

	test_check_false (system_get_memory (NULL));

	system_print_memory (&memory);

}

static void test_system_get_load (void) {

	SystemLoad load = CERVER_ZERO_INITIALIZER;
	test_check_true (system_get_load (&load));
	test_check_unsigned_gt (load.cpus, 0);
	test_check_unsigned_gt (load.processes, 0);

	for (unsigned int index = 0; index < SYSTEM_LOADS; index++) {
		test_check (load.loads[index] >= 0, "a negative load average");
	}

	test_check_false (system_get_load (NULL));

	system_print_load (&load);

}

// keeps the cpu busy, so the second sample has more ticks
static void system_test_busy (void) {

	const double deadline = test_wait_now () + SYSTEM_TEST_BUSY_SECONDS;

	unsigned int loops = 0;
	while (test_wait_now () < deadline) {
		loops += 1;
	}

	test_check_unsigned_gt (loops, 0);

}

static void test_system_cpu_usage (void) {

	SystemCpuSample first = { 0, 0 };
	SystemCpuSample second = { 0, 0 };

	test_check_true (system_cpu_sample (&first));
	test_check (first.busy <= first.total, "more busy time than the total");

	system_test_busy ();

	test_check_true (system_cpu_sample (&second));
	test_check (second.total >= first.total, "the cpu times went back");

	const double usage = system_cpu_usage (&first, &second);
	test_check ((usage >= 0) && (usage <= SYSTEM_TEST_PERCENTAGE), "the usage is not a percentage");

	// the same sample, or the samples in the wrong order
	test_check (test_double_equal (system_cpu_usage (&first, &first), 0), NULL);
	test_check (test_double_equal (system_cpu_usage (&second, &first), 0), NULL);
	test_check (test_double_equal (system_cpu_usage (NULL, &second), 0), NULL);

	// every tick was busy
	const SystemCpuSample busy = { first.busy + 1, first.total + 1 };
	test_check (test_double_equal (system_cpu_usage (&first, &busy), SYSTEM_TEST_PERCENTAGE), NULL);

	test_check_false (system_cpu_sample (NULL));

}

static void test_system_get_hostname (void) {

	char hostname[SYSTEM_HOSTNAME_SIZE] = { 0 };
	test_check_true (system_get_hostname (hostname, SYSTEM_HOSTNAME_SIZE));
	test_check (strlen (hostname) > 0, "the hostname is empty");

	// a name that does not fit is an error that leaves the buffer empty
	char short_hostname[SYSTEM_TEST_HOSTNAME_SHORT] = { 'x', 'x' };
	if (strlen (hostname) >= SYSTEM_TEST_HOSTNAME_SHORT) {
		test_check_false (system_get_hostname (short_hostname, SYSTEM_TEST_HOSTNAME_SHORT));
		test_check_str_empty (short_hostname);
	}

	test_check_false (system_get_hostname (NULL, SYSTEM_HOSTNAME_SIZE));
	test_check_false (system_get_hostname (hostname, 0));

}

static void test_system_get_process (void) {

	SystemProcess process = CERVER_ZERO_INITIALIZER;
	test_check_true (system_get_process (&process));
	test_check_unsigned_gt (process.resident, 0);
	test_check_unsigned_gt (process.threads, 0);
	test_check (process.open_files >= SYSTEM_TEST_MIN_OPEN_FILES, "the process does not have its standard files");

	// a file that is opened is counted
	FILE *file = fopen (SYSTEM_TEST_FILE_PATH, "r");
	test_check_ptr (file);

	SystemProcess opened = CERVER_ZERO_INITIALIZER;
	test_check_true (system_get_process (&opened));
	test_check_unsigned_eq (opened.open_files, process.open_files + 1, NULL);

	(void) fclose (file);

	test_check_false (system_get_process (NULL));

	system_print_process (&process);

}

int main (void) {

	test_output_init ();

	(void) printf ("Testing SYSTEM...\n");

	test_system_get_available_space ();
	test_system_get_used_space ();
	test_system_get_stats ();
	test_system_get_memory ();
	test_system_get_load ();
	test_system_cpu_usage ();
	test_system_get_hostname ();
	test_system_get_process ();

	(void) printf ("\nDone with SYSTEM tests!\n\n");

	return 0;

}
