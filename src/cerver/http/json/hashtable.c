/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */
#include <stdlib.h>
#include <string.h>

// #if HAVE_STDINT_H
#include <stdint.h>
#include <stddef.h>
#include <time.h>
// #endif

#include "cerver/http/json/json.h"
#include "cerver/http/json/private.h"
#include "cerver/http/json/config.h"
#include "cerver/http/json/hashtable.h"

#ifndef INITIAL_HASHTABLE_ORDER
#define INITIAL_HASHTABLE_ORDER 3
#endif

typedef struct hashtable_list list_t;
typedef struct hashtable_pair pair_t;
typedef struct hashtable_bucket bucket_t;

extern volatile uint32_t hashtable_seed;

#pragma region lookup3

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>  /* attempt to define endianness */
#endif

#ifdef HAVE_ENDIAN_H
# include <endian.h>    /* attempt to define endianness */
#endif

/*
 * My best guess at if you are big-endian or little-endian.  This may
 * need adjustment.
 */
#if (defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && \
     __BYTE_ORDER == __LITTLE_ENDIAN) || \
    (defined(i386) || defined(__i386__) || defined(__i486__) || \
     defined(__i586__) || defined(__i686__) || defined(vax) || defined(MIPSEL))
# define HASH_LITTLE_ENDIAN 1
# define HASH_BIG_ENDIAN 0
#elif (defined(__BYTE_ORDER) && defined(__BIG_ENDIAN) && \
       __BYTE_ORDER == __BIG_ENDIAN) || \
      (defined(sparc) || defined(POWERPC) || defined(mc68000) || defined(sel))
# define HASH_LITTLE_ENDIAN 0
# define HASH_BIG_ENDIAN 1
#else
# define HASH_LITTLE_ENDIAN 0
# define HASH_BIG_ENDIAN 0
#endif

#define hashsize(n) ((uint32_t)1<<(n))
#define hashmask(n) (hashsize(n)-1)
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

/*
-------------------------------------------------------------------------------
mix -- mix 3 32-bit values reversibly.

This is reversible, so any information in (a,b,c) before mix() is
still in (a,b,c) after mix().

If four pairs of (a,b,c) inputs are run through mix(), or through
mix() in reverse, there are at least 32 bits of the output that
are sometimes the same for one pair and different for another pair.
This was tested for:
* pairs that differed by one bit, by two bits, in any combination
  of top bits of (a,b,c), or in any combination of bottom bits of
  (a,b,c).
* "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
  the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
  is commonly produced by subtraction) look like a single 1-bit
  difference.
* the base values were pseudorandom, all zero but one bit set, or 
  all zero plus a counter that starts at zero.

Some k values for my "a-=c; a^=rot(c,k); c+=b;" arrangement that
satisfy this are
    4  6  8 16 19  4
    9 15  3 18 27 15
   14  9  3  7 17  3
Well, "9 15 3 18 27 15" didn't quite get 32 bits diffing
for "differ" defined as + with a one-bit base and a two-bit delta.  I
used http://burtleburtle.net/bob/hash/avalanche.html to choose 
the operations, constants, and arrangements of the variables.

This does not achieve avalanche.  There are input bits of (a,b,c)
that fail to affect some output bits of (a,b,c), especially of a.  The
most thoroughly mixed value is c, but it doesn't really even achieve
avalanche in c.

This allows some parallelism.  Read-after-writes are good at doubling
the number of bits affected, so the goal of mixing pulls in the opposite
direction as the goal of parallelism.  I did what I could.  Rotates
seem to cost as much as shifts on every machine I could lay my hands
on, and rotates are much kinder to the top and bottom bits, so I used
rotates.
-------------------------------------------------------------------------------
*/
#define mix(a,b,c) \
{ \
  a -= c;  a ^= rot(c, 4);  c += b; \
  b -= a;  b ^= rot(a, 6);  a += c; \
  c -= b;  c ^= rot(b, 8);  b += a; \
  a -= c;  a ^= rot(c,16);  c += b; \
  b -= a;  b ^= rot(a,19);  a += c; \
  c -= b;  c ^= rot(b, 4);  b += a; \
}

