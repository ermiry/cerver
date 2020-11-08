#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdint.h>
#include <unistd.h>

#include <assert.h>
#include <errno.h>
#include <math.h>

#include "cerver/http/json/json.h"
#include "cerver/http/json/private.h"
#include "cerver/http/json/config.h"
#include "cerver/http/json/hashtable.h"

#pragma region error

void jsonp_error_init(json_error_t *error, const char *source) {
    if (error) {
        error->text[0] = '\0';
        error->line = -1;
        error->column = -1;
        error->position = 0;
        if (source)
            jsonp_error_set_source(error, source);
        else
            error->source[0] = '\0';
    }
}

void jsonp_error_set_source(json_error_t *error, const char *source) {
    size_t length;

    if (!error || !source)
        return;

    length = strlen(source);
    if (length < JSON_ERROR_SOURCE_LENGTH)
        strncpy(error->source, source, length + 1);
    else {
        size_t extra = length - JSON_ERROR_SOURCE_LENGTH + 4;
        memcpy(error->source, "...", 3);
        strncpy(error->source + 3, source + extra, length - extra + 1);
    }
}

void jsonp_error_set(json_error_t *error, int line, int column, size_t position,
                     enum json_error_code code, const char *msg, ...) {
    va_list ap;

    va_start(ap, msg);
    jsonp_error_vset(error, line, column, position, code, msg, ap);
    va_end(ap);
}

void jsonp_error_vset(json_error_t *error, int line, int column, size_t position,
                      enum json_error_code code, const char *msg, va_list ap) {
    if (!error)
        return;

    if (error->text[0] != '\0') {
        /* error already set */
        return;
    }

    error->line = line;
    error->column = column;
    error->position = (int)position;

    vsnprintf(error->text, JSON_ERROR_TEXT_LENGTH - 1, msg, ap);
    error->text[JSON_ERROR_TEXT_LENGTH - 2] = '\0';
    error->text[JSON_ERROR_TEXT_LENGTH - 1] = code;
}

#pragma endregion

#pragma region memory

// #include <stdlib.h>
// #include <string.h>

// #include "jansson.h"
// #include "jansson_private.h"

// /* C89 allows these to be macros */
// #undef malloc
// #undef free

/* memory function pointers */
static json_malloc_t do_malloc = malloc;
static json_free_t do_free = free;

void *jsonp_malloc(size_t size) {
    if (!size)
        return NULL;

    return (*do_malloc)(size);
}

void jsonp_free(void *ptr) {
    if (!ptr)
        return;

    (*do_free)(ptr);
}

char *jsonp_strdup(const char *str) { return jsonp_strndup(str, strlen(str)); }

char *jsonp_strndup(const char *str, size_t len) {
    char *new_str = (char *) jsonp_malloc(len + 1);
    if (!new_str)
        return NULL;

    memcpy(new_str, str, len);
    new_str[len] = '\0';
    return new_str;
}

void json_set_alloc_funcs(json_malloc_t malloc_fn, json_free_t free_fn) {
    do_malloc = malloc_fn;
    do_free = free_fn;
}

void json_get_alloc_funcs(json_malloc_t *malloc_fn, json_free_t *free_fn) {
    if (malloc_fn)
        *malloc_fn = do_malloc;
    if (free_fn)
        *free_fn = do_free;
}


#pragma endregion

#pragma region strbuffer

// #ifndef _GNU_SOURCE
// #define _GNU_SOURCE
// #endif

// #include "strbuffer.h"
// #include "jansson_private.h"
// #include <stdlib.h>
// #include <string.h>

#define STRBUFFER_MIN_SIZE 16
#define STRBUFFER_FACTOR   2
#define STRBUFFER_SIZE_MAX ((size_t)-1)

int strbuffer_init(strbuffer_t *strbuff) {
    strbuff->size = STRBUFFER_MIN_SIZE;
    strbuff->length = 0;

    strbuff->value = (char *) jsonp_malloc(strbuff->size);
    if (!strbuff->value)
        return -1;

    /* initialize to empty */
    strbuff->value[0] = '\0';
    return 0;
}

void strbuffer_close(strbuffer_t *strbuff) {
    if (strbuff->value)
        jsonp_free(strbuff->value);

    strbuff->size = 0;
    strbuff->length = 0;
    strbuff->value = NULL;
}

void strbuffer_clear(strbuffer_t *strbuff) {
    strbuff->length = 0;
    strbuff->value[0] = '\0';
}

const char *strbuffer_value(const strbuffer_t *strbuff) { return strbuff->value; }

char *strbuffer_steal_value(strbuffer_t *strbuff) {
    char *result = strbuff->value;
    strbuff->value = NULL;
    return result;
}

int strbuffer_append_byte(strbuffer_t *strbuff, char byte) {
    return strbuffer_append_bytes(strbuff, &byte, 1);
}

int strbuffer_append_bytes(strbuffer_t *strbuff, const char *data, size_t size) {
    if (size >= strbuff->size - strbuff->length) {
        size_t new_size;
        char *new_value;

        /* avoid integer overflow */
        if (strbuff->size > STRBUFFER_SIZE_MAX / STRBUFFER_FACTOR ||
            size > STRBUFFER_SIZE_MAX - 1 ||
            strbuff->length > STRBUFFER_SIZE_MAX - 1 - size)
            return -1;

        new_size = max(strbuff->size * STRBUFFER_FACTOR, strbuff->length + size + 1);

        new_value = (char *) jsonp_malloc(new_size);
        if (!new_value)
            return -1;

        memcpy(new_value, strbuff->value, strbuff->length);

        jsonp_free(strbuff->value);
        strbuff->value = new_value;
        strbuff->size = new_size;
    }

    memcpy(strbuff->value + strbuff->length, data, size);
    strbuff->length += size;
    strbuff->value[strbuff->length] = '\0';

    return 0;
}

char strbuffer_pop(strbuffer_t *strbuff) {
    if (strbuff->length > 0) {
        char c = strbuff->value[--strbuff->length];
        strbuff->value[strbuff->length] = '\0';
        return c;
    } else
        return '\0';
}

#pragma endregion

#pragma region strconv

// #include "jansson_private.h"
// #include "strbuffer.h"
// #include <assert.h>
// #include <errno.h>
// #include <math.h>
// #include <stdio.h>
// #include <string.h>

/* need jansson_private_config.h to get the correct snprintf */
// #ifdef HAVE_CONFIG_H
// #include <jansson_private_config.h>
// #endif

#if JSON_HAVE_LOCALECONV
#include <locale.h>

/*
  - This code assumes that the decimal separator is exactly one
    character.

  - If setlocale() is called by another thread between the call to
    localeconv() and the call to sprintf() or strtod(), the result may
    be wrong. setlocale() is not thread-safe and should not be used
    this way. Multi-threaded programs should use uselocale() instead.
*/

static void to_locale(strbuffer_t *strbuffer) {
    const char *point;
    char *pos;

    point = localeconv()->decimal_point;
    if (*point == '.') {
        /* No conversion needed */
        return;
    }

    pos = strchr(strbuffer->value, '.');
    if (pos)
        *pos = *point;
}

static void from_locale(char *buffer) {
    const char *point;
    char *pos;

    point = localeconv()->decimal_point;
    if (*point == '.') {
        /* No conversion needed */
        return;
    }

    pos = strchr(buffer, *point);
    if (pos)
        *pos = '.';
}
#endif

int jsonp_strtod(strbuffer_t *strbuffer, double *out) {
    double value;
    char *end;

#if JSON_HAVE_LOCALECONV
    to_locale(strbuffer);
#endif

    errno = 0;
    value = strtod(strbuffer->value, &end);
    assert(end == strbuffer->value + strbuffer->length);

    if ((value == HUGE_VAL || value == -HUGE_VAL) && errno == ERANGE) {
        /* Overflow */
        return -1;
    }

    *out = value;
    return 0;
}

int jsonp_dtostr(char *buffer, size_t size, double value, int precision) {
    int ret;
    char *start, *end;
    size_t length;

    if (precision == 0)
        precision = 17;

    ret = snprintf(buffer, size, "%.*g", precision, value);
    if (ret < 0)
        return -1;

    length = (size_t)ret;
    if (length >= size)
        return -1;

#if JSON_HAVE_LOCALECONV
    from_locale(buffer);
#endif

    /* Make sure there's a dot or 'e' in the output. Otherwise
       a real is converted to an integer when decoding */
    if (strchr(buffer, '.') == NULL && strchr(buffer, 'e') == NULL) {
        if (length + 3 >= size) {
            /* No space to append ".0" */
            return -1;
        }
        buffer[length] = '.';
        buffer[length + 1] = '0';
        buffer[length + 2] = '\0';
        length += 2;
    }

    /* Remove leading '+' from positive exponent. Also remove leading
       zeros from exponents (added by some printf() implementations) */
    start = strchr(buffer, 'e');
    if (start) {
        start++;
        end = start + 1;

        if (*start == '-')
            start++;

        while (*end == '0')
            end++;

        if (end != start) {
            memmove(start, end, length - (size_t)(end - buffer));
            length -= (size_t)(end - start);
        }
    }

    return (int)length;
}

#pragma endregion

#pragma region utf

int utf8_encode(int32_t codepoint, char *buffer, size_t *size) {
    if (codepoint < 0)
        return -1;
    else if (codepoint < 0x80) {
        buffer[0] = (char)codepoint;
        *size = 1;
    } else if (codepoint < 0x800) {
        buffer[0] = 0xC0 + ((codepoint & 0x7C0) >> 6);
        buffer[1] = 0x80 + ((codepoint & 0x03F));
        *size = 2;
    } else if (codepoint < 0x10000) {
        buffer[0] = 0xE0 + ((codepoint & 0xF000) >> 12);
        buffer[1] = 0x80 + ((codepoint & 0x0FC0) >> 6);
        buffer[2] = 0x80 + ((codepoint & 0x003F));
        *size = 3;
    } else if (codepoint <= 0x10FFFF) {
        buffer[0] = 0xF0 + ((codepoint & 0x1C0000) >> 18);
        buffer[1] = 0x80 + ((codepoint & 0x03F000) >> 12);
        buffer[2] = 0x80 + ((codepoint & 0x000FC0) >> 6);
        buffer[3] = 0x80 + ((codepoint & 0x00003F));
        *size = 4;
    } else
        return -1;

    return 0;
}

