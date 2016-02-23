/* Returns a new logical CBN. 
   id=-1, peer_info is allocated, {orig,true}_client_id=ID_UNKNOWN. 
   All other fields are NULL or 0. */
lcbn_t * lcbn_create (void)
{
  lcbn_t *lcbn;

  lcbn = malloc(sizeof *lcbn);
  assert(lcbn != NULL);
  memset(lcbn, 0, sizeof *lcbn);

  lcbn->tree_number = -1;
  lcbn->tree_level = -1;
  lcbn->level_entry = -1;

  lcbn->global_id = -1;
  lcbn->active = 0;
  lcbn->finished = 0;

  list_init(&lcbn->children);

  assert(lcbn != NULL);
  return lcbn;
}

/* Create and initialize a new lcbn based on PARENT. */
lcbn_t * lcbn_init (lcbn_t *parent)
{
  lcbn_t *lcbn;

  lcbn = lcbn_create();
  if (parent)
  {
    lcbn->tree_number = parent->tree_number;
    lcbn->tree_level = parent->tree_level + 1;

    list_lock(&parent->children);
    lcbn->level_entry = list_size(&parent->children);
    list_push_back(&parent->children, &lcbn->child_elem);
    list_unlock(&parent->children);
    lcbn->parent = parent;
  }
  else
  {
    lcbn->tree_level = 0;
    lcbn->level_entry = 0;
  }

  assert(lcbn != NULL);
  return lcbn;
}

/* Destroy LCBN returned by lcbn_create or lcbn_init. */
void lcbn_destroy (lcbn_t *lcbn)
{
  if (lcbn == NULL)
    return;

  list_destroy(&lcbn->children);
  free(lcbn);
}
