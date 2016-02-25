#include "logical-callback-node.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* Returns a new logical CBN. 
   id=-1, peer_info is allocated, {orig,true}_client_id=ID_UNKNOWN. 
   All other fields are NULL or 0. */
lcbn_t * lcbn_create (void *context, void *cb, enum callback_type cb_type)
{
  lcbn_t *lcbn;

  lcbn = malloc(sizeof *lcbn);
  assert(lcbn != NULL);
  memset(lcbn, 0, sizeof *lcbn);

  lcbn->context = context;
  lcbn->cb = cb;
  lcbn->cb_type = cb_type;

  lcbn->tree_number = -1;
  lcbn->tree_level = -1;
  lcbn->level_entry = -1;

  lcbn->global_id = -1;

  lcbn->registrar = NULL;
  lcbn->tree_parent = NULL;
  list_init(&lcbn->children);

  lcbn->active = 0;
  lcbn->finished = 0;

  return lcbn;
}

/* Initialize CHILD as a child of PARENT. */
lcbn_t * lcbn_add_child (lcbn_t *parent, lcbn_t *child)
{
  assert(parent != NULL);
  assert(child != NULL);

  child->tree_number = parent->tree_number;
  child->tree_level = parent->tree_level + 1;
  child->tree_parent = parent;

  list_lock(&parent->children);
  child->level_entry = list_size(&parent->children);
  list_push_back(&parent->children, &child->child_elem);
  list_unlock(&parent->children);
}

/* Destroy LCBN returned by lcbn_create or lcbn_init. */
void lcbn_destroy (lcbn_t *lcbn)
{
  if (lcbn == NULL)
    return;

  list_destroy(&lcbn->children);
  free(lcbn);
}

/* Mark LCBN as active and update its start field. */
void lcbn_mark_begin (lcbn_t *lcbn)
{
  assert(lcbn != NULL);
  lcbn->active = 1;
  assert(clock_gettime(CLOCK_MONOTONIC, &lcbn->start) == 0);
}

/* Mark LCBN as finished and update its end field. */
void lcbn_mark_end (lcbn_t *lcbn)
{
  assert(lcbn != NULL);
  lcbn->active = 0;
  lcbn->finished = 1;
  assert(clock_gettime(CLOCK_MONOTONIC, &lcbn->end) == 0);
}