size_t utf8_check_first(char byte) {
    unsigned char u = (unsigned char)byte;

    if (u < 0x80)
        return 1;

    if (0x80 <= u && u <= 0xBF) {
        /* second, third or fourth byte of a multi-byte
           sequence, i.e. a "continuation byte" */
        return 0;
    } else if (u == 0xC0 || u == 0xC1) {
        /* overlong encoding of an ASCII byte */
        return 0;
    } else if (0xC2 <= u && u <= 0xDF) {
        /* 2-byte sequence */
        return 2;
    }

    else if (0xE0 <= u && u <= 0xEF) {
        /* 3-byte sequence */
        return 3;
    } else if (0xF0 <= u && u <= 0xF4) {
        /* 4-byte sequence */
        return 4;
    } else { /* u >= 0xF5 */
        /* Restricted (start of 4-, 5- or 6-byte sequence) or invalid
           UTF-8 */
        return 0;
    }
}

size_t utf8_check_full(const char *buffer, size_t size, int32_t *codepoint) {
    size_t i;
    int32_t value = 0;
    unsigned char u = (unsigned char)buffer[0];

    if (size == 2) {
        value = u & 0x1F;
    } else if (size == 3) {
        value = u & 0xF;
    } else if (size == 4) {
        value = u & 0x7;
    } else
        return 0;

    for (i = 1; i < size; i++) {
        u = (unsigned char)buffer[i];

        if (u < 0x80 || u > 0xBF) {
            /* not a continuation byte */
            return 0;
        }

        value = (value << 6) + (u & 0x3F);
    }

    if (value > 0x10FFFF) {
        /* not in Unicode range */
        return 0;
    }

    else if (0xD800 <= value && value <= 0xDFFF) {
        /* invalid code point (UTF-16 surrogate halves) */
        return 0;
    }

    else if ((size == 2 && value < 0x80) || (size == 3 && value < 0x800) ||
             (size == 4 && value < 0x10000)) {
        /* overlong encoding */
        return 0;
    }

    if (codepoint)
        *codepoint = value;

    return 1;
}

const char *utf8_iterate(const char *buffer, size_t bufsize, int32_t *codepoint) {
    size_t count;
    int32_t value;

    if (!bufsize)
        return buffer;

    count = utf8_check_first(buffer[0]);
    if (count <= 0)
        return NULL;

    if (count == 1)
        value = (unsigned char)buffer[0];
    else {
        if (count > bufsize || !utf8_check_full(buffer, count, &value))
            return NULL;
    }

    if (codepoint)
        *codepoint = value;

    return buffer + count;
}

int utf8_check_string(const char *string, size_t length) {
    size_t i;

    for (i = 0; i < length; i++) {
        size_t count = utf8_check_first(string[i]);
        if (count == 0)
            return 0;
        else if (count > 1) {
            if (count > length - i)
                return 0;

            if (!utf8_check_full(&string[i], count, NULL))
                return 0;

            i += count - 1;
        }
    }

    return 1;
}

#pragma endregion

#pragma region dump

// #ifndef _GNU_SOURCE
// #define _GNU_SOURCE
// #endif

// #include "jansson_private.h"

// #include <assert.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #ifdef HAVE_UNISTD_H
// #include <unistd.h>
// #endif

// #include "jansson.h"
// #include "strbuffer.h"
// #include "utf.h"

#define MAX_INTEGER_STR_LENGTH 100
#define MAX_REAL_STR_LENGTH    100

#define FLAGS_TO_INDENT(f)    ((f)&0x1F)
#define FLAGS_TO_PRECISION(f) (((f) >> 11) & 0x1F)

struct buffer {
    const size_t size;
    size_t used;
    char *data;
};

static int dump_to_strbuffer(const char *buffer, size_t size, void *data) {
    return strbuffer_append_bytes((strbuffer_t *)data, buffer, size);
}

static int dump_to_buffer(const char *buffer, size_t size, void *data) {
    struct buffer *buf = (struct buffer *)data;

    if (buf->used + size <= buf->size)
        memcpy(&buf->data[buf->used], buffer, size);

    buf->used += size;
    return 0;
}

static int dump_to_file(const char *buffer, size_t size, void *data) {
    FILE *dest = (FILE *)data;
    if (fwrite(buffer, size, 1, dest) != 1)
        return -1;
    return 0;
}

static int dump_to_fd(const char *buffer, size_t size, void *data) {
#ifdef HAVE_UNISTD_H
    int *dest = (int *)data;
    if (write(*dest, buffer, size) == (ssize_t)size)
        return 0;
#endif
    return -1;
}

/* 32 spaces (the maximum indentation size) */
static const char whitespace[] = "                                ";

static int dump_indent(size_t flags, int depth, int space, json_dump_callback_t dump,
                       void *data) {
    if (FLAGS_TO_INDENT(flags) > 0) {
        unsigned int ws_count = FLAGS_TO_INDENT(flags), n_spaces = depth * ws_count;

        if (dump("\n", 1, data))
            return -1;

        while (n_spaces > 0) {
            int cur_n =
                n_spaces < sizeof whitespace - 1 ? n_spaces : sizeof whitespace - 1;

            if (dump(whitespace, cur_n, data))
                return -1;

            n_spaces -= cur_n;
        }
    } else if (space && !(flags & JSON_COMPACT)) {
        return dump(" ", 1, data);
    }
    return 0;
}

static int dump_string(const char *str, size_t len, json_dump_callback_t dump, void *data,
                       size_t flags) {
    const char *pos, *end, *lim;
    int32_t codepoint = 0;

    if (dump("\"", 1, data))
        return -1;

    end = pos = str;
    lim = str + len;
    while (1) {
        const char *text;
        char seq[13];
        int length;

        while (end < lim) {
            end = utf8_iterate(pos, lim - pos, &codepoint);
            if (!end)
                return -1;

            /* mandatory escape or control char */
            if (codepoint == '\\' || codepoint == '"' || codepoint < 0x20)
                break;

            /* slash */
            if ((flags & JSON_ESCAPE_SLASH) && codepoint == '/')
                break;

            /* non-ASCII */
            if ((flags & JSON_ENSURE_ASCII) && codepoint > 0x7F)
                break;

            pos = end;
        }

        if (pos != str) {
            if (dump(str, pos - str, data))
                return -1;
        }

        if (end == pos)
            break;

        /* handle \, /, ", and control codes */
        length = 2;
        switch (codepoint) {
            case '\\':
                text = "\\\\";
                break;
            case '\"':
                text = "\\\"";
                break;
            case '\b':
                text = "\\b";
                break;
            case '\f':
                text = "\\f";
                break;
            case '\n':
                text = "\\n";
                break;
            case '\r':
                text = "\\r";
                break;
            case '\t':
                text = "\\t";
                break;
            case '/':
                text = "\\/";
                break;
            default: {
                /* codepoint is in BMP */
                if (codepoint < 0x10000) {
                    snprintf(seq, sizeof(seq), "\\u%04X", (unsigned int)codepoint);
                    length = 6;
                }

                /* not in BMP -> construct a UTF-16 surrogate pair */
                else {
                    int32_t first, last;

                    codepoint -= 0x10000;
                    first = 0xD800 | ((codepoint & 0xffc00) >> 10);
                    last = 0xDC00 | (codepoint & 0x003ff);

                    snprintf(seq, sizeof(seq), "\\u%04X\\u%04X", (unsigned int)first,
                             (unsigned int)last);
                    length = 12;
                }

                text = seq;
                break;
            }
        }

        if (dump(text, length, data))
            return -1;

        str = pos = end;
    }

    return dump("\"", 1, data);
}

static int compare_keys(const void *key1, const void *key2) {
    return strcmp(*(const char **)key1, *(const char **)key2);
}

