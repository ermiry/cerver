#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>

#include <pthread.h>

#include "cerver/collections/freelist.h"

#include "cerver/http/core/recycle.h"
#include "cerver/http/responses/response.h"

#pragma region recycle

// a released buffer keeps the next one in its first bytes
typedef struct HttpRecycleBufferNode {

	void *next;

} HttpRecycleBufferNode;

#define HTTP_RECYCLE_BUFFER_FREELIST	FREELIST_INITIALIZER (HttpRecycleBufferNode, next, HTTP_RECYCLE_LIMIT)

static FreeList http_recycled_responses = FREELIST_INITIALIZER (HttpResponse, recycle_next, HTTP_RECYCLE_LIMIT);

static FreeList http_recycled_buffers[HTTP_RECYCLE_BUFFER_CLASSES] = {
	HTTP_RECYCLE_BUFFER_FREELIST, HTTP_RECYCLE_BUFFER_FREELIST, HTTP_RECYCLE_BUFFER_FREELIST,
	HTTP_RECYCLE_BUFFER_FREELIST, HTTP_RECYCLE_BUFFER_FREELIST, HTTP_RECYCLE_BUFFER_FREELIST,
	HTTP_RECYCLE_BUFFER_FREELIST, HTTP_RECYCLE_BUFFER_FREELIST, HTTP_RECYCLE_BUFFER_FREELIST
};

// the responses and buffers that a thread takes and releases without a lock,
// the shared lists are only used when it is empty or full
typedef struct HttpRecycleThreadCache {

	HttpResponse *responses[HTTP_RECYCLE_THREAD_CACHE_SIZE];
	unsigned int n_responses;

	void *buffers[HTTP_RECYCLE_BUFFER_CLASSES][HTTP_RECYCLE_THREAD_CACHE_SIZE];
	unsigned int n_buffers[HTTP_RECYCLE_BUFFER_CLASSES];

} HttpRecycleThreadCache;

// 0 in the builds that do not recycle
static const unsigned int http_recycle_thread_cache_limit = ((HTTP_RECYCLE_LIMIT) > 0) ? HTTP_RECYCLE_THREAD_CACHE_SIZE : 0;

// the key only releases the cache of a thread that ends, the thread reads its own from http_recycle_thread_cache
static pthread_key_t http_recycle_cache_key;
static pthread_once_t http_recycle_cache_once = PTHREAD_ONCE_INIT;
static bool http_recycle_cache_key_created = false;

static __thread HttpRecycleThreadCache *http_recycle_thread_cache = NULL;

#define HTTP_RECYCLE_BUFFER_MIN_SIZE	((size_t) 1 << HTTP_RECYCLE_BUFFER_MIN_EXPONENT)
#define HTTP_RECYCLE_BUFFER_MAX_SIZE	((size_t) 1 << HTTP_RECYCLE_BUFFER_MAX_EXPONENT)
#define HTTP_RECYCLE_LONG_BITS			(sizeof (unsigned long) * CHAR_BIT)

// the size of the buffers of a class, that starts at 1
static size_t http_recycle_buffer_class_size (const unsigned int buffer_class) {

	return (size_t) 1 << (buffer_class - 1 + HTTP_RECYCLE_BUFFER_MIN_EXPONENT);

}

// returns the class of the buffers that fit the size, 0 if it is too big to be recycled,
// the exponent of the smallest power of 2 that fits it comes from its leading zeros
static unsigned int http_recycle_buffer_class (const size_t size) {

	unsigned int buffer_class = 0;

	if (size <= HTTP_RECYCLE_BUFFER_MIN_SIZE) {
		buffer_class = 1;
	}

	else if (size <= HTTP_RECYCLE_BUFFER_MAX_SIZE) {
		const unsigned int exponent = (unsigned int) (HTTP_RECYCLE_LONG_BITS - (size_t) __builtin_clzl ((unsigned long) (size - 1)));
		buffer_class = exponent - HTTP_RECYCLE_BUFFER_MIN_EXPONENT + 1;
	}

	return buffer_class;

}

// returns the class whose size is exactly the capacity, 0 if there is none
static unsigned int http_recycle_buffer_exact_class (const size_t capacity) {

	const unsigned int buffer_class = http_recycle_buffer_class (capacity);

	return (buffer_class && (http_recycle_buffer_class_size (buffer_class) == capacity)) ? buffer_class : 0;

}

// the elements of the cache go to the shared lists, or are released when they are full
static void http_recycle_cache_flush (HttpRecycleThreadCache *cache) {

	for (unsigned int index = 0; index < cache->n_responses; index++) {
		if (freelist_push (&http_recycled_responses, cache->responses[index])) free (cache->responses[index]);
	}

	cache->n_responses = 0;

	for (unsigned int buffer_class = 0; buffer_class < HTTP_RECYCLE_BUFFER_CLASSES; buffer_class++) {
		for (unsigned int index = 0; index < cache->n_buffers[buffer_class]; index++) {
			void *buffer = cache->buffers[buffer_class][index];
			if (freelist_push (&http_recycled_buffers[buffer_class], buffer)) free (buffer);
		}

		cache->n_buffers[buffer_class] = 0;
	}

}

