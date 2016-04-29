#include "map.h"
#include "list.h"
#include "mylog.h"

#include "uv-common.h" /* Allocators */

#include <assert.h>
#include <stddef.h> /* NULL */
#include <stdlib.h> /* malloc */
#include <string.h> /* memset */

#define MAP_MAGIC 11223344
#define MAP_ELEM_MAGIC 11223355

struct map
{
  int magic;

  unsigned table_size;
  struct list **table;

  pthread_mutex_t lock; /* For external locking via map_lock and map_unlock. */
  pthread_mutex_t _lock; /* Don't touch this. For internal locking via map__lock and map__unlock. Recursive. */
};

struct map_elem
{
  int magic;
  int key;
  void *value;

  struct list_elem elem;
};

static void map__lock (struct map *map);
static void map__unlock (struct map *map);

static int map_elem_looks_valid (struct map_elem *me)
{
  int valid = 1;

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_elem_looks_valid: begin: map_elem %p\n", me));
  if (!me)
  {
    valid = 0;
    goto DONE;
  }

  if (me->magic != MAP_ELEM_MAGIC)
  {
    valid = 0;
    goto DONE;
  }

  valid = 1;
  DONE:
    ENTRY_EXIT_LOG((LOG_MAP, 9, "map_elem_looks_valid: returning valid %i\n", valid));
    return valid;
}

static struct map_elem * map_elem_create (int key, void *value)
{
  struct map_elem *new_map_elem = NULL;
  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_elem_create: begin: key %i value %p\n", key, value));

  new_map_elem = (struct map_elem *) uv__malloc(sizeof *new_map_elem); 
  assert(new_map_elem);
  memset(new_map_elem, 0, sizeof(*new_map_elem));

  new_map_elem->magic = MAP_ELEM_MAGIC;
  new_map_elem->key = key;
  new_map_elem->value = value;

  assert(map_elem_looks_valid(new_map_elem));
  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_elem_create: returning new_map_elem %p\n", new_map_elem));
  return new_map_elem;
}

static void map_elem_destroy (struct map_elem *me)
{
  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_elem_destroy: begin: me %p\n", me));

  assert(map_elem_looks_valid(me));
#if JD_DEBUG
  memset(me, 'a', sizeof *me);
#endif
  uv__free(me);

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_elem_destroy: returning\n"));
}


struct map * map_create (void)
{
  struct map *new_map = NULL;
  unsigned i;
  pthread_mutexattr_t attr;

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_create: begin\n"));
  
  new_map = (struct map *) uv__malloc(sizeof *new_map);
  assert(new_map);
  new_map->magic = MAP_MAGIC;

  new_map->table_size = 128;
  new_map->table = (struct list **) uv__malloc(new_map->table_size*sizeof(struct list *));
  assert(new_map->table);
  for (i = 0; i < new_map->table_size; i++)
    new_map->table[i] = list_create();

  pthread_mutex_init(&new_map->lock, NULL);

  /* Recursive internal lock. */
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&new_map->_lock, &attr);
  pthread_mutexattr_destroy(&attr);

  assert(map_looks_valid(new_map));
  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_create: returning new_map %p\n", new_map));
  return new_map;
}

void map_destroy (struct map *map)
{
  struct list_elem *le = NULL;
  struct map_elem *me = NULL;
  unsigned i;

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_destroy: begin: map %p\n", map));
  assert(map_looks_valid(map));

  map__lock(map);
  for (i = 0; i < map->table_size; i++)
  {
    while (!list_empty(map->table[i]))
    {
      le = list_pop_front(map->table[i]);
      assert(le != NULL);

      me = list_entry(le, struct map_elem, elem); 
      assert(map_elem_looks_valid(me));
      map_elem_destroy(me);

      le = NULL;
      me = NULL;
    }

    list_destroy(map->table[i]);
    map->table[i] = NULL;
  }

#if JD_DEBUG
  memset(map->table, 'a', map->table_size*sizeof(struct list *));
#endif
  uv__free(map->table);
  map->table = NULL;

  map__unlock(map);

  pthread_mutex_destroy(&map->lock);
  pthread_mutex_destroy(&map->_lock);

#if JD_DEBUG
  memset(map, 'a', sizeof *map);
#endif
  uv__free(map);

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_destroy: returning\n"));
}

