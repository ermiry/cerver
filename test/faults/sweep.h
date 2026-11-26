#ifndef _CERVER_TESTS_FAULTS_SWEEP_H_
#define _CERVER_TESTS_FAULTS_SWEEP_H_

#include "faults.h"

// runs the path with each of its calls to malloc (), calloc (), realloc () and pthread_create () failing,
// one in each run: a run with a failure leaves alive at most what a run without one leaves
extern void faults_sweep_path (const char *name, FaultsSweepWork work, void *data);

extern void faults_tests_sweep (void);

#endif