// called when a thread that used responses or buffers ends
static void http_recycle_cache_delete (void *cache_ptr) {

	HttpRecycleThreadCache *cache = (HttpRecycleThreadCache *) cache_ptr;

	http_recycle_thread_cache = NULL;

	http_recycle_cache_flush (cache);

	free (cache);

}

static void http_recycle_cache_key_create (void) {

	http_recycle_cache_key_created = !pthread_key_create (&http_recycle_cache_key, http_recycle_cache_delete);

}

// the first object of a thread creates its cache,
// with the key that releases it when the thread ends
static HttpRecycleThreadCache *http_recycle_cache_create (void) {

	HttpRecycleThreadCache *cache = NULL;

	(void) pthread_once (&http_recycle_cache_once, http_recycle_cache_key_create);

	if (http_recycle_cache_key_created) {
		cache = (HttpRecycleThreadCache *) calloc (1, sizeof (HttpRecycleThreadCache));
		if (cache && pthread_setspecific (http_recycle_cache_key, cache)) {
			free (cache);
			cache = NULL;
		}

		http_recycle_thread_cache = cache;
	}

	return cache;

}

// returns the cache of the thread, NULL if nothing is recycled
static inline HttpRecycleThreadCache *http_recycle_cache_get (void) {

	HttpRecycleThreadCache *cache = http_recycle_thread_cache;

	if (!cache && (http_recycle_thread_cache_limit > 0)) {
		cache = http_recycle_cache_create ();
	}

	return cache;

}

HttpResponse *http_recycle_response_take (void) {

	HttpResponse *response = NULL;

	HttpRecycleThreadCache *cache = http_recycle_cache_get ();
	if (cache && cache->n_responses) {
		cache->n_responses -= 1;
		response = cache->responses[cache->n_responses];
	}

	if (!response) response = (HttpResponse *) freelist_pop (&http_recycled_responses);

	return response;

}

void http_recycle_response_give_back (HttpResponse *response) {

	HttpRecycleThreadCache *cache = http_recycle_cache_get ();
	if (cache && (cache->n_responses < http_recycle_thread_cache_limit)) {
		cache->responses[cache->n_responses] = response;
		cache->n_responses += 1;
	}

	else if (freelist_push (&http_recycled_responses, response)) {
		free (response);
	}

}

void *http_recycle_buffer_take (const size_t size, size_t *capacity) {

	void *buffer = NULL;

	const unsigned int buffer_class = http_recycle_buffer_class (size);
	if (buffer_class) {
		const unsigned int index = buffer_class - 1;

		HttpRecycleThreadCache *cache = http_recycle_cache_get ();
		if (cache && cache->n_buffers[index]) {
			cache->n_buffers[index] -= 1;
			buffer = cache->buffers[index][cache->n_buffers[index]];
		}

		if (!buffer) buffer = freelist_pop (&http_recycled_buffers[index]);

		*capacity = http_recycle_buffer_class_size (buffer_class);
		if (!buffer) buffer = malloc (*capacity);
	}

	else {
		*capacity = size;
		buffer = malloc (size);
	}

	if (!buffer) *capacity = 0;

	return buffer;

}

void http_recycle_buffer_give_back (void *buffer, const size_t capacity) {

	const unsigned int buffer_class = http_recycle_buffer_exact_class (capacity);

	if (buffer && buffer_class) {
		const unsigned int index = buffer_class - 1;

		HttpRecycleThreadCache *cache = http_recycle_cache_get ();
		if (cache && (cache->n_buffers[index] < http_recycle_thread_cache_limit)) {
			cache->buffers[index][cache->n_buffers[index]] = buffer;
			cache->n_buffers[index] += 1;
		}

		else if (freelist_push (&http_recycled_buffers[index], buffer)) {
			free (buffer);
		}
	}

	else {
		free (buffer);
	}

}

// the cache of the thread that unloads the library, and the shared lists
__attribute__ ((destructor)) static void http_recycled_drain (void) {

	if (http_recycle_cache_key_created && http_recycle_thread_cache) {
		(void) pthread_setspecific (http_recycle_cache_key, NULL);
		http_recycle_cache_delete (http_recycle_thread_cache);
	}

	freelist_drain (&http_recycled_responses, free);

	for (unsigned int buffer_class = 0; buffer_class < HTTP_RECYCLE_BUFFER_CLASSES; buffer_class++) {
		freelist_drain (&http_recycled_buffers[buffer_class], free);
	}

}

#pragma endregion
