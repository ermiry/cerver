#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "collections/dllist.h"
#include "utils/config.h"

static void ConfigKeyValuePair_destroy (void *ptr) {

    if (ptr) {
        ConfigKeyValuePair *ckvp = (ConfigKeyValuePair *) ptr;
        if (ckvp->key) free (ckvp->key);
        if (ckvp->value) free (ckvp->value);

        free (ckvp);
    }

}

static void ConfigEntity_destroy (void *ptr) {

    if (ptr) {
        ConfigEntity *ce = (ConfigEntity *) ptr;
        if (ce->name) free (ce->name);
        dlist_destroy (ce->keyValuePairs);
        free (ptr);
    }

}

static ConfigEntity *config_new_entity (char *buffer, Config *cfg) {

    // so grab the name
    char *name = strtok (buffer, "[]");

    ConfigEntity *entity = (ConfigEntity *) malloc (sizeof (ConfigEntity));
    char *copy = (char *) calloc (strlen (name) + 1, sizeof (char));
    strcpy (copy, name);
    entity->name = copy;
    entity->keyValuePairs = dlist_init (ConfigKeyValuePair_destroy, NULL);
    dlist_insert_after (cfg->entities, dlist_end (cfg->entities), entity);

    return entity;

}

static void config_get_data (char *buffer, ConfigEntity *currentEntity) {

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

        dlist_insert_after (currentEntity->keyValuePairs, dlist_end (currentEntity->keyValuePairs), kvp);
    }

}

// parse the given file to memory
Config *config_parse_file (const char *filename) {

    Config *cfg = NULL;

    if (filename) {
        FILE *configFile = fopen (filename, "r");
        if (configFile == NULL) return NULL;

        cfg = (Config *) malloc (sizeof (Config));
        cfg->entities = dlist_init (ConfigEntity_destroy, NULL);

        char buffer[CONFIG_MAX_LINE_LEN];

        ConfigEntity *currentEntity = NULL;

        while (fgets (buffer, CONFIG_MAX_LINE_LEN, configFile) != NULL) {
            // we have a new entity
            if (buffer[0] == '[') currentEntity = config_new_entity (buffer, cfg);

                // we have a key/value data, so parse it into its various parts
            else if ((buffer[0] != '\n') && (buffer[0] != ' '))
                config_get_data (buffer, currentEntity);
        }
    }

    return cfg;

}

/*** RETURN DATA ***/

// get a value for a given key in an entity
// returns a newly allocated string with the key value
char *config_get_entity_value (ConfigEntity *entity, const char *key) {

    char *retval = NULL;
    for (ListElement *e = dlist_start (entity->keyValuePairs); e != NULL; e = e->next) {
        ConfigKeyValuePair *kvp = (ConfigKeyValuePair *) e->data;
        if (!strcmp (key, kvp->key)) {
            retval = (char *) calloc (strlen (kvp->value), sizeof (char));
            strcpy (retval, kvp->value);
            return retval;
        }
    }

    return retval;

}

// get the config entity associated with an id
ConfigEntity *config_get_entity_with_id (Config *cfg, uint32_t id) {

    ConfigEntity *entity = NULL;
    for (ListElement *e = dlist_start (cfg->entities); e != NULL; e = e->next) {
        entity = (ConfigEntity *) e->data;
        uint32_t eId = atoi (config_get_entity_value (entity, "id"));
        if (eId == id) return entity;
    }

    return NULL;

}

/*** Add DATA ***/

// add a new key-value pair to the entity
void config_set_entity_value (ConfigEntity *entity, const char *key, const char *value) {

    if (!entity->keyValuePairs) entity->keyValuePairs = dlist_init (ConfigKeyValuePair_destroy, NULL);

    ConfigKeyValuePair *kv = (ConfigKeyValuePair *) malloc (sizeof (ConfigKeyValuePair));
    kv->key = strdup (key);
    kv->value = strdup (value);

    dlist_insert_after (entity->keyValuePairs, dlist_end (entity->keyValuePairs), kv);

}

// create a file with config values
void config_write_file (const char *filename, Config *config) {

    FILE *configFile = fopen (filename, "w+");
    if (configFile == NULL) return;

    for (ListElement *e = dlist_start (config->entities); e != NULL; e = e->next) {
        ConfigEntity *entity = (ConfigEntity *) e->data;
        fprintf (configFile, "[%s]\n", entity->name);

        for (ListElement *le = dlist_start (entity->keyValuePairs); le != NULL; le = le->next) {
            ConfigKeyValuePair *kv = (ConfigKeyValuePair *) le->data;
            fprintf (configFile, "%s=%s\n", kv->key, kv->value);
        }
    }

    fclose (configFile);

}

// destroy a config structure
void config_destroy (Config *cfg) {

    if (cfg) {
        dlist_destroy (cfg->entities);

        free (cfg);
    }

}