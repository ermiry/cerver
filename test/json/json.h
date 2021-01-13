#ifndef _CERVER_TESTS_JSON_H_
#define _CERVER_TESTS_JSON_H_

#if HAVE_LOCALE_H
#include <locale.h>
#endif

extern void json_tests_array (void);

extern void json_tests_chaos (void);

extern void json_tests_copy (void);

extern void json_tests_dump_cb (void);

extern void json_tests_dump (void);

extern void json_tests_equal (void);

extern void json_tests_load_cb (void);

extern void json_tests_load (void);

extern void json_tests_loadb (void);

extern void json_tests_memory (void);

extern void json_tests_numbers (void);

extern void json_tests_objects (void);

extern void json_tests_pack (void);

extern void json_tests_simple (void);

extern void json_tests_sprintf (void);

extern void json_tests_unpack (void);

#endif