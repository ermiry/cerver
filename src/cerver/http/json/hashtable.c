/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#include "cerver/http/json/config.h"
#include "cerver/http/json/hashtable.h"
#include "cerver/http/json/json.h"
#include "cerver/http/json/private.h"

// a new table has 2^3 buckets
#define JSON_HASHTABLE_INITIAL_ORDER		3

typedef struct hashtable_list list_t;
typedef struct hashtable_pair pair_t;
typedef struct hashtable_bucket bucket_t;

static uint32_t hashtable_seed_get (void);

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
#if (defined (__BYTE_ORDER) && defined (__LITTLE_ENDIAN) && \
     __BYTE_ORDER == __LITTLE_ENDIAN) || \
    (defined (i386) || defined (__i386__) || defined (__i486__) || \
     defined (__i586__) || defined (__i686__) || defined (vax) || defined (MIPSEL))
# define HASH_LITTLE_ENDIAN 1
# define HASH_BIG_ENDIAN 0
#elif (defined (__BYTE_ORDER) && defined (__BIG_ENDIAN) && \
       __BYTE_ORDER == __BIG_ENDIAN) || \
      (defined (sparc) || defined (POWERPC) || defined (mc68000) || defined (sel))
# define HASH_LITTLE_ENDIAN 0
# define HASH_BIG_ENDIAN 1
#else
# define HASH_LITTLE_ENDIAN 0
# define HASH_BIG_ENDIAN 0
#endif

#define hashsize(n) ((uint32_t)1<<(n))
#define hashmask(n) (hashsize (n)-1)
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
  a -= c;  a ^= rot (c, 4);  c += b; \
  b -= a;  b ^= rot (a, 6);  a += c; \
  c -= b;  c ^= rot (b, 8);  b += a; \
  a -= c;  a ^= rot (c,16);  c += b; \
  b -= a;  b ^= rot (a,19);  a += c; \
  c -= b;  c ^= rot (b, 4);  b += a; \
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
  c ^= b; c -= rot (b,14); \
  a ^= c; a -= rot (c,11); \
  b ^= a; b -= rot (a,25); \
  c ^= b; c -= rot (b,16); \
  a ^= c; a -= rot (c,4);  \
  b ^= a; b -= rot (a,14); \
  c ^= b; c -= rot (b,24); \
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

static uint32_t hashlittle (const void *key, size_t length, uint32_t initval)
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
# if defined (__has_feature)  /* Clang */
#  if __has_feature (address_sanitizer)  /* is ASAN enabled? */
#   define NO_MASKING_TRICK 1
#  endif
# else
#  if defined (__SANITIZE_ADDRESS__)  /* GCC 4.8.x, is ASAN enabled? */
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
      mix (a,b,c);
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

    switch (length)
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
    switch (length)
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
      mix (a,b,c);
      length -= 12;
      k += 6;
    }

    /*----------------------------- handle the last (probably partial) block */
    k8 = (const uint8_t *)k;
    switch (length)
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
      mix (a,b,c);
      length -= 12;
      k += 12;
    }

    /*-------------------------------- last block: affect all 32 bits of (c) */
    switch (length)                   /* all the case statements fall through */
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

  final (a,b,c);
  return c;
  
}

#pragma endregion

#pragma region table

#define hashtable_list_to_pair(list_)			container_of (list_, pair_t, list)
#define hashtable_ordered_list_to_pair(list_)	container_of (list_, pair_t, ordered_list)

// the hash of a key with the seed of the process
static size_t hashtable_hash_key (const char *key) {

	return (size_t) hashlittle (key, strlen (key), hashtable_seed_get ());

}

static CERVER_INLINE void hashtable_list_init (list_t *list) {

	list->next = list;
	list->prev = list;

}

static CERVER_INLINE void hashtable_list_insert (list_t *list, list_t *node) {

	node->next = list;
	node->prev = list->prev;
	list->prev->next = node;
	list->prev = node;

}

static CERVER_INLINE void hashtable_list_remove (list_t *list) {

	list->prev->next = list->next;
	list->next->prev = list->prev;

}

static CERVER_INLINE int hashtable_bucket_is_empty (hashtable_t *hashtable, bucket_t *bucket) {

	return (bucket->first == &hashtable->list) && (bucket->first == bucket->last);

}

static void hashtable_bucket_insert (hashtable_t *hashtable, bucket_t *bucket, list_t *list) {

	if (hashtable_bucket_is_empty (hashtable, bucket)) {
		hashtable_list_insert (&hashtable->list, list);
		bucket->first = list;
		bucket->last = list;
	}

	else {
		hashtable_list_insert (bucket->first, list);
		bucket->first = list;
	}

}

