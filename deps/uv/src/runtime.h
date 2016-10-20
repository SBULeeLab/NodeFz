#ifndef UV_SRC_RUNTIME_H_
#define UV_SRC_RUNTIME_H_

/* Runtime parameters. 
 * We check env vars during initialization and then offer
 *   APIs so everyone else can get the value later.
 */

/* Query all the env vars and store the results. */
void runtime_init (void);

/* Returns non-zero if we should be silent, otherwise 0. */
int runtime_should_be_silent (void);

/* Returns non-zero if we should print summary information, otherwise 0. */
int runtime_should_print_summary (void);

#endif  /* UV_SRC_RUNTIME_H_ */