static int do_dump(const json_t *json, size_t flags, int depth, hashtable_t *parents,
                   json_dump_callback_t dump, void *data) {
    int embed = flags & JSON_EMBED;

    flags &= ~JSON_EMBED;

    if (!json)
        return -1;

    switch (json_typeof(json)) {
        case JSON_NULL:
            return dump("null", 4, data);

        case JSON_TRUE:
            return dump("true", 4, data);

        case JSON_FALSE:
            return dump("false", 5, data);

        case JSON_INTEGER: {
            char buffer[MAX_INTEGER_STR_LENGTH];
            int size;

            size = snprintf(buffer, MAX_INTEGER_STR_LENGTH, "%" JSON_INTEGER_FORMAT,
                            json_integer_value(json));
            if (size < 0 || size >= MAX_INTEGER_STR_LENGTH)
                return -1;

            return dump(buffer, size, data);
        }

        case JSON_REAL: {
            char buffer[MAX_REAL_STR_LENGTH];
            int size;
            double value = json_real_value(json);

            size = jsonp_dtostr(buffer, MAX_REAL_STR_LENGTH, value,
                                FLAGS_TO_PRECISION(flags));
            if (size < 0)
                return -1;

            return dump(buffer, size, data);
        }

        case JSON_STRING:
            return dump_string(json_string_value(json), json_string_length(json), dump,
                               data, flags);

        case JSON_ARRAY: {
            size_t n;
            size_t i;
            /* Space for "0x", double the sizeof a pointer for the hex and a
             * terminator. */
            char key[2 + (sizeof(json) * 2) + 1];

            /* detect circular references */
            if (jsonp_loop_check(parents, json, key, sizeof(key)))
                return -1;

            n = json_array_size(json);

            if (!embed && dump("[", 1, data))
                return -1;
            if (n == 0) {
                hashtable_del(parents, key);
                return embed ? 0 : dump("]", 1, data);
            }
            if (dump_indent(flags, depth + 1, 0, dump, data))
                return -1;

            for (i = 0; i < n; ++i) {
                if (do_dump(json_array_get(json, i), flags, depth + 1, parents, dump,
                            data))
                    return -1;

                if (i < n - 1) {
                    if (dump(",", 1, data) ||
                        dump_indent(flags, depth + 1, 1, dump, data))
                        return -1;
                } else {
                    if (dump_indent(flags, depth, 0, dump, data))
                        return -1;
                }
            }

            hashtable_del(parents, key);
            return embed ? 0 : dump("]", 1, data);
        }

        case JSON_OBJECT: {
            void *iter;
            const char *separator;
            int separator_length;
            char loop_key[LOOP_KEY_LEN];

            if (flags & JSON_COMPACT) {
                separator = ":";
                separator_length = 1;
            } else {
                separator = ": ";
                separator_length = 2;
            }

            /* detect circular references */
            if (jsonp_loop_check(parents, json, loop_key, sizeof(loop_key)))
                return -1;

            iter = json_object_iter((json_t *)json);

            if (!embed && dump("{", 1, data))
                return -1;
            if (!iter) {
                hashtable_del(parents, loop_key);
                return embed ? 0 : dump("}", 1, data);
            }
            if (dump_indent(flags, depth + 1, 0, dump, data))
                return -1;

            if (flags & JSON_SORT_KEYS) {
                const char **keys;
                size_t size, i;

                size = json_object_size(json);
                keys = (const char **) jsonp_malloc(size * sizeof(const char *));
                if (!keys)
                    return -1;

                i = 0;
                while (iter) {
                    keys[i] = json_object_iter_key(iter);
                    iter = json_object_iter_next((json_t *)json, iter);
                    i++;
                }
                assert(i == size);

                qsort(keys, size, sizeof(const char *), compare_keys);

                for (i = 0; i < size; i++) {
                    const char *key;
                    json_t *value;

                    key = keys[i];
                    value = json_object_get(json, key);
                    assert(value);

                    dump_string(key, strlen(key), dump, data, flags);
                    if (dump(separator, separator_length, data) ||
                        do_dump(value, flags, depth + 1, parents, dump, data)) {
                        jsonp_free(keys);
                        return -1;
                    }

                    if (i < size - 1) {
                        if (dump(",", 1, data) ||
                            dump_indent(flags, depth + 1, 1, dump, data)) {
                            jsonp_free(keys);
                            return -1;
                        }
                    } else {
                        if (dump_indent(flags, depth, 0, dump, data)) {
                            jsonp_free(keys);
                            return -1;
                        }
                    }
                }

                jsonp_free(keys);
            } else {
                /* Don't sort keys */

                while (iter) {
                    void *next = json_object_iter_next((json_t *)json, iter);
                    const char *key = json_object_iter_key(iter);

                    dump_string(key, strlen(key), dump, data, flags);
                    if (dump(separator, separator_length, data) ||
                        do_dump(json_object_iter_value(iter), flags, depth + 1, parents,
                                dump, data))
                        return -1;

                    if (next) {
                        if (dump(",", 1, data) ||
                            dump_indent(flags, depth + 1, 1, dump, data))
                            return -1;
                    } else {
                        if (dump_indent(flags, depth, 0, dump, data))
                            return -1;
                    }

                    iter = next;
                }
            }

            hashtable_del(parents, loop_key);
            return embed ? 0 : dump("}", 1, data);
        }

        default:
            /* not reached */
            return -1;
    }
}

char *json_dumps(const json_t *json, size_t flags) {
    strbuffer_t strbuff;
    char *result;

    if (strbuffer_init(&strbuff))
        return NULL;

    if (json_dump_callback(json, dump_to_strbuffer, (void *)&strbuff, flags))
        result = NULL;
    else
        result = jsonp_strdup(strbuffer_value(&strbuff));

    strbuffer_close(&strbuff);
    return result;
}

size_t json_dumpb(const json_t *json, char *buffer, size_t size, size_t flags) {
    struct buffer buf = {size, 0, buffer};

    if (json_dump_callback(json, dump_to_buffer, (void *)&buf, flags))
        return 0;

    return buf.used;
}

int json_dumpf(const json_t *json, FILE *output, size_t flags) {
    return json_dump_callback(json, dump_to_file, (void *)output, flags);
}

int json_dumpfd(const json_t *json, int output, size_t flags) {
    return json_dump_callback(json, dump_to_fd, (void *)&output, flags);
}

int json_dump_file(const json_t *json, const char *path, size_t flags) {
    int result;

    FILE *output = fopen(path, "w");
    if (!output)
        return -1;

    result = json_dumpf(json, output, flags);

    if (fclose(output) != 0)
        return -1;

    return result;
}

int json_dump_callback(const json_t *json, json_dump_callback_t callback, void *data,
                       size_t flags) {
    int res;
    hashtable_t parents_set;

    if (!(flags & JSON_ENCODE_ANY)) {
        if (!json_is_array(json) && !json_is_object(json))
            return -1;
    }

    if (hashtable_init(&parents_set))
        return -1;
    res = do_dump(json, flags, 0, &parents_set, callback, data);
    hashtable_close(&parents_set);

    return res;
}

#pragma endregion

#pragma region load

// #include "jansson_private.h"

// #include <assert.h>
// #include <errno.h>
// #include <limits.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #ifdef HAVE_UNISTD_H
// #include <unistd.h>
// #endif

// #include "jansson.h"
// #include "strbuffer.h"
// #include "utf.h"

#define STREAM_STATE_OK    0
#define STREAM_STATE_EOF   -1
#define STREAM_STATE_ERROR -2

#define TOKEN_INVALID -1
#define TOKEN_EOF     0
#define TOKEN_STRING  256
#define TOKEN_INTEGER 257
#define TOKEN_REAL    258
#define TOKEN_TRUE    259
#define TOKEN_FALSE   260
#define TOKEN_NULL    261

/* Locale independent versions of isxxx() functions */
#define l_isupper(c) ('A' <= (c) && (c) <= 'Z')
#define l_islower(c) ('a' <= (c) && (c) <= 'z')
#define l_isalpha(c) (l_isupper(c) || l_islower(c))
#define l_isdigit(c) ('0' <= (c) && (c) <= '9')
#define l_isxdigit(c)                                                                    \
    (l_isdigit(c) || ('A' <= (c) && (c) <= 'F') || ('a' <= (c) && (c) <= 'f'))

/* Read one byte from stream, convert to unsigned char, then int, and
   return. return EOF on end of file. This corresponds to the
   behaviour of fgetc(). */
typedef int (*get_func)(void *data);

typedef struct {
    get_func get;
    void *data;
    char buffer[5];
    size_t buffer_pos;
    int state;
    int line;
    int column, last_column;
    size_t position;
} stream_t;

typedef struct {
    stream_t stream;
    strbuffer_t saved_text;
    size_t flags;
    size_t depth;
    int token;
    union {
        struct {
            char *val;
            size_t len;
        } string;
        json_int_t integer;
        double real;
    } value;
} lex_t;

#define stream_to_lex(stream) container_of(stream, lex_t, stream)

/*** error reporting ***/

static void error_set(json_error_t *error, const lex_t *lex, enum json_error_code code,
                      const char *msg, ...) {
    va_list ap;
    char msg_text[JSON_ERROR_TEXT_LENGTH];
    char msg_with_context[JSON_ERROR_TEXT_LENGTH];

    int line = -1, col = -1;
    size_t pos = 0;
    const char *result = msg_text;

    if (!error)
        return;

    va_start(ap, msg);
    vsnprintf(msg_text, JSON_ERROR_TEXT_LENGTH, msg, ap);
    msg_text[JSON_ERROR_TEXT_LENGTH - 1] = '\0';
    va_end(ap);

    if (lex) {
        const char *saved_text = strbuffer_value(&lex->saved_text);

        line = lex->stream.line;
        col = lex->stream.column;
        pos = lex->stream.position;

        if (saved_text && saved_text[0]) {
            if (lex->saved_text.length <= 20) {
                snprintf(msg_with_context, JSON_ERROR_TEXT_LENGTH_MAX, "%s near '%s'",
                         msg_text, saved_text);
                msg_with_context[JSON_ERROR_TEXT_LENGTH_MAX - 1] = '\0';
                result = msg_with_context;
            }
        } else {
            if (code == json_error_invalid_syntax) {
                /* More specific error code for premature end of file. */
                code = json_error_premature_end_of_input;
            }
            if (lex->stream.state == STREAM_STATE_ERROR) {
                /* No context for UTF-8 decoding errors */
                result = msg_text;
            } else {
                snprintf(msg_with_context, JSON_ERROR_TEXT_LENGTH_MAX, "%s near end of file",
                         msg_text);
                msg_with_context[JSON_ERROR_TEXT_LENGTH_MAX - 1] = '\0';
                result = msg_with_context;
            }
        }
    }

    jsonp_error_set(error, line, col, pos, code, "%s", result);
}

/*** lexical analyzer ***/

static void stream_init(stream_t *stream, get_func get, void *data) {
    stream->get = get;
    stream->data = data;
    stream->buffer[0] = '\0';
    stream->buffer_pos = 0;

    stream->state = STREAM_STATE_OK;
    stream->line = 1;
    stream->column = 0;
    stream->position = 0;
}