// every bucket of the table is empty
static void hashtable_buckets_reset (hashtable_t *hashtable) {

	for (size_t index = 0; index < hashsize (hashtable->order); index++) {
		hashtable->buckets[index].first = &hashtable->list;
		hashtable->buckets[index].last = &hashtable->list;
	}

}

static bucket_t *hashtable_bucket_of (hashtable_t *hashtable, const size_t hash) {

	return &hashtable->buckets[hash & hashmask (hashtable->order)];

}

static pair_t *hashtable_find_pair (
	hashtable_t *hashtable, bucket_t *bucket,
	const char *key, size_t hash
) {

	if (hashtable_bucket_is_empty (hashtable, bucket)) return NULL;

	list_t *list = bucket->first;
	while (true) {
		pair_t *pair = hashtable_list_to_pair (list);
		if ((pair->hash == hash) && !strcmp (pair->key, key)) return pair;

		if (list == bucket->last) break;

		list = list->next;
	}

	return NULL;

}

// the bucket no longer has the pair as its first or its last element
static void hashtable_bucket_remove (hashtable_t *hashtable, bucket_t *bucket, pair_t *pair) {

	if ((&pair->list == bucket->first) && (&pair->list == bucket->last)) {
		bucket->first = &hashtable->list;
		bucket->last = &hashtable->list;
	}

	else if (&pair->list == bucket->first) {
		bucket->first = pair->list.next;
	}

	else if (&pair->list == bucket->last) {
		bucket->last = pair->list.prev;
	}

}

// returns 0 on success, -1 if key was not found
static int hashtable_remove (hashtable_t *hashtable, const char *key, size_t hash) {

	bucket_t *bucket = hashtable_bucket_of (hashtable, hash);

	pair_t *pair = hashtable_find_pair (hashtable, bucket, key, hash);
	if (!pair) return -1;

	hashtable_bucket_remove (hashtable, bucket, pair);

	hashtable_list_remove (&pair->list);
	hashtable_list_remove (&pair->ordered_list);
	json_decref (pair->value);

	jsonp_free (pair);
	hashtable->size -= 1;

	return 0;

}

static void hashtable_release_pairs (hashtable_t *hashtable) {

	list_t *next = NULL;
	for (list_t *list = hashtable->list.next; list != &hashtable->list; list = next) {
		next = list->next;

		pair_t *pair = hashtable_list_to_pair (list);
		json_decref (pair->value);
		jsonp_free (pair);
	}

}

// doubles the buckets and inserts every pair again
static int hashtable_rehash (hashtable_t *hashtable) {

	const size_t new_order = hashtable->order + 1;
	const size_t new_size = hashsize (new_order);

	bucket_t *new_buckets = (bucket_t *) jsonp_malloc (new_size * sizeof (bucket_t));
	if (!new_buckets) return -1;

	jsonp_free (hashtable->buckets);
	hashtable->buckets = new_buckets;
	hashtable->order = new_order;

	hashtable_buckets_reset (hashtable);

	list_t *list = hashtable->list.next;
	hashtable_list_init (&hashtable->list);

	list_t *next = NULL;
	for (; list != &hashtable->list; list = next) {
		next = list->next;

		pair_t *pair = hashtable_list_to_pair (list);
		hashtable_bucket_insert (hashtable, &hashtable->buckets[pair->hash % new_size], &pair->list);
	}

	return 0;

}

int hashtable_init (hashtable_t *hashtable) {

	hashtable->size = 0;
	hashtable->order = JSON_HASHTABLE_INITIAL_ORDER;
	hashtable->buckets = (bucket_t *) jsonp_malloc (hashsize (hashtable->order) * sizeof (bucket_t));
	if (!hashtable->buckets) return -1;

	hashtable_list_init (&hashtable->list);
	hashtable_list_init (&hashtable->ordered_list);

	hashtable_buckets_reset (hashtable);

	return 0;

}

void hashtable_close (hashtable_t *hashtable) {

	hashtable_release_pairs (hashtable);
	jsonp_free (hashtable->buckets);

}

