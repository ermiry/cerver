/**
 * @internal
 * @file cerver/http/core/pairs.h
 * @brief The key value pairs, and the pairs of the query of an url. A handler reads them as views, with the
 * methods of cerver/http/requests/request.h.
 */

#ifndef _CERVER_HTTP_CORE_PAIRS_H_
#define _CERVER_HTTP_CORE_PAIRS_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_pairs Pairs
 * @ingroup http_core
 * @brief The key value pairs of a query or of a form.
 * @{
 */

/**
 * @brief A key and its value.
 */
typedef struct KeyValuePair {

	/** @brief The key, NULL if the pair does not have one. */
	String *key;
	/** @brief The value, NULL if the pair does not have one. */
	String *value;

} KeyValuePair;

/**
 * @brief Allocates an empty pair.
 *
 * @return the new pair, NULL if it could not be allocated
 */
CERVER_PUBLIC KeyValuePair *key_value_pair_new (void);

/**
 * @brief Releases a pair, its key and its value.
 *
 * @param[in] kvp_ptr the pair, it can be NULL
 */
CERVER_PUBLIC void key_value_pair_delete (void *kvp_ptr);

/**
 * @brief Creates a pair with a copy of a key and of a value.
 *
 * @param[in] key the key, NULL for a pair without a key
 * @param[in] value the value, NULL for a pair without a value
 * @return the new pair, released with key_value_pair_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC KeyValuePair *key_value_pair_create (
	const char *key, const char *value
);

/**
 * @brief Returns the value of the first pair of a list with a key.
 *
 * @param[in] pairs the list of KeyValuePair
 * @param[in] key the key
 * @return the value, that belongs to the pair; NULL for a NULL argument, a missing key or a pair without a value
 */
CERVER_PUBLIC const String *key_value_pairs_get_value (
	const DoubleList *pairs, const char *key
);

/**
 * @brief Prints each pair of a list to stdout; a missing key or value is printed as an empty string.
 *
 * @param[in] pairs the list of KeyValuePair, NULL prints nothing
 */
CERVER_PUBLIC void key_value_pairs_print (
	const DoubleList *pairs
);

/**
 * @brief Parses the query of an url into a list of key value pairs.
 *
 * The pairs are split by '&' and their keys by the first '='; a piece without a '=' is not added.
 *
 * @param[in] first the first character of the query
 * @param[in] last the character after the last one of the query
 * @return the new list of KeyValuePair, released with dlist_delete (); NULL for a NULL argument
 */
CERVER_PUBLIC DoubleList *http_parse_query_into_pairs (
	const char *first, const char *last
);

/**
 * @brief Returns the value of a key of the pairs of a query, like key_value_pairs_get_value ().
 *
 * @param[in] pairs the list of KeyValuePair
 * @param[in] key the key
 * @return the value, that belongs to the pair; NULL if it is not found
 */
CERVER_PUBLIC const String *cerver_http_query_pairs_get_value (
	const DoubleList *pairs, const char *key
);

/**
 * @brief Prints the pairs of a query, like key_value_pairs_print ().
 *
 * @param[in] pairs the list of KeyValuePair
 */
CERVER_PUBLIC void cerver_http_query_pairs_print (
	const DoubleList *pairs
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
