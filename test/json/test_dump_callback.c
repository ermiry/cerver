/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/json/json.h>

#include "json.h"

struct my_sink {
    char *buf;
    size_t off;
    size_t cap;
};

static int my_writer(const char *buffer, size_t len, void *data) {
    struct my_sink *s = (struct my_sink *) data;
    if (len > s->cap - s->off) {
        return -1;
    }
    memcpy(s->buf + s->off, buffer, len);
    s->off += len;
    return 0;
}

static void start (void) {
    struct my_sink s;
    json_t *json;
    const char str[] = "[\"A\", {\"B\": \"C\", \"e\": false}, 1, null, \"foo\"]";
    char *dumped_to_string;

    json = json_loads(str, 0, NULL);
    if (!json) {
        fail("json_loads failed");
    }

    dumped_to_string = json_dumps(json, 0);
    if (!dumped_to_string) {
        json_decref(json);
        fail("json_dumps failed");
    }

    s.off = 0;
    s.cap = strlen(dumped_to_string);
    s.buf = (char *) malloc(s.cap);
    if (!s.buf) {
        json_decref(json);
        free(dumped_to_string);
        fail("malloc failed");
    }

    if (json_dump_callback(json, my_writer, &s, 0) == -1) {
        json_decref(json);
        free(dumped_to_string);
        free(s.buf);
        fail("json_dump_callback failed on an exact-length sink buffer");
    }

    if (strncmp(dumped_to_string, s.buf, s.off) != 0) {
        json_decref(json);
        free(dumped_to_string);
        free(s.buf);
        fail("json_dump_callback and json_dumps did not produce identical "
             "output");
    }

    s.off = 1;
    if (json_dump_callback(json, my_writer, &s, 0) != -1) {
        json_decref(json);
        free(dumped_to_string);
        free(s.buf);
        fail("json_dump_callback succeeded on a short buffer when it should "
             "have failed");
    }

    json_decref(json);
    free(dumped_to_string);
    free(s.buf);
}

void json_tests_dump_cb (void) {

    (void) printf ("Testing JSON dump callback...\n");

    start ();

    (void) printf ("Done!\n");

}