// a new pair with a copy of the key,
// offsetof () is the size of pair_t without its flexible member, the key
static pair_t *hashtable_pair_create (const char *key, size_t hash, json_t *value) {

	const size_t length = strlen (key);

	// a key so long that its size does not fit
	if (length >= (SIZE_MAX - offsetof (pair_t, key))) return NULL;

	pair_t *pair = (pair_t *) jsonp_malloc (offsetof (pair_t, key) + length + 1);
	if (pair) {
		pair->hash = hash;
		(void) memcpy (pair->key, key, length + 1);
		pair->value = value;
		hashtable_list_init (&pair->list);
		hashtable_list_init (&pair->ordered_list);
	}

	return pair;

}

int hashtable_set (hashtable_t *hashtable, const char *key, json_t *value) {

	// rehash if the load ratio exceeds 1
	if ((hashtable->size >= hashsize (hashtable->order)) && hashtable_rehash (hashtable)) return -1;

	const size_t hash = hashtable_hash_key (key);
	bucket_t *bucket = hashtable_bucket_of (hashtable, hash);

	pair_t *pair = hashtable_find_pair (hashtable, bucket, key, hash);
	if (pair) {
		json_decref (pair->value);
		pair->value = value;
	}

	else {
		pair = hashtable_pair_create (key, hash, value);
		if (!pair) return -1;

		hashtable_bucket_insert (hashtable, bucket, &pair->list);
		hashtable_list_insert (&hashtable->ordered_list, &pair->ordered_list);

		hashtable->size += 1;
	}

	return 0;

}

void *hashtable_get (hashtable_t *hashtable, const char *key) {

	const size_t hash = hashtable_hash_key (key);

	pair_t *pair = hashtable_find_pair (hashtable, hashtable_bucket_of (hashtable, hash), key, hash);

	return pair ? pair->value : NULL;

}

int hashtable_del (hashtable_t *hashtable, const char *key) {

	return hashtable_remove (hashtable, key, hashtable_hash_key (key));

}

void hashtable_clear (hashtable_t *hashtable) {

	hashtable_release_pairs (hashtable);

	hashtable_buckets_reset (hashtable);

	hashtable_list_init (&hashtable->list);
	hashtable_list_init (&hashtable->ordered_list);
	hashtable->size = 0;

}

void *hashtable_iter (hashtable_t *hashtable) {

	return hashtable_iter_next (hashtable, &hashtable->ordered_list);

}

void *hashtable_iter_at (hashtable_t *hashtable, const char *key) {

	const size_t hash = hashtable_hash_key (key);

	pair_t *pair = hashtable_find_pair (hashtable, hashtable_bucket_of (hashtable, hash), key, hash);

	return pair ? &pair->ordered_list : NULL;

}

void *hashtable_iter_next (hashtable_t *hashtable, void *iter) {

	list_t *list = (list_t *) iter;

	return (list->next == &hashtable->ordered_list) ? NULL : list->next;

}

void *hashtable_iter_key (void *iter) {

	pair_t *pair = hashtable_ordered_list_to_pair ((list_t *) iter);

	return pair->key;

}

void *hashtable_iter_value (void *iter) {

	pair_t *pair = hashtable_ordered_list_to_pair ((list_t *) iter);

	return pair->value;

}

void hashtable_iter_set (void *iter, json_t *value) {

	pair_t *pair = hashtable_ordered_list_to_pair ((list_t *) iter);

	json_decref (pair->value);
	pair->value = value;

}

#pragma endregion

#pragma region seed

// the seed of the hash of the keys of every object of the process,
// set once before the first object, it never changes after that
static uint32_t hashtable_seed = 0;

static uint32_t hashtable_seed_get (void) {

	return __atomic_load_n (&hashtable_seed, __ATOMIC_ACQUIRE);

}

// a seed that nobody can compute, so the keys that collide in the buckets
// can not be chosen to slow down every object of a request (hash flooding)
// the time, the process and an address only if the kernel gives no entropy
static uint32_t hashtable_seed_generate (void) {

	uint32_t seed = 0;
	if (getentropy (&seed, sizeof (seed))) {
		seed = (uint32_t) time (NULL) ^ (uint32_t) getpid () ^ (uint32_t) (uintptr_t) &seed;
	}

	// zero means that the seed was not set
	return seed ? seed : 1;

}

// the first call sets the seed: the given one or a random one if it is 0,
// the next calls do nothing, also when two threads call it at the same time
void json_object_seed (size_t seed) {

	if (!hashtable_seed_get ()) {
		uint32_t new_seed = (uint32_t) seed;
		if (!new_seed) new_seed = hashtable_seed_generate ();

		uint32_t expected = 0;
		(void) __atomic_compare_exchange_n (
			&hashtable_seed, &expected, new_seed,
			false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE
		);
	}

}

#pragma endregion