static int stream_get(stream_t *stream, json_error_t *error) {
    int c;

    if (stream->state != STREAM_STATE_OK)
        return stream->state;

    if (!stream->buffer[stream->buffer_pos]) {
        c = stream->get(stream->data);
        if (c == EOF) {
            stream->state = STREAM_STATE_EOF;
            return STREAM_STATE_EOF;
        }

        stream->buffer[0] = c;
        stream->buffer_pos = 0;

        if (0x80 <= c && c <= 0xFF) {
            /* multi-byte UTF-8 sequence */
            size_t i, count;

            count = utf8_check_first(c);
            if (!count)
                goto out;

            assert(count >= 2);

            for (i = 1; i < count; i++)
                stream->buffer[i] = stream->get(stream->data);

            if (!utf8_check_full(stream->buffer, count, NULL))
                goto out;

            stream->buffer[count] = '\0';
        } else
            stream->buffer[1] = '\0';
    }

    c = stream->buffer[stream->buffer_pos++];

    stream->position++;
    if (c == '\n') {
        stream->line++;
        stream->last_column = stream->column;
        stream->column = 0;
    } else if (utf8_check_first(c)) {
        /* track the Unicode character column, so increment only if
           this is the first character of a UTF-8 sequence */
        stream->column++;
    }

    return c;

out:
    stream->state = STREAM_STATE_ERROR;
    // error_set(error, stream_to_lex(stream), json_error_invalid_utf8,
    //           "unable to decode byte 0x%x", c);
    return STREAM_STATE_ERROR;
}

static void stream_unget(stream_t *stream, int c) {
    if (c == STREAM_STATE_EOF || c == STREAM_STATE_ERROR)
        return;

    stream->position--;
    if (c == '\n') {
        stream->line--;
        stream->column = stream->last_column;
    } else if (utf8_check_first(c))
        stream->column--;

    assert(stream->buffer_pos > 0);
    stream->buffer_pos--;
    assert(stream->buffer[stream->buffer_pos] == c);
}

static int lex_get(lex_t *lex, json_error_t *error) {
    return stream_get(&lex->stream, error);
}

static void lex_save(lex_t *lex, int c) { strbuffer_append_byte(&lex->saved_text, c); }

static int lex_get_save(lex_t *lex, json_error_t *error) {
    int c = stream_get(&lex->stream, error);
    if (c != STREAM_STATE_EOF && c != STREAM_STATE_ERROR)
        lex_save(lex, c);
    return c;
}

static void lex_unget(lex_t *lex, int c) { stream_unget(&lex->stream, c); }

static void lex_unget_unsave(lex_t *lex, int c) {
    if (c != STREAM_STATE_EOF && c != STREAM_STATE_ERROR) {
/* Since we treat warnings as errors, when assertions are turned
 * off the "d" variable would be set but never used. Which is
 * treated as an error by GCC.
 */
#ifndef NDEBUG
        char d;
#endif
        stream_unget(&lex->stream, c);
#ifndef NDEBUG
        d =
#endif
            strbuffer_pop(&lex->saved_text);
        assert(c == d);
    }
}

static void lex_save_cached(lex_t *lex) {
    while (lex->stream.buffer[lex->stream.buffer_pos] != '\0') {
        lex_save(lex, lex->stream.buffer[lex->stream.buffer_pos]);
        lex->stream.buffer_pos++;
        lex->stream.position++;
    }
}

static void lex_free_string(lex_t *lex) {
    jsonp_free(lex->value.string.val);
    lex->value.string.val = NULL;
    lex->value.string.len = 0;
}

/* assumes that str points to 'u' plus at least 4 valid hex digits */
static int32_t decode_unicode_escape(const char *str) {
    int i;
    int32_t value = 0;

    assert(str[0] == 'u');

    for (i = 1; i <= 4; i++) {
        char c = str[i];
        value <<= 4;
        if (l_isdigit(c))
            value += c - '0';
        else if (l_islower(c))
            value += c - 'a' + 10;
        else if (l_isupper(c))
            value += c - 'A' + 10;
        else
            return -1;
    }

    return value;
}

static void lex_scan_string(lex_t *lex, json_error_t *error) {
    int c;
    const char *p;
    char *t;
    int i;

    lex->value.string.val = NULL;
    lex->token = TOKEN_INVALID;

    c = lex_get_save(lex, error);

    while (c != '"') {
        if (c == STREAM_STATE_ERROR)
            goto out;

        else if (c == STREAM_STATE_EOF) {
            error_set(error, lex, json_error_premature_end_of_input,
                      "premature end of input");
            goto out;
        }

        else if (0 <= c && c <= 0x1F) {
            /* control character */
            lex_unget_unsave(lex, c);
            if (c == '\n')
                error_set(error, lex, json_error_invalid_syntax, "unexpected newline");
            else
                error_set(error, lex, json_error_invalid_syntax, "control character 0x%x",
                          c);
            goto out;
        }

        else if (c == '\\') {
            c = lex_get_save(lex, error);
            if (c == 'u') {
                c = lex_get_save(lex, error);
                for (i = 0; i < 4; i++) {
                    if (!l_isxdigit(c)) {
                        error_set(error, lex, json_error_invalid_syntax,
                                  "invalid escape");
                        goto out;
                    }
                    c = lex_get_save(lex, error);
                }
            } else if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' ||
                       c == 'n' || c == 'r' || c == 't')
                c = lex_get_save(lex, error);
            else {
                error_set(error, lex, json_error_invalid_syntax, "invalid escape");
                goto out;
            }
        } else
            c = lex_get_save(lex, error);
    }

    /* the actual value is at most of the same length as the source
       string, because:
         - shortcut escapes (e.g. "\t") (length 2) are converted to 1 byte
         - a single \uXXXX escape (length 6) is converted to at most 3 bytes
         - two \uXXXX escapes (length 12) forming an UTF-16 surrogate pair
           are converted to 4 bytes
    */
    t = (char *) jsonp_malloc(lex->saved_text.length + 1);
    if (!t) {
        /* this is not very nice, since TOKEN_INVALID is returned */
        goto out;
    }
    lex->value.string.val = t;

    /* + 1 to skip the " */
    p = strbuffer_value(&lex->saved_text) + 1;

    while (*p != '"') {
        if (*p == '\\') {
            p++;
            if (*p == 'u') {
                size_t length;
                int32_t value;

                value = decode_unicode_escape(p);
                if (value < 0) {
                    error_set(error, lex, json_error_invalid_syntax,
                              "invalid Unicode escape '%.6s'", p - 1);
                    goto out;
                }
                p += 5;

                if (0xD800 <= value && value <= 0xDBFF) {
                    /* surrogate pair */
                    if (*p == '\\' && *(p + 1) == 'u') {
                        int32_t value2 = decode_unicode_escape(++p);
                        if (value2 < 0) {
                            error_set(error, lex, json_error_invalid_syntax,
                                      "invalid Unicode escape '%.6s'", p - 1);
                            goto out;
                        }
                        p += 5;

                        if (0xDC00 <= value2 && value2 <= 0xDFFF) {
                            /* valid second surrogate */
                            value =
                                ((value - 0xD800) << 10) + (value2 - 0xDC00) + 0x10000;
                        } else {
                            /* invalid second surrogate */
                            error_set(error, lex, json_error_invalid_syntax,
                                      "invalid Unicode '\\u%04X\\u%04X'", value, value2);
                            goto out;
                        }
                    } else {
                        /* no second surrogate */
                        error_set(error, lex, json_error_invalid_syntax,
                                  "invalid Unicode '\\u%04X'", value);
                        goto out;
                    }
                } else if (0xDC00 <= value && value <= 0xDFFF) {
                    error_set(error, lex, json_error_invalid_syntax,
                              "invalid Unicode '\\u%04X'", value);
                    goto out;
                }

                if (utf8_encode(value, t, &length))
                    assert(0);
                t += length;
            } else {
                switch (*p) {
                    case '"':
                    case '\\':
                    case '/':
                        *t = *p;
                        break;
                    case 'b':
                        *t = '\b';
                        break;
                    case 'f':
                        *t = '\f';
                        break;
                    case 'n':
                        *t = '\n';
                        break;
                    case 'r':
                        *t = '\r';
                        break;
                    case 't':
                        *t = '\t';
                        break;
                    default:
                        assert(0);
                }
                t++;
                p++;
            }
        } else
            *(t++) = *(p++);
    }
    *t = '\0';
    lex->value.string.len = t - lex->value.string.val;
    lex->token = TOKEN_STRING;
    return;

out:
    lex_free_string(lex);
}

#ifndef JANSSON_USING_CMAKE /* disabled if using cmake */
#if JSON_INTEGER_IS_LONG_LONG
#ifdef _MSC_VER /* Microsoft Visual Studio */
#define json_strtoint _strtoi64
#else
#define json_strtoint strtoll
#endif
#else
#define json_strtoint strtol
#endif
#endif

