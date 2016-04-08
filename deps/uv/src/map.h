#ifndef UV_SRC_MAP_H_
#define UV_SRC_MAP_H_

/* Key-value store.
   You can store void *values associated with int keys.
   Attempting to store a key already associated with a value will overwrite the previous value.

   All map APIs are internally thread-safe. 
   If you wish a higher-level locking mechanism (e.g. iterating with potential parallel modifications), 
     use map_lock and map_unlock. */

struct map;

struct map * map_create (void);
void map_destroy (struct map *map);
unsigned map_size (struct map *map);
int map_empty (struct map *map);
int map_looks_valid (struct map *map);

void map_insert (struct map *map, int key, void *value);
void * map_lookup (struct map *map, int key, int *found);
void * map_remove (struct map *map, int key, int *found);

/* For higher-level locking discipline. */
void map_lock (struct map *map);
void map_unlock (struct map *map);

/* If you wish to construct a hash table. Hash BUF of LEN. */
unsigned map_hash (void *buf, unsigned len); 

/* Tests all of the map APIs. */
void map_UT (void);

#endif  /* UV_SRC_MAP_H_ */
