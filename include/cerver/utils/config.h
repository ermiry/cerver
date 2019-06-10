#ifndef _CERVER_CONFIG_H_
#define _CERVER_CONFIG_H_

#include <stdint.h>

#include "cerver/collections/dllist.h"

#define CONFIG_MAX_LINE_LEN     128

typedef struct ConfigKeyValuePair {

    char *key;
    char *value;

} ConfigKeyValuePair;

typedef struct ConfigEntity {

    char *name;
    DoubleList *keyValuePairs;

} ConfigEntity;

typedef struct Config {

    DoubleList *entities;

} Config;

// parse the given file to memory
extern Config *config_parse_file (const char *filename);
// get a value for a given key in an entity
extern char *config_get_entity_value (ConfigEntity *entity, const char *key);
// get the config entity associated with an id
extern ConfigEntity *config_get_entity_with_id (Config *cfg, uint32_t id);

// add a new key-value pair to the entity
extern void config_set_entity_value (ConfigEntity *entity, const char *key, const char *value);
// create a file with config values
extern void config_write_file (const char *filename, Config *config);

// destroy a config structure
extern void config_destroy (Config *cfg);

#endif