#include "map.h"
#include "list.h"

#include <assert.h>
#include <stddef.h> /* NULL */
#include <stdlib.h> /* malloc */

#define MAP_MAGIC 11223344

struct map
{
  int magic;
  pthread_mutex_t lock; /* For external locking via map_lock and map_unlock. */
  pthread_mutex_t _lock; /* Don't touch this. For internal locking via map__lock and map__unlock. Recursive. */
  struct list list;
};

struct map_elem
{
  int key;
  void *value;

  struct list_elem elem;
};

static void map__lock (struct map *map);
static void map__unlock (struct map *map);

struct map * map_create (void)
{
  struct map *new_map;
  pthread_mutexattr_t attr;
  
  new_map = (struct map *) malloc (sizeof *new_map);
  assert (new_map != NULL);
  new_map->magic = MAP_MAGIC;
  list_init (&new_map->list);

  pthread_mutex_init (&new_map->lock, NULL);

  /* Recursive internal lock. */
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init (&new_map->_lock, &attr);

  return new_map;
}

void map_destroy (struct map *map)
{
  struct list_elem *le;
  struct map_elem *me;

  assert(map != NULL);
  map__lock(map);
  while (!list_empty (&map->list))
  {
    le = list_pop_front (&map->list);
    assert(le != NULL);
    me = list_entry(le, struct map_elem, elem); 
    assert(me != NULL);
    free (me);
    le = NULL;
    me = NULL;
  }
  map__unlock(map);

  pthread_mutex_destroy(&map->lock);
  pthread_mutex_destroy(&map->_lock);
  free(map);
}

unsigned map_size (struct map *map)
{
  int size;

  assert(map != NULL);
  map__lock(map);

  assert(map_looks_valid (map));
  size = list_size (&map->list);

  map__unlock(map);
  return size;
}

int map_empty (struct map *map)
{
  int empty;
  assert(map != NULL);
  assert(map_looks_valid (map));

  map__lock(map);

  empty = (map_size (map) == 0);

  map__unlock(map);

  return empty;
}

int map_looks_valid (struct map *map)
{
  int is_valid; 

  map__lock(map);
  assert(map != NULL);

  is_valid = 1;
  /* Magic must be correct. */
  if (map->magic != MAP_MAGIC)
    is_valid = 0;
  if (!list_looks_valid (&map->list))
    is_valid = 0;

  map__unlock(map);

  return is_valid;
}

/* Add an element with <KEY, VALUE> to MAP. */
void map_insert (struct map *map, int key, void *value)
{
  struct list_elem *le;
  struct map_elem *me, *new_me;
  int in_map;

  assert(map != NULL);
  map__lock(map);
  in_map = 0;
  for (le = list_begin(&map->list); le != list_end(&map->list); le = list_next(le))
  {
    assert(le != NULL);
    me = list_entry(le, struct map_elem, elem); 
    assert(me != NULL);

    if (me->key == key)
    {
      /* This key is already in the map. Update its value. */
      me->value = value;
      in_map = 1;
      break;
    }
  } 

  if (!in_map)
  {
    /* This key is not yet in the map. Allocate a new map_elem and insert it (at the front for improved locality). */
    new_me = (struct map_elem *) malloc(sizeof *new_me); 
    assert(new_me != NULL);
    new_me->key = key;
    new_me->value = value;
    list_push_front(&map->list, &new_me->elem);
    in_map = 1;
  }

  map__unlock(map);
}

/* Look up KEY in MAP.
   If KEY is found, returns the associated VALUE and sets FOUND to 1. 
   Else returns NULL and sets FOUND to 0. */
void * map_lookup (struct map *map, int key, int *found)
{
  struct list_elem *le;
  struct map_elem *me;
  void *ret;

  assert(map != NULL);
  assert(found != NULL);

  map__lock(map);
  ret = NULL;
  *found = 0;
  for (le = list_begin(&map->list); le != list_end(&map->list); le = list_next(le))
  {
    assert(le != NULL);
    me = list_entry(le, struct map_elem, elem); 
    assert(me != NULL);

    if (me->key == key)
    {
      ret = me->value;
      *found = 1;
      break;
    }
  }

  map__unlock(map);
  return ret;
}