static int lex_scan_number(lex_t *lex, int c, json_error_t *error) {
    const char *saved_text;
    char *end;
    double doubleval;

    lex->token = TOKEN_INVALID;

    if (c == '-')
        c = lex_get_save(lex, error);

    if (c == '0') {
        c = lex_get_save(lex, error);
        if (l_isdigit(c)) {
            lex_unget_unsave(lex, c);
            goto out;
        }
    } else if (l_isdigit(c)) {
        do
            c = lex_get_save(lex, error);
        while (l_isdigit(c));
    } else {
        lex_unget_unsave(lex, c);
        goto out;
    }

    if (!(lex->flags & JSON_DECODE_INT_AS_REAL) && c != '.' && c != 'E' && c != 'e') {
        json_int_t intval;

        lex_unget_unsave(lex, c);

        saved_text = strbuffer_value(&lex->saved_text);

        errno = 0;
        intval = json_strtoint(saved_text, &end, 10);
        if (errno == ERANGE) {
            if (intval < 0)
                error_set(error, lex, json_error_numeric_overflow,
                          "too big negative integer");
            else
                error_set(error, lex, json_error_numeric_overflow, "too big integer");
            goto out;
        }

        assert(end == saved_text + lex->saved_text.length);

        lex->token = TOKEN_INTEGER;
        lex->value.integer = intval;
        return 0;
    }

    if (c == '.') {
        c = lex_get(lex, error);
        if (!l_isdigit(c)) {
            lex_unget(lex, c);
            goto out;
        }
        lex_save(lex, c);

        do
            c = lex_get_save(lex, error);
        while (l_isdigit(c));
    }

    if (c == 'E' || c == 'e') {
        c = lex_get_save(lex, error);
        if (c == '+' || c == '-')
            c = lex_get_save(lex, error);

        if (!l_isdigit(c)) {
            lex_unget_unsave(lex, c);
            goto out;
        }

        do
            c = lex_get_save(lex, error);
        while (l_isdigit(c));
    }

    lex_unget_unsave(lex, c);

    if (jsonp_strtod(&lex->saved_text, &doubleval)) {
        error_set(error, lex, json_error_numeric_overflow, "real number overflow");
        goto out;
    }

    lex->token = TOKEN_REAL;
    lex->value.real = doubleval;
    return 0;

out:
    return -1;
}

static int lex_scan(lex_t *lex, json_error_t *error) {
    int c;

    strbuffer_clear(&lex->saved_text);

    if (lex->token == TOKEN_STRING)
        lex_free_string(lex);

    do
        c = lex_get(lex, error);
    while (c == ' ' || c == '\t' || c == '\n' || c == '\r');

    if (c == STREAM_STATE_EOF) {
        lex->token = TOKEN_EOF;
        goto out;
    }

    if (c == STREAM_STATE_ERROR) {
        lex->token = TOKEN_INVALID;
        goto out;
    }

    lex_save(lex, c);

    if (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',')
        lex->token = c;

    else if (c == '"')
        lex_scan_string(lex, error);

    else if (l_isdigit(c) || c == '-') {
        if (lex_scan_number(lex, c, error))
            goto out;
    }

    else if (l_isalpha(c)) {
        /* eat up the whole identifier for clearer error messages */
        const char *saved_text;

        do
            c = lex_get_save(lex, error);
        while (l_isalpha(c));
        lex_unget_unsave(lex, c);

        saved_text = strbuffer_value(&lex->saved_text);

        if (strcmp(saved_text, "true") == 0)
            lex->token = TOKEN_TRUE;
        else if (strcmp(saved_text, "false") == 0)
            lex->token = TOKEN_FALSE;
        else if (strcmp(saved_text, "null") == 0)
            lex->token = TOKEN_NULL;
        else
            lex->token = TOKEN_INVALID;
    }

    else {
        /* save the rest of the input UTF-8 sequence to get an error
           message of valid UTF-8 */
        lex_save_cached(lex);
        lex->token = TOKEN_INVALID;
    }

out:
    return lex->token;
}

static char *lex_steal_string(lex_t *lex, size_t *out_len) {
    char *result = NULL;
    if (lex->token == TOKEN_STRING) {
        result = lex->value.string.val;
        *out_len = lex->value.string.len;
        lex->value.string.val = NULL;
        lex->value.string.len = 0;
    }
    return result;
}

static int lex_init(lex_t *lex, get_func get, size_t flags, void *data) {
    stream_init(&lex->stream, get, data);
    if (strbuffer_init(&lex->saved_text))
        return -1;

    lex->flags = flags;
    lex->token = TOKEN_INVALID;
    return 0;
}

static void lex_close(lex_t *lex) {
    if (lex->token == TOKEN_STRING)
        lex_free_string(lex);
    strbuffer_close(&lex->saved_text);
}

/*** parser ***/

static json_t *parse_value(lex_t *lex, size_t flags, json_error_t *error);

static json_t *parse_object(lex_t *lex, size_t flags, json_error_t *error) {
    json_t *object = json_object();
    if (!object)
        return NULL;

    lex_scan(lex, error);
    if (lex->token == '}')
        return object;

    while (1) {
        char *key;
        size_t len;
        json_t *value;

        if (lex->token != TOKEN_STRING) {
            error_set(error, lex, json_error_invalid_syntax, "string or '}' expected");
            goto error;
        }

        key = lex_steal_string(lex, &len);
        if (!key)
            return NULL;
        if (memchr(key, '\0', len)) {
            jsonp_free(key);
            error_set(error, lex, json_error_null_byte_in_key,
                      "NUL byte in object key not supported");
            goto error;
        }

        if (flags & JSON_REJECT_DUPLICATES) {
            if (json_object_get(object, key)) {
                jsonp_free(key);
                error_set(error, lex, json_error_duplicate_key, "duplicate object key");
                goto error;
            }
        }

        lex_scan(lex, error);
        if (lex->token != ':') {
            jsonp_free(key);
            error_set(error, lex, json_error_invalid_syntax, "':' expected");
            goto error;
        }

        lex_scan(lex, error);
        value = parse_value(lex, flags, error);
        if (!value) {
            jsonp_free(key);
            goto error;
        }

        if (json_object_set_new_nocheck(object, key, value)) {
            jsonp_free(key);
            goto error;
        }

        jsonp_free(key);

        lex_scan(lex, error);
        if (lex->token != ',')
            break;

        lex_scan(lex, error);
    }

    if (lex->token != '}') {
        error_set(error, lex, json_error_invalid_syntax, "'}' expected");
        goto error;
    }

    return object;

error:
    json_decref(object);
    return NULL;
}

static json_t *parse_array(lex_t *lex, size_t flags, json_error_t *error) {
    json_t *array = json_array();
    if (!array)
        return NULL;

    lex_scan(lex, error);
    if (lex->token == ']')
        return array;

    while (lex->token) {
        json_t *elem = parse_value(lex, flags, error);
        if (!elem)
            goto error;

        if (json_array_append_new(array, elem)) {
            goto error;
        }

        lex_scan(lex, error);
        if (lex->token != ',')
            break;

        lex_scan(lex, error);
    }

    if (lex->token != ']') {
        error_set(error, lex, json_error_invalid_syntax, "']' expected");
        goto error;
    }

    return array;

error:
    json_decref(array);
    return NULL;
}

static json_t *parse_value(lex_t *lex, size_t flags, json_error_t *error) {
    json_t *json;

    lex->depth++;
    if (lex->depth > JSON_PARSER_MAX_DEPTH) {
        error_set(error, lex, json_error_stack_overflow, "maximum parsing depth reached");
        return NULL;
    }

    switch (lex->token) {
        case TOKEN_STRING: {
            const char *value = lex->value.string.val;
            size_t len = lex->value.string.len;

            if (!(flags & JSON_ALLOW_NUL)) {
                if (memchr(value, '\0', len)) {
                    error_set(error, lex, json_error_null_character,
                              "\\u0000 is not allowed without JSON_ALLOW_NUL");
                    return NULL;
                }
            }

            json = jsonp_stringn_nocheck_own(value, len);
            lex->value.string.val = NULL;
            lex->value.string.len = 0;
            break;
        }

        case TOKEN_INTEGER: {
            json = json_integer(lex->value.integer);
            break;
        }

        case TOKEN_REAL: {
            json = json_real(lex->value.real);
            break;
        }

        case TOKEN_TRUE:
            json = json_true();
            break;

        case TOKEN_FALSE:
            json = json_false();
            break;

        case TOKEN_NULL:
            json = json_null();
            break;

        case '{':
            json = parse_object(lex, flags, error);
            break;

        case '[':
            json = parse_array(lex, flags, error);
            break;

        case TOKEN_INVALID:
            error_set(error, lex, json_error_invalid_syntax, "invalid token");
            return NULL;

        default:
            error_set(error, lex, json_error_invalid_syntax, "unexpected token");
            return NULL;
    }

    if (!json)
        return NULL;

    lex->depth--;
    return json;
}

static json_t *parse_json(lex_t *lex, size_t flags, json_error_t *error) {
    json_t *result;

    lex->depth = 0;

    lex_scan(lex, error);
    if (!(flags & JSON_DECODE_ANY)) {
        if (lex->token != '[' && lex->token != '{') {
            error_set(error, lex, json_error_invalid_syntax, "'[' or '{' expected");
            return NULL;
        }
    }

    result = parse_value(lex, flags, error);
    if (!result)
        return NULL;

    if (!(flags & JSON_DISABLE_EOF_CHECK)) {
        lex_scan(lex, error);
        if (lex->token != TOKEN_EOF) {
            error_set(error, lex, json_error_end_of_input_expected,
                      "end of file expected");
            json_decref(result);
            return NULL;
        }
    }

    if (error) {
        /* Save the position even though there was no error */
        error->position = (int)lex->stream.position;
    }

    return result;
}

typedef struct {
    const char *data;
    size_t pos;
} string_data_t;

static int string_get(void *data) {
    char c;
    string_data_t *stream = (string_data_t *)data;
    c = stream->data[stream->pos];
    if (c == '\0')
        return EOF;
    else {
        stream->pos++;
        return (unsigned char)c;
    }
}

json_t *json_loads(const char *string, size_t flags, json_error_t *error) {
    lex_t lex;
    json_t *result;
    string_data_t stream_data;

    jsonp_error_init(error, "<string>");

    if (string == NULL) {
        error_set(error, NULL, json_error_invalid_argument, "wrong arguments");
        return NULL;
    }

    stream_data.data = string;
    stream_data.pos = 0;

    if (lex_init(&lex, string_get, flags, (void *)&stream_data))
        return NULL;

    result = parse_json(&lex, flags, error);

    lex_close(&lex);
    return result;
}

typedef struct {
    const char *data;
    size_t len;
    size_t pos;
} buffer_data_t;