unsigned map_size (struct map *map)
{
  int size = 0;
  unsigned i;

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_size: begin: map %p\n", map));
  assert(map_looks_valid(map));

  map__lock(map);
  size = 0;
  for (i = 0; i < map->table_size; i++)
    size += list_size(map->table[i]);
  map__unlock(map);

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_size: returning size %u\n", size));
  return size;
}

int map_empty (struct map *map)
{
  int empty = 0;
  unsigned i;

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_empty: begin: map %p\n", map));
  assert(map_looks_valid(map));

  map__lock(map);
  empty = 1;
  for (i = 0; i < map->table_size; i++)
  {
    if (!list_empty(map->table[i]))
    {
      empty = 0;
      break;
    }
  }
  map__unlock(map);

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_empty: returning empty %i\n", empty));
  return empty;
}

int map_looks_valid (struct map *map)
{
  int is_valid = 0;
  unsigned i;

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_looks_valid: begin: map %p\n", map));

  if (!map)
  {
    is_valid = 0;
    goto DONE;
  }
  if (map->magic != MAP_MAGIC)
  {
    is_valid = 0;
    goto DONE;
  }

  is_valid = 1;
  if (map->table)
  {
    for (i = 0; i < map->table_size; i++)
    {
      if (!list_looks_valid(map->table[i]))
      {
        is_valid = 0;
        break;
      }
    }
  }

  DONE:
    ENTRY_EXIT_LOG((LOG_MAP, 9, "map_looks_valid: returning is_valid %i\n", is_valid));
    return is_valid;
}

/* Add an element with <KEY, VALUE> to MAP. */
void map_insert (struct map *map, int key, void *value)
{
  struct list_elem *le = NULL;
  struct map_elem *me = NULL, *new_me = NULL;
  int in_map = 0;
  struct list *elem_list = NULL;

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_insert: begin: map %p key %i value %p\n", map, key, value));
  assert(map_looks_valid(map));

  elem_list = map->table[key % map->table_size];

  map__lock(map);
  /* If key is in the map already, update value. */
  in_map = 0;
  for (le = list_begin(elem_list); le != list_end(elem_list); le = list_next(le))
  {
    assert(le);
    me = list_entry(le, struct map_elem, elem); 
    assert(map_elem_looks_valid(me));

    if (me->key == key)
    {
      mylog(LOG_MAP, 8, "map_insert: key %i was in the map already with value %p, changing value to %p\n", key, me->value, value);
      me->value = value;
      in_map = 1;
      break;
    }
  } 

  if (!in_map)
  {
    mylog(LOG_MAP, 8, "map_insert: key %i was not in the map already\n", key);
    /* This key is not yet in the map. Allocate a new map_elem and insert it (at the front for improved locality on subsequent access). */
    new_me = map_elem_create(key, value);
    list_push_front(elem_list, &new_me->elem);
    in_map = 1;
  }

  map__unlock(map);

  assert(in_map);
  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_insert: returning\n"));
}

/* Look up KEY in MAP.
   If KEY is found, returns the associated VALUE and sets FOUND to 1. 
   Else returns NULL and sets FOUND to 0. */
void * map_lookup (struct map *map, int key, int *found)
{
  struct list_elem *le = NULL;
  struct map_elem *me = NULL;
  void *ret = NULL;
  struct list *elem_list = NULL;

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_lookup: begin: map %p key %i found %p\n", map, key, found));
  assert(map_looks_valid(map));
  assert(found);

  ret = NULL;
  *found = 0;

  elem_list = map->table[key % map->table_size];

  map__lock(map);
  for (le = list_begin(elem_list); le != list_end(elem_list); le = list_next(le))
  {
    assert(le);
    me = list_entry(le, struct map_elem, elem); 
    assert(map_elem_looks_valid(me));

    if (me->key == key)
    {
      mylog(LOG_MAP, 8, "map_lookup: Found it: me %p has key %i (value %p)\n", me, key, me->value);
      ret = me->value;
      *found = 1;
      break;
    }
  }

  map__unlock(map);

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_lookup: returning ret %p *found %i\n", ret, *found));
  return ret;
}