/*
-------------------------------------------------------------------------------
final -- final mixing of 3 32-bit values (a,b,c) into c

Pairs of (a,b,c) values differing in only a few bits will usually
produce values of c that look totally different.  This was tested for
* pairs that differed by one bit, by two bits, in any combination
  of top bits of (a,b,c), or in any combination of bottom bits of
  (a,b,c).
* "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
  the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
  is commonly produced by subtraction) look like a single 1-bit
  difference.
* the base values were pseudorandom, all zero but one bit set, or 
  all zero plus a counter that starts at zero.

These constants passed:
 14 11 25 16 4 14 24
 12 14 25 16 4 14 24
and these came close:
  4  8 15 26 3 22 24
 10  8 15 26 3 22 24
 11  8 15 26 3 22 24
-------------------------------------------------------------------------------
*/
#define final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}

/*
-------------------------------------------------------------------------------
hashlittle() -- hash a variable-length key into a 32-bit value
  k       : the key (the unaligned variable-length array of bytes)
  length  : the length of the key, counting by bytes
  initval : can be any 4-byte value
Returns a 32-bit value.  Every bit of the key affects every bit of
the return value.  Two keys differing by one or two bits will have
totally different hash values.

The best hash table sizes are powers of 2.  There is no need to do
mod a prime (mod is sooo slow!).  If you need less than 32 bits,
use a bitmask.  For example, if you need only 10 bits, do
  h = (h & hashmask(10));
In which case, the hash table should have hashsize(10) elements.

If you are hashing n strings (uint8_t **)k, do it like this:
  for (i=0, h=0; i<n; ++i) h = hashlittle( k[i], len[i], h);

By Bob Jenkins, 2006.  bob_jenkins@burtleburtle.net.  You may use this
code any way you wish, private, educational, or commercial.  It's free.

Use for hash table lookup, or anything where one collision in 2^^32 is
acceptable.  Do NOT use for cryptographic purposes.
-------------------------------------------------------------------------------
*/