static int buffer_get(void *data) {
    char c;
    buffer_data_t *stream = (buffer_data_t *) data;
    if (stream->pos >= stream->len)
        return EOF;

    c = stream->data[stream->pos];
    stream->pos++;
    return (unsigned char)c;
}

json_t *json_loadb(const char *buffer, size_t buflen, size_t flags, json_error_t *error) {
    lex_t lex;
    json_t *result;
    buffer_data_t stream_data;

    jsonp_error_init(error, "<buffer>");

    if (buffer == NULL) {
        error_set(error, NULL, json_error_invalid_argument, "wrong arguments");
        return NULL;
    }

    stream_data.data = buffer;
    stream_data.pos = 0;
    stream_data.len = buflen;

    if (lex_init(&lex, buffer_get, flags, (void *)&stream_data))
        return NULL;

    result = parse_json(&lex, flags, error);

    lex_close(&lex);
    return result;
}

json_t *json_loadf(FILE *input, size_t flags, json_error_t *error) {
    lex_t lex;
    const char *source;
    json_t *result;

    if (input == stdin)
        source = "<stdin>";
    else
        source = "<stream>";

    jsonp_error_init(error, source);

    if (input == NULL) {
        error_set(error, NULL, json_error_invalid_argument, "wrong arguments");
        return NULL;
    }

    if (lex_init(&lex, (get_func)fgetc, flags, input))
        return NULL;

    result = parse_json(&lex, flags, error);

    lex_close(&lex);
    return result;
}

static int fd_get_func(int *fd) {
#ifdef HAVE_UNISTD_H
    uint8_t c;
    if (read(*fd, &c, 1) == 1)
        return c;
#endif
    return EOF;
}

json_t *json_loadfd(int input, size_t flags, json_error_t *error) {
    lex_t lex;
    const char *source;
    json_t *result;

#ifdef HAVE_UNISTD_H
    if (input == STDIN_FILENO)
        source = "<stdin>";
    else
#endif
        source = "<stream>";

    jsonp_error_init(error, source);

    if (input < 0) {
        error_set(error, NULL, json_error_invalid_argument, "wrong arguments");
        return NULL;
    }

    if (lex_init(&lex, (get_func)fd_get_func, flags, &input))
        return NULL;

    result = parse_json(&lex, flags, error);

    lex_close(&lex);
    return result;
}

json_t *json_load_file(const char *path, size_t flags, json_error_t *error) {
    json_t *result;
    FILE *fp;

    jsonp_error_init(error, path);

    if (path == NULL) {
        error_set(error, NULL, json_error_invalid_argument, "wrong arguments");
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        error_set(error, NULL, json_error_cannot_open_file, "unable to open %s: %s", path,
                  strerror(errno));
        return NULL;
    }

    result = json_loadf(fp, flags, error);

    fclose(fp);
    return result;
}

#define MAX_BUF_LEN 1024

typedef struct {
    char data[MAX_BUF_LEN];
    size_t len;
    size_t pos;
    json_load_callback_t callback;
    void *arg;
} callback_data_t;

static int callback_get(void *data) {
    char c;
    callback_data_t *stream = (callback_data_t *) data;

    if (stream->pos >= stream->len) {
        stream->pos = 0;
        stream->len = stream->callback(stream->data, MAX_BUF_LEN, stream->arg);
        if (stream->len == 0 || stream->len == (size_t)-1)
            return EOF;
    }

    c = stream->data[stream->pos];
    stream->pos++;
    return (unsigned char)c;
}

json_t *json_load_callback(json_load_callback_t callback, void *arg, size_t flags,
                           json_error_t *error) {
    lex_t lex;
    json_t *result;

    callback_data_t stream_data;

    memset(&stream_data, 0, sizeof(stream_data));
    stream_data.callback = callback;
    stream_data.arg = arg;

    jsonp_error_init(error, "<callback>");

    if (callback == NULL) {
        error_set(error, NULL, json_error_invalid_argument, "wrong arguments");
        return NULL;
    }

    if (lex_init(&lex, (get_func)callback_get, flags, &stream_data))
        return NULL;

    result = parse_json(&lex, flags, error);

    lex_close(&lex);
    return result;
}

#pragma endregion

#pragma region pack unpack

// #include "jansson.h"
// #include "jansson_private.h"
// #include "utf.h"
// #include <string.h>

typedef struct token_t {
    int line;
    int column;
    size_t pos;
    char token;
} token_t;

typedef struct scanner_t {
    const char *start;
    const char *fmt;
    token_t prev_token;
    token_t token;
    token_t next_token;
    json_error_t *error;
    size_t flags;
    int line;
    int column;
    size_t pos;
    int has_error;
} scanner_t;

#define token(scanner) ((scanner)->token.token)

static const char *const type_names[] = {"object", "array", "string", "integer",
                                         "real",   "true",  "false",  "null"};

#define type_name(x) type_names[json_typeof(x)]

static const char unpack_value_starters[] = "{[siIbfFOon";

static void scanner_init(scanner_t *s, json_error_t *error, size_t flags,
                         const char *fmt) {
    s->error = error;
    s->flags = flags;
    s->fmt = s->start = fmt;
    memset(&s->prev_token, 0, sizeof(token_t));
    memset(&s->token, 0, sizeof(token_t));
    memset(&s->next_token, 0, sizeof(token_t));
    s->line = 1;
    s->column = 0;
    s->pos = 0;
    s->has_error = 0;
}

static void next_token(scanner_t *s) {
    const char *t;
    s->prev_token = s->token;

    if (s->next_token.line) {
        s->token = s->next_token;
        s->next_token.line = 0;
        return;
    }

    if (!token(s) && !*s->fmt)
        return;

    t = s->fmt;
    s->column++;
    s->pos++;

    /* skip space and ignored chars */
    while (*t == ' ' || *t == '\t' || *t == '\n' || *t == ',' || *t == ':') {
        if (*t == '\n') {
            s->line++;
            s->column = 1;
        } else
            s->column++;

        s->pos++;
        t++;
    }

    s->token.token = *t;
    s->token.line = s->line;
    s->token.column = s->column;
    s->token.pos = s->pos;

    if (*t)
        t++;
    s->fmt = t;
}

static void prev_token(scanner_t *s) {
    s->next_token = s->token;
    s->token = s->prev_token;
}

static void set_error(scanner_t *s, const char *source, enum json_error_code code,
                      const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    jsonp_error_vset(s->error, s->token.line, s->token.column, s->token.pos, code, fmt,
                     ap);

    jsonp_error_set_source(s->error, source);

    va_end(ap);
}

static json_t *pack(scanner_t *s, va_list *ap);

/* ours will be set to 1 if jsonp_free() must be called for the result
   afterwards */
static char *read_string(scanner_t *s, va_list *ap, const char *purpose, size_t *out_len,
                         int *ours, int optional) {
    char t;
    strbuffer_t strbuff;
    const char *str;
    size_t length;

    next_token(s);
    t = token(s);
    prev_token(s);

    *ours = 0;
    if (t != '#' && t != '%' && t != '+') {
        /* Optimize the simple case */
        str = va_arg(*ap, const char *);

        if (!str) {
            if (!optional) {
                set_error(s, "<args>", json_error_null_value, "NULL %s", purpose);
                s->has_error = 1;
            }
            return NULL;
        }

        length = strlen(str);

        if (!utf8_check_string(str, length)) {
            set_error(s, "<args>", json_error_invalid_utf8, "Invalid UTF-8 %s", purpose);
            s->has_error = 1;
            return NULL;
        }

        *out_len = length;
        return (char *)str;
    } else if (optional) {
        set_error(s, "<format>", json_error_invalid_format,
                  "Cannot use '%c' on optional strings", t);
        s->has_error = 1;

        return NULL;
    }

    if (strbuffer_init(&strbuff)) {
        set_error(s, "<internal>", json_error_out_of_memory, "Out of memory");
        s->has_error = 1;
    }

    while (1) {
        str = va_arg(*ap, const char *);
        if (!str) {
            set_error(s, "<args>", json_error_null_value, "NULL %s", purpose);
            s->has_error = 1;
        }

        next_token(s);

        if (token(s) == '#') {
            length = va_arg(*ap, int);
        } else if (token(s) == '%') {
            length = va_arg(*ap, size_t);
        } else {
            prev_token(s);
            length = s->has_error ? 0 : strlen(str);
        }

        if (!s->has_error && strbuffer_append_bytes(&strbuff, str, length) == -1) {
            set_error(s, "<internal>", json_error_out_of_memory, "Out of memory");
            s->has_error = 1;
        }

        next_token(s);
        if (token(s) != '+') {
            prev_token(s);
            break;
        }
    }

    if (s->has_error) {
        strbuffer_close(&strbuff);
        return NULL;
    }

    if (!utf8_check_string(strbuff.value, strbuff.length)) {
        set_error(s, "<args>", json_error_invalid_utf8, "Invalid UTF-8 %s", purpose);
        strbuffer_close(&strbuff);
        s->has_error = 1;
        return NULL;
    }

    *out_len = strbuff.length;
    *ours = 1;
    return strbuffer_steal_value(&strbuff);
}

static json_t *pack_object(scanner_t *s, va_list *ap) {
    json_t *object = json_object();
    next_token(s);

    while (token(s) != '}') {
        char *key;
        size_t len;
        int ours;
        json_t *value;
        char valueOptional;

        if (!token(s)) {
            set_error(s, "<format>", json_error_invalid_format,
                      "Unexpected end of format string");
            goto error;
        }

        if (token(s) != 's') {
            set_error(s, "<format>", json_error_invalid_format,
                      "Expected format 's', got '%c'", token(s));
            goto error;
        }

        key = read_string(s, ap, "object key", &len, &ours, 0);

        next_token(s);

        next_token(s);
        valueOptional = token(s);
        prev_token(s);

        value = pack(s, ap);
        if (!value) {
            if (ours)
                jsonp_free(key);

            if (valueOptional != '*') {
                set_error(s, "<args>", json_error_null_value, "NULL object value");
                s->has_error = 1;
            }

            next_token(s);
            continue;
        }

        if (s->has_error)
            json_decref(value);

        if (!s->has_error && json_object_set_new_nocheck(object, key, value)) {
            set_error(s, "<internal>", json_error_out_of_memory,
                      "Unable to add key \"%s\"", key);
            s->has_error = 1;
        }

        if (ours)
            jsonp_free(key);

        next_token(s);
    }

    if (!s->has_error)
        return object;

error:
    json_decref(object);
    return NULL;
}

