#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerver/utils/log.h>

#include "../test.h"
#include "utils.h"

#define LOG_TEST_PATH_FORMAT			"/tmp/cerver-log-%d"
#define LOG_TEST_PATH_SIZE				64
#define LOG_TEST_FILE_SIZE				320
#define LOG_TEST_MESSAGE				"a message in the log file"
#define LOG_TEST_UPDATE_INTERVAL		1
#define LOG_TEST_FLUSH_NANOSECONDS		200000000L
#define LOG_TEST_END_MAX_SECONDS		0.5

// returns the size of the first log file in the directory, and removes it
static long log_test_file_size (const char *path) {

	long size = -1;

	DIR *directory = opendir (path);
	test_check_ptr (directory);

	const struct dirent *entry = NULL;
	while ((entry = readdir (directory))) {
		if (entry->d_name[0] != '.') {
			char filename[LOG_TEST_FILE_SIZE] = { 0 };
			(void) snprintf (filename, LOG_TEST_FILE_SIZE, "%s/%s", path, entry->d_name);

			struct stat file_stat;
			if (!stat (filename, &file_stat)) size = (long) file_stat.st_size;

			(void) unlink (filename);
		}
	}

	(void) closedir (directory);

	return size;

}

// the update thread syncs the file every interval, the end wakes it up and joins it
// before the file is closed, it used to sleep in a detached thread that could flush a closed file
static void test_log_file (void) {

	char path[LOG_TEST_PATH_SIZE] = { 0 };
	(void) snprintf (path, LOG_TEST_PATH_SIZE, LOG_TEST_PATH_FORMAT, (int) getpid ());

	cerver_log_set_output_type (LOG_OUTPUT_TYPE_FILE);
	test_check_unsigned_eq (cerver_log_set_path (path), 0, NULL);
	cerver_log_set_update_interval (LOG_TEST_UPDATE_INTERVAL);

	cerver_log_init ();

	cerver_log_msg (LOG_TEST_MESSAGE);

	const struct timespec flush = { 0, LOG_TEST_FLUSH_NANOSECONDS };
	(void) nanosleep (&flush, NULL);

	const double start = test_wait_now ();
	cerver_log_end ();
	test_check ((test_wait_now () - start) < LOG_TEST_END_MAX_SECONDS, "the end waited the update interval");

	test_check (log_test_file_size (path) > 0, "the log file is empty");
	(void) rmdir (path);

	cerver_log_set_output_type (LOG_OUTPUT_TYPE_STD);

}

void utils_tests_log (void) {

	(void) printf ("Testing UTILS log...\n");

	test_log_file ();

	(void) printf ("Done!\n");

}
