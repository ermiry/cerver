#ifndef _CERVER_BENCHMARKS_COMPETITORS_MEASURE_H_
#define _CERVER_BENCHMARKS_COMPETITORS_MEASURE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// the runs of each measure, its result is their median
#define MEASURE_RUNS						5

// the seconds that a run of a measure takes, its iterations are calibrated to it
#define MEASURE_RUN_SECONDS					0.25

#ifdef __cplusplus
extern "C" {
#endif

// one iteration of what is measured, it returns false when it failed
typedef bool (*MeasureWork) (void *data);

typedef struct MeasureResult {

	// the median of the runs
	double items_per_second;
	double megabytes_per_second;

	// the allocations of one iteration, in one of the runs
	double allocations;

	// the most memory of the process until now
	long rss_kib;

} MeasureResult;

// the seconds of a monotonic clock
extern double measure_now (void);

// the allocations of the process, counted by the malloc () family of this program
extern uint64_t measure_allocations (void);

// measures the work, each iteration takes the bytes of its input
// returns false when an iteration failed
extern bool measure_run (
	MeasureWork work, void *data, const size_t bytes, MeasureResult *result
);

// prints a line of the results: module, case, implementation, operation and the values
extern void measure_print (
	const char *module, const char *name, const char *implementation, const char *operation,
	const MeasureResult *result
);

#ifdef __cplusplus
}
#endif

#endif