static json_t *pack_array(scanner_t *s, va_list *ap) {
    json_t *array = json_array();
    next_token(s);

    while (token(s) != ']') {
        json_t *value;
        char valueOptional;

        if (!token(s)) {
            set_error(s, "<format>", json_error_invalid_format,
                      "Unexpected end of format string");
            /* Format string errors are unrecoverable. */
            goto error;
        }

        next_token(s);
        valueOptional = token(s);
        prev_token(s);

        value = pack(s, ap);
        if (!value) {
            if (valueOptional != '*') {
                s->has_error = 1;
            }

            next_token(s);
            continue;
        }

        if (s->has_error)
            json_decref(value);

        if (!s->has_error && json_array_append_new(array, value)) {
            set_error(s, "<internal>", json_error_out_of_memory,
                      "Unable to append to array");
            s->has_error = 1;
        }

        next_token(s);
    }

    if (!s->has_error)
        return array;

error:
    json_decref(array);
    return NULL;
}

static json_t *pack_string(scanner_t *s, va_list *ap) {
    char *str;
    char t;
    size_t len;
    int ours;
    int optional;

    next_token(s);
    t = token(s);
    optional = t == '?' || t == '*';
    if (!optional)
        prev_token(s);

    str = read_string(s, ap, "string", &len, &ours, optional);

    if (!str)
        return t == '?' && !s->has_error ? json_null() : NULL;

    if (s->has_error) {
        /* It's impossible to reach this point if ours != 0, do not free str. */
        return NULL;
    }

    if (ours)
        return jsonp_stringn_nocheck_own(str, len);

    return json_stringn_nocheck(str, len);
}

static json_t *pack_object_inter(scanner_t *s, va_list *ap, int need_incref) {
    json_t *json;
    char ntoken;

    next_token(s);
    ntoken = token(s);

    if (ntoken != '?' && ntoken != '*')
        prev_token(s);

    json = va_arg(*ap, json_t *);

    if (json)
        return need_incref ? json_incref(json) : json;

    switch (ntoken) {
        case '?':
            return json_null();
        case '*':
            return NULL;
        default:
            break;
    }

    set_error(s, "<args>", json_error_null_value, "NULL object");
    s->has_error = 1;
    return NULL;
}

static json_t *pack_integer(scanner_t *s, json_int_t value) {
    json_t *json = json_integer(value);

    if (!json) {
        set_error(s, "<internal>", json_error_out_of_memory, "Out of memory");
        s->has_error = 1;
    }

    return json;
}

static json_t *pack_real(scanner_t *s, double value) {
    /* Allocate without setting value so we can identify OOM error. */
    json_t *json = json_real(0.0);

    if (!json) {
        set_error(s, "<internal>", json_error_out_of_memory, "Out of memory");
        s->has_error = 1;

        return NULL;
    }

    if (json_real_set(json, value)) {
        json_decref(json);

        set_error(s, "<args>", json_error_numeric_overflow,
                  "Invalid floating point value");
        s->has_error = 1;

        return NULL;
    }

    return json;
}

static json_t *pack(scanner_t *s, va_list *ap) {
    switch (token(s)) {
        case '{':
            return pack_object(s, ap);

        case '[':
            return pack_array(s, ap);

        case 's': /* string */
            return pack_string(s, ap);

        case 'n': /* null */
            return json_null();

        case 'b': /* boolean */
            return va_arg(*ap, int) ? json_true() : json_false();

        case 'i': /* integer from int */
            return pack_integer(s, va_arg(*ap, int));

        case 'I': /* integer from json_int_t */
            return pack_integer(s, va_arg(*ap, json_int_t));

        case 'f': /* real */
            return pack_real(s, va_arg(*ap, double));

        case 'O': /* a json_t object; increments refcount */
            return pack_object_inter(s, ap, 1);

        case 'o': /* a json_t object; doesn't increment refcount */
            return pack_object_inter(s, ap, 0);

        default:
            set_error(s, "<format>", json_error_invalid_format,
                      "Unexpected format character '%c'", token(s));
            s->has_error = 1;
            return NULL;
    }
}

static int unpack(scanner_t *s, json_t *root, va_list *ap);

static int unpack_object(scanner_t *s, json_t *root, va_list *ap) {
    int ret = -1;
    int strict = 0;
    int gotopt = 0;

    /* Use a set (emulated by a hashtable) to check that all object
       keys are accessed. Checking that the correct number of keys
       were accessed is not enough, as the same key can be unpacked
       multiple times.
    */
    hashtable_t key_set;

    if (hashtable_init(&key_set)) {
        set_error(s, "<internal>", json_error_out_of_memory, "Out of memory");
        return -1;
    }

    if (root && !json_is_object(root)) {
        set_error(s, "<validation>", json_error_wrong_type, "Expected object, got %s",
                  type_name(root));
        goto out;
    }
    next_token(s);

    while (token(s) != '}') {
        const char *key;
        json_t *value;
        int opt = 0;

        if (strict != 0) {
            set_error(s, "<format>", json_error_invalid_format,
                      "Expected '}' after '%c', got '%c'", (strict == 1 ? '!' : '*'),
                      token(s));
            goto out;
        }

        if (!token(s)) {
            set_error(s, "<format>", json_error_invalid_format,
                      "Unexpected end of format string");
            goto out;
        }

        if (token(s) == '!' || token(s) == '*') {
            strict = (token(s) == '!' ? 1 : -1);
            next_token(s);
            continue;
        }

        if (token(s) != 's') {
            set_error(s, "<format>", json_error_invalid_format,
                      "Expected format 's', got '%c'", token(s));
            goto out;
        }

        key = va_arg(*ap, const char *);
        if (!key) {
            set_error(s, "<args>", json_error_null_value, "NULL object key");
            goto out;
        }

        next_token(s);

        if (token(s) == '?') {
            opt = gotopt = 1;
            next_token(s);
        }

        if (!root) {
            /* skipping */
            value = NULL;
        } else {
            value = json_object_get(root, key);
            if (!value && !opt) {
                set_error(s, "<validation>", json_error_item_not_found,
                          "Object item not found: %s", key);
                goto out;
            }
        }

        if (unpack(s, value, ap))
            goto out;

        hashtable_set(&key_set, key, json_null());
        next_token(s);
    }

    if (strict == 0 && (s->flags & JSON_STRICT))
        strict = 1;

    if (root && strict == 1) {
        /* We need to check that all non optional items have been parsed */
        const char *key;
        /* keys_res is 1 for uninitialized, 0 for success, -1 for error. */
        int keys_res = 1;
        strbuffer_t unrecognized_keys;
        json_t *value;
        long unpacked = 0;

        if (gotopt || json_object_size(root) != key_set.size) {
            json_object_foreach(root, key, value) {
                if (!hashtable_get(&key_set, key)) {
                    unpacked++;

                    /* Save unrecognized keys for the error message */
                    if (keys_res == 1) {
                        keys_res = strbuffer_init(&unrecognized_keys);
                    } else if (!keys_res) {
                        keys_res = strbuffer_append_bytes(&unrecognized_keys, ", ", 2);
                    }

                    if (!keys_res)
                        keys_res =
                            strbuffer_append_bytes(&unrecognized_keys, key, strlen(key));
                }
            }
        }
        if (unpacked) {
            set_error(s, "<validation>", json_error_end_of_input_expected,
                      "%li object item(s) left unpacked: %s", unpacked,
                      keys_res ? "<unknown>" : strbuffer_value(&unrecognized_keys));
            strbuffer_close(&unrecognized_keys);
            goto out;
        }
    }

    ret = 0;

out:
    hashtable_close(&key_set);
    return ret;
}

static int unpack_array(scanner_t *s, json_t *root, va_list *ap) {
    size_t i = 0;
    int strict = 0;

    if (root && !json_is_array(root)) {
        set_error(s, "<validation>", json_error_wrong_type, "Expected array, got %s",
                  type_name(root));
        return -1;
    }
    next_token(s);

    while (token(s) != ']') {
        json_t *value;

        if (strict != 0) {
            set_error(s, "<format>", json_error_invalid_format,
                      "Expected ']' after '%c', got '%c'", (strict == 1 ? '!' : '*'),
                      token(s));
            return -1;
        }

        if (!token(s)) {
            set_error(s, "<format>", json_error_invalid_format,
                      "Unexpected end of format string");
            return -1;
        }

        if (token(s) == '!' || token(s) == '*') {
            strict = (token(s) == '!' ? 1 : -1);
            next_token(s);
            continue;
        }

        if (!strchr(unpack_value_starters, token(s))) {
            set_error(s, "<format>", json_error_invalid_format,
                      "Unexpected format character '%c'", token(s));
            return -1;
        }

        if (!root) {
            /* skipping */
            value = NULL;
        } else {
            value = json_array_get(root, i);
            if (!value) {
                set_error(s, "<validation>", json_error_index_out_of_range,
                          "Array index %lu out of range", (unsigned long)i);
                return -1;
            }
        }

        if (unpack(s, value, ap))
            return -1;

        next_token(s);
        i++;
    }

    if (strict == 0 && (s->flags & JSON_STRICT))
        strict = 1;

    if (root && strict == 1 && i != json_array_size(root)) {
        long diff = (long)json_array_size(root) - (long)i;
        set_error(s, "<validation>", json_error_end_of_input_expected,
                  "%li array item(s) left unpacked", diff);
        return -1;
    }

    return 0;
}