/* Remove KEY from MAP.
   If KEY is found, returns the associated VALUE and sets FOUND to 1. 
   Else returns NULL and sets FOUND to 0. */
void * map_remove (struct map *map, int key, int *found)
{
  struct list_elem *le;
  struct map_elem *me;
  void *ret;

  assert(map != NULL);
  assert(found != NULL);

  map__lock(map);
  ret = NULL;
  *found = 0;
  for (le = list_begin(&map->list); le != list_end(&map->list); le = list_next(le))
  {
    assert(le != NULL);
    me = list_entry(le, struct map_elem, elem); 
    assert(me != NULL);

    if (me->key == key)
    {
      ret = me->value;
      *found = 1;
      list_remove(&map->list, le);
      free(me);
      break;
    }
  }

  map__unlock(map);
  return ret;
}

/* For external locking. */
void map_lock (struct map *map)
{
  assert(map != NULL);
  pthread_mutex_lock(&map->lock);
}

/* For external locking. */
void map_unlock (struct map *map)
{
  assert(map != NULL);
  pthread_mutex_unlock (&map->lock);
}

/* For internal locking. */
static void map__lock (struct map *map)
{
  assert(map != NULL);
  pthread_mutex_lock(&map->_lock);
}

/* For internal locking. */
static void map__unlock (struct map *map)
{
  assert(map != NULL);
  pthread_mutex_unlock (&map->_lock);
}

/* Unit test for the map class. */
void map_UT (void)
{
  struct map *m;
  int v1, v2, v3, found;
  int i;

  v1 = 1;
  v2 = 2;
  v3 = 3;
  
  /* Create and destroy a map. */
  m = map_create();
  assert(map_looks_valid(m) == 1);
  map_destroy(m);

  /* Create and destroy a map. Insert, lookup, remove keys. */
  m = map_create();
  assert(map_looks_valid(m) == 1);
  assert(map_size(m) == 0);
  assert(map_empty(m) == 1);
  map_insert(m, 1, &v1);
  map_insert(m, 2, &v2);
  map_insert(m, 3, &v3);
  assert(map_looks_valid(m) == 1);
  assert(map_size(m) == 3);
  assert(map_empty(m) == 0);

  assert(map_lookup(m, 1, &found) == &v1);
  assert(found == 1);
  assert(map_lookup(m, 4, &found) == NULL);
  assert(found == 0);
  assert(map_lookup(m, 2, &found) == &v2);
  assert(found == 1);
  assert(map_lookup(m, 3, &found) == &v3);
  assert(found == 1);

  /* Empty the map. */
  assert(map_remove(m, 1, &found) == &v1);
  assert(found == 1);
  /* No such element. */
  assert(map_remove(m, 4, &found) == NULL);
  assert(found == 0);
  assert(map_size(m) == 2);
  assert(map_empty(m) == 0);

  assert(map_remove(m, 2, &found) == &v2);
  assert(found == 1);
  assert(map_size(m) == 1);

  assert(map_remove(m, 3, &found) == &v3);
  assert(found == 1);
  assert(map_size(m) == 0);
  assert(map_empty(m) == 1);

  /* Put 100 elements into the map. Repeat. Size should remain 100. */
  for(i = 0; i < 100; i++)
    map_insert(m, i, &v1);
  assert(map_size(m) == 100);
  assert(map_lookup(m, 99, &found) == &v1);
  assert(found == 1);

  for(i = 0; i < 100; i++)
    map_insert(m, i, &v2);
  assert(map_lookup(m, 99, &found) == &v2);
  assert(found == 1);

  map_lock(m);
  map_unlock(m);

  map_destroy(m);
}