static uint32_t hashlittle(const void *key, size_t length, uint32_t initval)
{
  uint32_t a,b,c;                                          /* internal state */
  union { const void *ptr; size_t i; } u;     /* needed for Mac Powerbook G4 */

  /* Set up the internal state */
  a = b = c = 0xdeadbeef + ((uint32_t)length) + initval;

  u.ptr = key;
  if (HASH_LITTLE_ENDIAN && ((u.i & 0x3) == 0)) {
    const uint32_t *k = (const uint32_t *)key;         /* read 32-bit chunks */

/* Detect Valgrind or AddressSanitizer */
#ifdef VALGRIND
# define NO_MASKING_TRICK 1
#else
# if defined(__has_feature)  /* Clang */
#  if __has_feature(address_sanitizer)  /* is ASAN enabled? */
#   define NO_MASKING_TRICK 1
#  endif
# else
#  if defined(__SANITIZE_ADDRESS__)  /* GCC 4.8.x, is ASAN enabled? */
#   define NO_MASKING_TRICK 1
#  endif
# endif
#endif

#ifdef NO_MASKING_TRICK
    const uint8_t  *k8;
#endif

    /*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      b += k[1];
      c += k[2];
      mix(a,b,c);
      length -= 12;
      k += 3;
    }

    /*----------------------------- handle the last (probably partial) block */
    /* 
     * "k[2]&0xffffff" actually reads beyond the end of the string, but
     * then masks off the part it's not allowed to read.  Because the
     * string is aligned, the masked-off tail is in the same word as the
     * rest of the string.  Every machine with memory protection I've seen
     * does it on word boundaries, so is OK with this.  But VALGRIND will
     * still catch it and complain.  The masking trick does make the hash
     * noticeably faster for short strings (like English words).
     */
#ifndef NO_MASKING_TRICK

    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=k[2]&0xffffff; b+=k[1]; a+=k[0]; break;
    case 10: c+=k[2]&0xffff; b+=k[1]; a+=k[0]; break;
    case 9 : c+=k[2]&0xff; b+=k[1]; a+=k[0]; break;
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=k[1]&0xffffff; a+=k[0]; break;
    case 6 : b+=k[1]&0xffff; a+=k[0]; break;
    case 5 : b+=k[1]&0xff; a+=k[0]; break;
    case 4 : a+=k[0]; break;
    case 3 : a+=k[0]&0xffffff; break;
    case 2 : a+=k[0]&0xffff; break;
    case 1 : a+=k[0]&0xff; break;
    case 0 : return c;              /* zero length strings require no mixing */
    }

#else /* make valgrind happy */

    k8 = (const uint8_t *)k;
    switch(length)
    {
    case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
    case 11: c+=((uint32_t)k8[10])<<16;  /* fall through */
    case 10: c+=((uint32_t)k8[9])<<8;    /* fall through */
    case 9 : c+=k8[8];                   /* fall through */
    case 8 : b+=k[1]; a+=k[0]; break;
    case 7 : b+=((uint32_t)k8[6])<<16;   /* fall through */
    case 6 : b+=((uint32_t)k8[5])<<8;    /* fall through */
    case 5 : b+=k8[4];                   /* fall through */
    case 4 : a+=k[0]; break;
    case 3 : a+=((uint32_t)k8[2])<<16;   /* fall through */
    case 2 : a+=((uint32_t)k8[1])<<8;    /* fall through */
    case 1 : a+=k8[0]; break;
    case 0 : return c;
    }

#endif /* !valgrind */

  } else if (HASH_LITTLE_ENDIAN && ((u.i & 0x1) == 0)) {
    const uint16_t *k = (const uint16_t *)key;         /* read 16-bit chunks */
    const uint8_t  *k8;

    /*--------------- all but last block: aligned reads and different mixing */
    while (length > 12)
    {
      a += k[0] + (((uint32_t)k[1])<<16);
      b += k[2] + (((uint32_t)k[3])<<16);
      c += k[4] + (((uint32_t)k[5])<<16);
      mix(a,b,c);
      length -= 12;
      k += 6;
    }

    /*----------------------------- handle the last (probably partial) block */
    k8 = (const uint8_t *)k;
    switch(length)
    {
    case 12: c+=k[4]+(((uint32_t)k[5])<<16);
             b+=k[2]+(((uint32_t)k[3])<<16);
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 11: c+=((uint32_t)k8[10])<<16;     /* fall through */
    case 10: c+=k[4];
             b+=k[2]+(((uint32_t)k[3])<<16);
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 9 : c+=k8[8];                      /* fall through */
    case 8 : b+=k[2]+(((uint32_t)k[3])<<16);
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 7 : b+=((uint32_t)k8[6])<<16;      /* fall through */
    case 6 : b+=k[2];
             a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 5 : b+=k8[4];                      /* fall through */
    case 4 : a+=k[0]+(((uint32_t)k[1])<<16);
             break;
    case 3 : a+=((uint32_t)k8[2])<<16;      /* fall through */
    case 2 : a+=k[0];
             break;
    case 1 : a+=k8[0];
             break;
    case 0 : return c;                     /* zero length requires no mixing */
    }

  } else {                        /* need to read the key one byte at a time */
    const uint8_t *k = (const uint8_t *)key;

    /*--------------- all but the last block: affect some 32 bits of (a,b,c) */
    while (length > 12)
    {
      a += k[0];
      a += ((uint32_t)k[1])<<8;
      a += ((uint32_t)k[2])<<16;
      a += ((uint32_t)k[3])<<24;
      b += k[4];
      b += ((uint32_t)k[5])<<8;
      b += ((uint32_t)k[6])<<16;
      b += ((uint32_t)k[7])<<24;
      c += k[8];
      c += ((uint32_t)k[9])<<8;
      c += ((uint32_t)k[10])<<16;
      c += ((uint32_t)k[11])<<24;
      mix(a,b,c);
      length -= 12;
      k += 12;
    }

    /*-------------------------------- last block: affect all 32 bits of (c) */
    switch(length)                   /* all the case statements fall through */
    {
    case 12: c+=((uint32_t)k[11])<<24; /* fall through */
    case 11: c+=((uint32_t)k[10])<<16; /* fall through */
    case 10: c+=((uint32_t)k[9])<<8; /* fall through */
    case 9 : c+=k[8]; /* fall through */
    case 8 : b+=((uint32_t)k[7])<<24; /* fall through */
    case 7 : b+=((uint32_t)k[6])<<16; /* fall through */
    case 6 : b+=((uint32_t)k[5])<<8; /* fall through */
    case 5 : b+=k[4]; /* fall through */
    case 4 : a+=((uint32_t)k[3])<<24; /* fall through */
    case 3 : a+=((uint32_t)k[2])<<16; /* fall through */
    case 2 : a+=((uint32_t)k[1])<<8; /* fall through */
    case 1 : a+=k[0];
             break;
    case 0 : return c;
    }
  }

  final(a,b,c);
  return c;
  
}

