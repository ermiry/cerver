/*** This file is used to read .cfg files to retrive data for our items, monsters, etc. 
 * 
 * As of 15/08/2018 -- 23:02 -- we will be reading from hardcoded internal files to retriev some
 * information of our various entities. Maybe later we will want to have a more advanced system
 * and even have the server with this type of data.
 * 
 * We also have to think of a better way for adding new stuff in our game in an easier way.
 * We will also want to have more values to tweak by config files.
 * 
 * ***/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"

#include "utils/list.h"
#include "utils/config.h"


/*** PARSE THE FILE ***/

ConfigEntity *newEntity (char *buffer, Config *cfg) {

    // so grab the name
    char *name = strtok (buffer, "[]");

    ConfigEntity *entity = (ConfigEntity *) malloc (sizeof (ConfigEntity));
    char *copy = (char *) calloc (strlen (name) + 1, sizeof (char));
    strcpy (copy, name);
    entity->name = copy;
    entity->keyValuePairs = initList (free);
    insertAfter (cfg->entities, LIST_END (cfg->entities), entity);
    
    return entity;

}

void getData (char *buffer, ConfigEntity *currentEntity) {

    buffer[CONFIG_MAX_LINE_LEN - 1] = '\0';
    char *key = strtok (buffer, "=");
    char *value = strtok (NULL, "\n");
    if ((key != NULL) && (value != NULL)) {
        ConfigKeyValuePair *kvp = (ConfigKeyValuePair *) malloc (sizeof (ConfigKeyValuePair));
        char *copyKey = (char *) calloc (strlen (key) + 1, sizeof (char));
        strcpy (copyKey, key);
        kvp->key = copyKey;
        char *copyValue = (char *) calloc (strlen (value) + 1, sizeof (char));
        strcpy (copyValue, value);
        kvp->value = copyValue;

        insertAfter (currentEntity->keyValuePairs, LIST_END (currentEntity->keyValuePairs), kvp);
    }

}

// Parse the given file to memory
Config *parseConfigFile (const char *filename) {

    if (filename == NULL) return NULL;

    Config *cfg = NULL;

    FILE *configFile = fopen (filename, "r");
    if (configFile == NULL) return NULL;

    cfg = (Config *) malloc (sizeof (Config));
    cfg->entities = initList (free);

    char buffer[CONFIG_MAX_LINE_LEN];

    ConfigEntity *currentEntity = NULL;

    while (fgets (buffer, CONFIG_MAX_LINE_LEN, configFile) != NULL) {
        // we have a new entity
        if (buffer[0] == '[') currentEntity = newEntity (buffer, cfg);

        // we have a key/value data, so parse it into its various parts 
        else if ((buffer[0] != '\n') && (buffer[0] != ' ')) 
            getData (buffer, currentEntity);
        
    }

    return cfg;

}


/*** RETURN DATA ***/

// get a value for a given key in an entity
char *getEntityValue (ConfigEntity *entity, char *key) {

    ConfigKeyValuePair *kvp = NULL;
    for (ListElement *e = LIST_START (entity->keyValuePairs); e != NULL; e = e->next) {
        kvp = (ConfigKeyValuePair *) e->data;
        if (strcmp (key, kvp->key) == 0) return kvp->value;
    }

    return NULL;

}

ConfigEntity *getEntityWithId (Config *cfg, u8 id) {

    ConfigEntity *entity = NULL;
    for (ListElement *e = LIST_START (cfg->entities); e != NULL; e = e->next) {
        entity = (ConfigEntity *) e->data;
        u8 eId = atoi (getEntityValue (entity, "id"));
        if (eId == id) return entity;
    }

    return NULL;

}

/*** Add DATA ***/

// add a new key-value pair to the entity
void setEntityValue (ConfigEntity *entity, char *key, char *value) {

    if (entity->keyValuePairs == NULL) entity->keyValuePairs = initList (free);

    ConfigKeyValuePair *kv = (ConfigKeyValuePair *) malloc (sizeof (ConfigKeyValuePair));
    kv->key = strdup (key);
    kv->value = strdup (value);

    insertAfter (entity->keyValuePairs, LIST_END (entity->keyValuePairs), kv);

}

void writeConfigFile (const char *filename, Config *config) {

    FILE *configFile = fopen (filename, "w+");
    if (configFile == NULL) return;

    for (ListElement *e = LIST_START (config->entities); e != NULL; e = e->next) {
        ConfigEntity *entity = (ConfigEntity *) e->data;
        fprintf (configFile, "[%s]\n", entity->name);

        for (ListElement *le = LIST_START (entity->keyValuePairs); le != NULL; le = le->next) {
            ConfigKeyValuePair *kv = (ConfigKeyValuePair *) le->data;
            fprintf (configFile, "%s=%s\n", kv->key, kv->value);
        }
    } 

    fclose (configFile);

}

/*** CLEAN UP ***/

void clearConfig (Config *cfg) {

    if (cfg != NULL) {
        // clear each entity values
        for (ListElement *e = LIST_START (cfg->entities); e != NULL; e = e->next) 
            destroyList (((ConfigEntity *) e->data)->keyValuePairs);
        
        destroyList (cfg->entities);
        free (cfg);
    }

}