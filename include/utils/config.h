#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#include "utils/list.h"

#define CONFIG_MAX_LINE_LEN     128

typedef struct ConfigKeyValuePair {

    char *key;
    char *value;

} ConfigKeyValuePair;

typedef struct ConfigEntity {

    char *name;
    List *keyValuePairs;

} ConfigEntity;

typedef struct Config {

    List *entities;

} Config;


extern Config *parseConfigFile (const char *filename);
extern char *getEntityValue (ConfigEntity *entity, char *key);
extern ConfigEntity *getEntityWithId (Config *cfg, uint16_t id);

extern void setEntityValue (ConfigEntity *entity, char *key, char *value);
extern void writeConfigFile (const char *filename, Config *config);

extern void clearConfig (Config *);


#endif