#pragma endregion

#define list_to_pair(list_)         container_of(list_, pair_t, list)
#define ordered_list_to_pair(list_) container_of(list_, pair_t, ordered_list)
#define hash_str(key)               ((size_t)hashlittle((key), strlen(key), hashtable_seed))

static CERVER_INLINE void list_init(list_t *list) {
    list->next = list;
    list->prev = list;
}

static CERVER_INLINE void list_insert(list_t *list, list_t *node) {
    node->next = list;
    node->prev = list->prev;
    list->prev->next = node;
    list->prev = node;
}

static CERVER_INLINE void list_remove(list_t *list) {
    list->prev->next = list->next;
    list->next->prev = list->prev;
}

static CERVER_INLINE int bucket_is_empty(hashtable_t *hashtable, bucket_t *bucket) {
    return bucket->first == &hashtable->list && bucket->first == bucket->last;
}

static void insert_to_bucket(hashtable_t *hashtable, bucket_t *bucket, list_t *list) {
    if (bucket_is_empty(hashtable, bucket)) {
        list_insert(&hashtable->list, list);
        bucket->first = bucket->last = list;
    } else {
        list_insert(bucket->first, list);
        bucket->first = list;
    }
}

static pair_t *hashtable_find_pair(hashtable_t *hashtable, bucket_t *bucket,
                                   const char *key, size_t hash) {
    list_t *list;
    pair_t *pair;

    if (bucket_is_empty(hashtable, bucket))
        return NULL;

    list = bucket->first;
    while (1) {
        pair = list_to_pair(list);
        if (pair->hash == hash && strcmp(pair->key, key) == 0)
            return pair;

        if (list == bucket->last)
            break;

        list = list->next;
    }

    return NULL;
}

/* returns 0 on success, -1 if key was not found */
static int hashtable_do_del(hashtable_t *hashtable, const char *key, size_t hash) {
    pair_t *pair;
    bucket_t *bucket;
    size_t index;

    index = hash & hashmask(hashtable->order);
    bucket = &hashtable->buckets[index];

    pair = hashtable_find_pair(hashtable, bucket, key, hash);
    if (!pair)
        return -1;

    if (&pair->list == bucket->first && &pair->list == bucket->last)
        bucket->first = bucket->last = &hashtable->list;

    else if (&pair->list == bucket->first)
        bucket->first = pair->list.next;

    else if (&pair->list == bucket->last)
        bucket->last = pair->list.prev;

    list_remove(&pair->list);
    list_remove(&pair->ordered_list);
    json_decref(pair->value);

    jsonp_free(pair);
    hashtable->size--;

    return 0;
}

static void hashtable_do_clear(hashtable_t *hashtable) {
    list_t *list, *next;
    pair_t *pair;

    for (list = hashtable->list.next; list != &hashtable->list; list = next) {
        next = list->next;
        pair = list_to_pair(list);
        json_decref(pair->value);
        jsonp_free(pair);
    }
}

static int hashtable_do_rehash(hashtable_t *hashtable) {
    list_t *list, *next;
    pair_t *pair;
    size_t i, index, new_size, new_order;
    struct hashtable_bucket *new_buckets;

    new_order = hashtable->order + 1;
    new_size = hashsize(new_order);

    new_buckets = (struct hashtable_bucket *) jsonp_malloc(new_size * sizeof(bucket_t));
    if (!new_buckets)
        return -1;

    jsonp_free(hashtable->buckets);
    hashtable->buckets = new_buckets;
    hashtable->order = new_order;

    for (i = 0; i < hashsize(hashtable->order); i++) {
        hashtable->buckets[i].first = hashtable->buckets[i].last = &hashtable->list;
    }

    list = hashtable->list.next;
    list_init(&hashtable->list);

    for (; list != &hashtable->list; list = next) {
        next = list->next;
        pair = list_to_pair(list);
        index = pair->hash % new_size;
        insert_to_bucket(hashtable, &hashtable->buckets[index], &pair->list);
    }

    return 0;
}

int hashtable_init(hashtable_t *hashtable) {
    size_t i;

    hashtable->size = 0;
    hashtable->order = INITIAL_HASHTABLE_ORDER;
    hashtable->buckets = (struct hashtable_bucket *) jsonp_malloc(hashsize(hashtable->order) * sizeof(bucket_t));
    if (!hashtable->buckets)
        return -1;

    list_init(&hashtable->list);
    list_init(&hashtable->ordered_list);

    for (i = 0; i < hashsize(hashtable->order); i++) {
        hashtable->buckets[i].first = hashtable->buckets[i].last = &hashtable->list;
    }

    return 0;
}

