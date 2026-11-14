#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/resource.h>

#include "measure.h"

// the allocators of glibc, exported for the programs that replace malloc ()
extern void *__libc_malloc (size_t size);
extern void *__libc_calloc (size_t count, size_t size);
extern void *__libc_realloc (void *pointer, size_t size);
extern void *__libc_memalign (size_t alignment, size_t size);

#define MEASURE_NANOSECONDS					1e-9
#define MEASURE_MEGABYTE					(1024.0 * 1024.0)

// the iterations of the first calibration, doubled until a run takes MEASURE_RUN_SECONDS / 4
#define MEASURE_CALIBRATION_ITERATIONS		1
#define MEASURE_CALIBRATION_FRACTION		4.0

static uint64_t measure_allocated = 0;

static void measure_count (void) {

	(void) __atomic_add_fetch (&measure_allocated, 1, __ATOMIC_RELAXED);

}

void *malloc (size_t size) {

	measure_count ();

	return __libc_malloc (size);

}

void *calloc (size_t count, size_t size) {

	measure_count ();

	return __libc_calloc (count, size);

}

void *realloc (void *pointer, size_t size) {

	measure_count ();

	return __libc_realloc (pointer, size);

}

void *aligned_alloc (size_t alignment, size_t size) {

	measure_count ();

	return __libc_memalign (alignment, size);

}

int posix_memalign (void **pointer, size_t alignment, size_t size) {

	measure_count ();

	*pointer = __libc_memalign (alignment, size);

	return *pointer ? 0 : ENOMEM;

}

uint64_t measure_allocations (void) {

	return __atomic_load_n (&measure_allocated, __ATOMIC_RELAXED);

}

double measure_now (void) {

	struct timespec now;
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec * MEASURE_NANOSECONDS);

}

static int measure_compare (const void *first, const void *second) {

	const double a = *((const double *) first);
	const double b = *((const double *) second);

	return (a > b) - (a < b);

}

// runs the iterations, returns the seconds that they took, a negative value when one failed
static double measure_iterations (MeasureWork work, void *data, const uint64_t iterations) {

	const double start = measure_now ();

	bool succeeded = true;
	for (uint64_t i = 0; succeeded && (i < iterations); i++) {
		succeeded = work (data);
	}

	return succeeded ? (measure_now () - start) : -1.0;

}

// the iterations of a run of MEASURE_RUN_SECONDS, 0 when an iteration failed
static uint64_t measure_calibrate (MeasureWork work, void *data) {

	uint64_t iterations = MEASURE_CALIBRATION_ITERATIONS;

	double elapsed = measure_iterations (work, data, iterations);
	while ((elapsed >= 0.0) && (elapsed < (MEASURE_RUN_SECONDS / MEASURE_CALIBRATION_FRACTION))) {
		iterations <<= 1;
		elapsed = measure_iterations (work, data, iterations);
	}

	return (elapsed < 0.0) ? 0 : (uint64_t) ((double) iterations * (MEASURE_RUN_SECONDS / elapsed)) + 1;

}

static long measure_rss_kib (void) {

	struct rusage usage;
	(void) getrusage (RUSAGE_SELF, &usage);

	return usage.ru_maxrss;

}

bool measure_run (
	MeasureWork work, void *data, const size_t bytes, MeasureResult *result
) {

	(void) memset (result, 0, sizeof (MeasureResult));

	const uint64_t iterations = measure_calibrate (work, data);
	bool succeeded = (iterations > 0);

	double rates[MEASURE_RUNS];
	for (unsigned int run = 0; succeeded && (run < MEASURE_RUNS); run++) {
		const uint64_t allocated = measure_allocations ();
		const double elapsed = measure_iterations (work, data, iterations);
		const uint64_t allocations = measure_allocations () - allocated;

		succeeded = (elapsed > 0.0);
		rates[run] = succeeded ? ((double) iterations / elapsed) : 0.0;
		result->allocations = (double) allocations / (double) iterations;
	}

	if (succeeded) {
		qsort (rates, MEASURE_RUNS, sizeof (double), measure_compare);
		result->items_per_second = rates[MEASURE_RUNS / 2];
		result->megabytes_per_second = (result->items_per_second * (double) bytes) / MEASURE_MEGABYTE;
		result->rss_kib = measure_rss_kib ();
	}

	return succeeded;

}

void measure_print (
	const char *module, const char *name, const char *implementation, const char *operation,
	const MeasureResult *result
) {

	(void) printf (
		"%-8s %-10s %-16s %-10s %14.1f %10.1f %10.2f %10ld\n",
		module, name, implementation, operation,
		result->items_per_second, result->megabytes_per_second, result->allocations, result->rss_kib
	);

	(void) fflush (stdout);

}