static int unpack(scanner_t *s, json_t *root, va_list *ap) {
    switch (token(s)) {
        case '{':
            return unpack_object(s, root, ap);

        case '[':
            return unpack_array(s, root, ap);

        case 's':
            if (root && !json_is_string(root)) {
                set_error(s, "<validation>", json_error_wrong_type,
                          "Expected string, got %s", type_name(root));
                return -1;
            }

            if (!(s->flags & JSON_VALIDATE_ONLY)) {
                const char **str_target;
                size_t *len_target = NULL;

                str_target = va_arg(*ap, const char **);
                if (!str_target) {
                    set_error(s, "<args>", json_error_null_value, "NULL string argument");
                    return -1;
                }

                next_token(s);

                if (token(s) == '%') {
                    len_target = va_arg(*ap, size_t *);
                    if (!len_target) {
                        set_error(s, "<args>", json_error_null_value,
                                  "NULL string length argument");
                        return -1;
                    }
                } else
                    prev_token(s);

                if (root) {
                    *str_target = json_string_value(root);
                    if (len_target)
                        *len_target = json_string_length(root);
                }
            }
            return 0;

        case 'i':
            if (root && !json_is_integer(root)) {
                set_error(s, "<validation>", json_error_wrong_type,
                          "Expected integer, got %s", type_name(root));
                return -1;
            }

            if (!(s->flags & JSON_VALIDATE_ONLY)) {
                int *target = va_arg(*ap, int *);
                if (root)
                    *target = (int)json_integer_value(root);
            }

            return 0;

        case 'I':
            if (root && !json_is_integer(root)) {
                set_error(s, "<validation>", json_error_wrong_type,
                          "Expected integer, got %s", type_name(root));
                return -1;
            }

            if (!(s->flags & JSON_VALIDATE_ONLY)) {
                json_int_t *target = va_arg(*ap, json_int_t *);
                if (root)
                    *target = json_integer_value(root);
            }

            return 0;

        case 'b':
            if (root && !json_is_boolean(root)) {
                set_error(s, "<validation>", json_error_wrong_type,
                          "Expected true or false, got %s", type_name(root));
                return -1;
            }

            if (!(s->flags & JSON_VALIDATE_ONLY)) {
                int *target = va_arg(*ap, int *);
                if (root)
                    *target = json_is_true(root);
            }

            return 0;

        case 'f':
            if (root && !json_is_real(root)) {
                set_error(s, "<validation>", json_error_wrong_type,
                          "Expected real, got %s", type_name(root));
                return -1;
            }

            if (!(s->flags & JSON_VALIDATE_ONLY)) {
                double *target = va_arg(*ap, double *);
                if (root)
                    *target = json_real_value(root);
            }

            return 0;

        case 'F':
            if (root && !json_is_number(root)) {
                set_error(s, "<validation>", json_error_wrong_type,
                          "Expected real or integer, got %s", type_name(root));
                return -1;
            }

            if (!(s->flags & JSON_VALIDATE_ONLY)) {
                double *target = va_arg(*ap, double *);
                if (root)
                    *target = json_number_value(root);
            }

            return 0;

        case 'O':
            if (root && !(s->flags & JSON_VALIDATE_ONLY))
                json_incref(root);
            /* Fall through */

        case 'o':
            if (!(s->flags & JSON_VALIDATE_ONLY)) {
                json_t **target = va_arg(*ap, json_t **);
                if (root)
                    *target = root;
            }

            return 0;

        case 'n':
            /* Never assign, just validate */
            if (root && !json_is_null(root)) {
                set_error(s, "<validation>", json_error_wrong_type,
                          "Expected null, got %s", type_name(root));
                return -1;
            }
            return 0;

        default:
            set_error(s, "<format>", json_error_invalid_format,
                      "Unexpected format character '%c'", token(s));
            return -1;
    }
}

json_t *json_vpack_ex(json_error_t *error, size_t flags, const char *fmt, va_list ap) {
    scanner_t s;
    va_list ap_copy;
    json_t *value;

    if (!fmt || !*fmt) {
        jsonp_error_init(error, "<format>");
        jsonp_error_set(error, -1, -1, 0, json_error_invalid_argument,
                        "NULL or empty format string");
        return NULL;
    }
    jsonp_error_init(error, NULL);

    scanner_init(&s, error, flags, fmt);
    next_token(&s);

    va_copy(ap_copy, ap);
    value = pack(&s, &ap_copy);
    va_end(ap_copy);

    /* This will cover all situations where s.has_error is true */
    if (!value)
        return NULL;

    next_token(&s);
    if (token(&s)) {
        json_decref(value);
        set_error(&s, "<format>", json_error_invalid_format,
                  "Garbage after format string");
        return NULL;
    }

    return value;
}

json_t *json_pack_ex(json_error_t *error, size_t flags, const char *fmt, ...) {
    json_t *value;
    va_list ap;

    va_start(ap, fmt);
    value = json_vpack_ex(error, flags, fmt, ap);
    va_end(ap);

    return value;
}

json_t *json_pack(const char *fmt, ...) {
    json_t *value;
    va_list ap;

    va_start(ap, fmt);
    value = json_vpack_ex(NULL, 0, fmt, ap);
    va_end(ap);

    return value;
}

int json_vunpack_ex(json_t *root, json_error_t *error, size_t flags, const char *fmt,
                    va_list ap) {
    scanner_t s;
    va_list ap_copy;

    if (!root) {
        jsonp_error_init(error, "<root>");
        jsonp_error_set(error, -1, -1, 0, json_error_null_value, "NULL root value");
        return -1;
    }

    if (!fmt || !*fmt) {
        jsonp_error_init(error, "<format>");
        jsonp_error_set(error, -1, -1, 0, json_error_invalid_argument,
                        "NULL or empty format string");
        return -1;
    }
    jsonp_error_init(error, NULL);

    scanner_init(&s, error, flags, fmt);
    next_token(&s);

    va_copy(ap_copy, ap);
    if (unpack(&s, root, &ap_copy)) {
        va_end(ap_copy);
        return -1;
    }
    va_end(ap_copy);

    next_token(&s);
    if (token(&s)) {
        set_error(&s, "<format>", json_error_invalid_format,
                  "Garbage after format string");
        return -1;
    }

    return 0;
}

int json_unpack_ex(json_t *root, json_error_t *error, size_t flags, const char *fmt,
                   ...) {
    int ret;
    va_list ap;

    va_start(ap, fmt);
    ret = json_vunpack_ex(root, error, flags, fmt, ap);
    va_end(ap);

    return ret;
}

int json_unpack(json_t *root, const char *fmt, ...) {
    int ret;
    va_list ap;

    va_start(ap, fmt);
    ret = json_vunpack_ex(root, NULL, 0, fmt, ap);
    va_end(ap);

    return ret;
}

#pragma endregion

#pragma region print

static void print_json_aux (json_t *element, int indent, const char *key);

static void print_json_indent (int indent) {

    for (int i = 0; i < indent; i++) {
        putchar (' ');
    }

}

static const char *json_plural (int count) { return count == 1 ? "" : "s"; }

static void print_json_object (json_t *element, int indent) {

    print_json_indent (indent);

    const char *key = NULL;
    json_t *value = NULL;
    size_t size = json_object_size (element);
    printf ("JSON Object of %ld pair%s:\n", size, json_plural (size));
    json_object_foreach (element, key, value) {
        print_json_indent (indent + 2);
        print_json_aux (value, indent + 2, key);
    }

}

static void print_json_array (json_t *element, int indent, const char *key) {

    print_json_indent (indent);

    size_t size = json_array_size (element);
    printf("JSON Array of %ld element%s:\n", size, json_plural (size));
    for (size_t i = 0; i < size; i++) {
        print_json_aux (json_array_get (element, i), indent + 2, key);
    }

}

static void print_json_string (json_t *element, int indent, const char *key) {

    print_json_indent(indent);
    printf ("[String] %s: \"%s\"\n", key, json_string_value (element));

}

static void print_json_integer (json_t *element, int indent, const char *key) {

    print_json_indent (indent);
    printf ("[Integer] %s: \"%" JSON_INTEGER_FORMAT "\"\n", key, json_integer_value(element));

}

static void print_json_real (json_t *element, int indent, const char *key) {

    print_json_indent (indent);
    printf ("[Real] %s: %f\n", key, json_real_value (element));

}

static void print_json_true (json_t *element, int indent, const char *key) {

    print_json_indent (indent);
    printf ("%s: True\n", key);

}

static void print_json_false (json_t *element, int indent, const char *key) {
    
    print_json_indent (indent);
    printf ("%s: False\n", key);

}

static void print_json_null (json_t *element, int indent, const char *key) {

    print_json_indent (indent);
    printf ("%s: NULL\n", key);

}

static void print_json_aux (json_t *element, int indent, const char *key) {

    switch (json_typeof (element)) {
        case JSON_OBJECT: print_json_object (element, indent); break;
        case JSON_ARRAY: print_json_array (element, indent, key); break;
        case JSON_STRING: print_json_string (element, indent, key); break;
        case JSON_INTEGER: print_json_integer (element, indent, key); break;
        case JSON_REAL: print_json_real (element, indent, key); break;
        case JSON_TRUE: print_json_true (element, indent, key); break;
        case JSON_FALSE: print_json_false (element, indent, key); break;
        case JSON_NULL: print_json_null (element, indent, key); break;

        default: fprintf (stderr, "unrecognized JSON type %d\n", json_typeof (element)); break;
    }

}

void json_print (json_t *root) { print_json_aux (root, 0, NULL); }

#pragma endregion