void hashtable_close(hashtable_t *hashtable) {
    hashtable_do_clear(hashtable);
    jsonp_free(hashtable->buckets);
}

int hashtable_set(hashtable_t *hashtable, const char *key, json_t *value) {
    pair_t *pair;
    bucket_t *bucket;
    size_t hash, index;

    /* rehash if the load ratio exceeds 1 */
    if (hashtable->size >= hashsize(hashtable->order))
        if (hashtable_do_rehash(hashtable))
            return -1;

    hash = hash_str(key);
    index = hash & hashmask(hashtable->order);
    bucket = &hashtable->buckets[index];
    pair = hashtable_find_pair(hashtable, bucket, key, hash);

    if (pair) {
        json_decref(pair->value);
        pair->value = value;
    } else {
        /* offsetof(...) returns the size of pair_t without the last,
           flexible member. This way, the correct amount is
           allocated. */

        size_t len = strlen(key);
        if (len >= (size_t)-1 - offsetof(pair_t, key)) {
            /* Avoid an overflow if the key is very long */
            return -1;
        }

        pair = (pair_t *) jsonp_malloc(offsetof(pair_t, key) + len + 1);
        if (!pair)
            return -1;

        pair->hash = hash;
        strncpy(pair->key, key, len + 1);
        pair->value = value;
        list_init(&pair->list);
        list_init(&pair->ordered_list);

        insert_to_bucket(hashtable, bucket, &pair->list);
        list_insert(&hashtable->ordered_list, &pair->ordered_list);

        hashtable->size++;
    }
    return 0;
}

void *hashtable_get(hashtable_t *hashtable, const char *key) {
    pair_t *pair;
    size_t hash;
    bucket_t *bucket;

    hash = hash_str(key);
    bucket = &hashtable->buckets[hash & hashmask(hashtable->order)];

    pair = hashtable_find_pair(hashtable, bucket, key, hash);
    if (!pair)
        return NULL;

    return pair->value;
}

int hashtable_del(hashtable_t *hashtable, const char *key) {
    size_t hash = hash_str(key);
    return hashtable_do_del(hashtable, key, hash);
}

void hashtable_clear(hashtable_t *hashtable) {
    size_t i;

    hashtable_do_clear(hashtable);

    for (i = 0; i < hashsize(hashtable->order); i++) {
        hashtable->buckets[i].first = hashtable->buckets[i].last = &hashtable->list;
    }

    list_init(&hashtable->list);
    list_init(&hashtable->ordered_list);
    hashtable->size = 0;
}

void *hashtable_iter(hashtable_t *hashtable) {
    return hashtable_iter_next(hashtable, &hashtable->ordered_list);
}

void *hashtable_iter_at(hashtable_t *hashtable, const char *key) {
    pair_t *pair;
    size_t hash;
    bucket_t *bucket;

    hash = hash_str(key);
    bucket = &hashtable->buckets[hash & hashmask(hashtable->order)];

    pair = hashtable_find_pair(hashtable, bucket, key, hash);
    if (!pair)
        return NULL;

    return &pair->ordered_list;
}

void *hashtable_iter_next(hashtable_t *hashtable, void *iter) {
    list_t *list = (list_t *)iter;
    if (list->next == &hashtable->ordered_list)
        return NULL;
    return list->next;
}

void *hashtable_iter_key(void *iter) {
    pair_t *pair = ordered_list_to_pair((list_t *)iter);
    return pair->key;
}

void *hashtable_iter_value(void *iter) {
    pair_t *pair = ordered_list_to_pair((list_t *)iter);
    return pair->value;
}

void hashtable_iter_set(void *iter, json_t *value) {
    pair_t *pair = ordered_list_to_pair((list_t *)iter);

    json_decref(pair->value);
    pair->value = value;
}

// static uint32_t buf_to_uint32(char *data) {
//     size_t i;
//     uint32_t result = 0;

//     for (i = 0; i < sizeof(uint32_t); i++)
//         result = (result << 8) | (unsigned char)data[i];

//     return result;
// }