/* Remove KEY from MAP.
   If KEY is found, returns the associated VALUE and sets FOUND to 1. 
   Else returns NULL and sets FOUND to 0. */
void * map_remove (struct map *map, int key, int *found)
{
  struct list_elem *le = NULL;
  struct map_elem *me = NULL;
  void *ret = NULL;
  struct list *elem_list = NULL;

  assert(map_looks_valid(map));
  assert(found);

  ret = NULL;
  *found = 0;
  elem_list = map->table[key % map->table_size];

  map__lock(map);
  for (le = list_begin(elem_list); le != list_end(elem_list); le = list_next(le))
  {
    assert(le != NULL);
    me = list_entry(le, struct map_elem, elem); 
    assert(map_elem_looks_valid(me));

    if (me->key == key)
    {
      mylog(LOG_MAP, 8, "map_remove: Found it: me %p key %i value %p\n", me, key, me->value);
      ret = me->value;
      *found = 1;
      list_remove(elem_list, le);
      map_elem_destroy(me);
      break;
    }
  }

  map__unlock(map);
  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_remove: returning ret %p *found %i\n", ret, *found));
  return ret;
}

/* For external locking. */
void map_lock (struct map *map)
{
  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_lock: begin: map %p\n", map));
  assert(map_looks_valid(map));

  pthread_mutex_lock(&map->lock);

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_lock: returning\n"));
}

/* For external locking. */
void map_unlock (struct map *map)
{
  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_unlock: begin: map %p\n", map));
  assert(map_looks_valid(map));

  pthread_mutex_unlock(&map->lock);

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_unlock: returning\n"));
}

/* For internal locking. */
static void map__lock (struct map *map)
{
  ENTRY_EXIT_LOG((LOG_MAP, 9, "map__lock: begin: map %p\n", map));
  assert(map_looks_valid(map));

  pthread_mutex_lock(&map->_lock);

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map__lock: returning\n"));
}

/* For internal locking. */
static void map__unlock (struct map *map)
{
  ENTRY_EXIT_LOG((LOG_MAP, 9, "map__unlock: begin: map %p\n", map));
  assert(map_looks_valid(map));

  pthread_mutex_unlock(&map->_lock);

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map__unlock: returning\n"));
}

/* Unit test for the map class. */
void map_UT (void)
{
  struct map *m;
  int v1, v2, v3, found;
  unsigned i = 0, big_map_size = 400;

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

  /* Put big_map_size elements into the map. Repeat. Size should remain big_map_size. */
  for(i = 0; i < big_map_size; i++)
    map_insert(m, i, &v1);
  assert(map_size(m) == big_map_size);
  assert(map_lookup(m, 99, &found) == &v1);
  assert(found == 1);

  for(i = 0; i < big_map_size; i++)
    map_insert(m, i, &v2);
  assert(map_size(m) == big_map_size);
  assert(map_lookup(m, 99, &found) == &v2);
  assert(found == 1);

  /* Lock and unlock. */
  map_lock(m);
  map_unlock(m);

  map_destroy(m);
}

/* Utility routines. */
/* Hash BUF of LEN bytes. */
unsigned map_hash (void *buf, unsigned len)
{ 
  unsigned i = 0, hash = 0;
  char *bufc = NULL;

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_hash: begin: buf %p len %u\n", buf, len));

  bufc = buf;
  /* Source: http://stackoverflow.com/questions/7627723/how-to-create-a-md5-hash-of-a-string-in-c */
  hash = 0;
  for (i = 0; i < len; i++)
    hash = bufc[i] + (hash << 6) + (hash << 16) - hash;

  ENTRY_EXIT_LOG((LOG_MAP, 9, "map_hash: returning hash %u\n", hash));
  return hash;
}