/* /dev/urandom */
#if !defined(_WIN32) && defined(USE_URANDOM)
static int seed_from_urandom(uint32_t *seed) {
    /* Use unbuffered I/O if we have open(), close() and read(). Otherwise
       fall back to fopen() */

    char data[sizeof(uint32_t)];
    int ok;

#if defined(HAVE_OPEN) && defined(HAVE_CLOSE) && defined(HAVE_READ)
    int urandom;
    urandom = open("/dev/urandom", O_RDONLY);
    if (urandom == -1)
        return 1;

    ok = read(urandom, data, sizeof(uint32_t)) == sizeof(uint32_t);
    close(urandom);
#else
    FILE *urandom;

    urandom = fopen("/dev/urandom", "rb");
    if (!urandom)
        return 1;

    ok = fread(data, 1, sizeof(uint32_t), urandom) == sizeof(uint32_t);
    fclose(urandom);
#endif

    if (!ok)
        return 1;

    *seed = buf_to_uint32(data);
    return 0;
}
#endif

/* gettimeofday() and getpid() */
static int seed_from_timestamp_and_pid(uint32_t *seed) {
#ifdef HAVE_GETTIMEOFDAY
    /* XOR of seconds and microseconds */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *seed = (uint32_t)tv.tv_sec ^ (uint32_t)tv.tv_usec;
#else
    /* Seconds only */
    *seed = (uint32_t)time(NULL);
#endif

    /* XOR with PID for more randomness */
#if defined(_WIN32)
    *seed ^= (uint32_t)GetCurrentProcessId();
#elif defined(HAVE_GETPID)
    *seed ^= (uint32_t)getpid();
#endif

    return 0;
}

static uint32_t generate_seed() {
    uint32_t seed = 0;
    int done = 0;

#if !defined(_WIN32) && defined(USE_URANDOM)
    if (seed_from_urandom(&seed) == 0)
        done = 1;
#endif

#if defined(_WIN32) && defined(USE_WINDOWS_CRYPTOAPI)
    if (seed_from_windows_cryptoapi(&seed) == 0)
        done = 1;
#endif

    if (!done) {
        /* Fall back to timestamp and PID if no better randomness is
           available */
        seed_from_timestamp_and_pid(&seed);
    }

    /* Make sure the seed is never zero */
    if (seed == 0)
        seed = 1;

    return seed;
}

volatile uint32_t hashtable_seed = 0;

#if defined(HAVE_ATOMIC_BUILTINS) && (defined(HAVE_SCHED_YIELD) || !defined(_WIN32))
static volatile char seed_initialized = 0;

void json_object_seed(size_t seed) {
    uint32_t new_seed = (uint32_t)seed;

    if (hashtable_seed == 0) {
        if (__atomic_test_and_set(&seed_initialized, __ATOMIC_RELAXED) == 0) {
            /* Do the seeding ourselves */
            if (new_seed == 0)
                new_seed = generate_seed();

            __atomic_store_n(&hashtable_seed, new_seed, __ATOMIC_RELEASE);
        } else {
            /* Wait for another thread to do the seeding */
            do {
#ifdef HAVE_SCHED_YIELD
                sched_yield();
#endif
            } while (__atomic_load_n(&hashtable_seed, __ATOMIC_ACQUIRE) == 0);
        }
    }
}
#elif defined(HAVE_SYNC_BUILTINS) && (defined(HAVE_SCHED_YIELD) || !defined(_WIN32))
void json_object_seed(size_t seed) {
    uint32_t new_seed = (uint32_t)seed;

    if (hashtable_seed == 0) {
        if (new_seed == 0) {
            /* Explicit synchronization fences are not supported by the
               __sync builtins, so every thread getting here has to
               generate the seed value.
            */
            new_seed = generate_seed();
        }

        do {
            if (__sync_bool_compare_and_swap(&hashtable_seed, 0, new_seed)) {
                /* We were the first to seed */
                break;
            } else {
                /* Wait for another thread to do the seeding */
#ifdef HAVE_SCHED_YIELD
                sched_yield();
#endif
            }
        } while (hashtable_seed == 0);
    }
}
#else
/* Fall back to a thread-unsafe version */
void json_object_seed(size_t seed) {
    uint32_t new_seed = (uint32_t)seed;

    if (hashtable_seed == 0) {
        if (new_seed == 0)
            new_seed = generate_seed();

        hashtable_seed = new_seed;
    }
}
#endif