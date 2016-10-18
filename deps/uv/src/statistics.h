#ifndef UV_SRC_STATISTICS_H_
#define UV_SRC_STATISTICS_H_

/* This module offers libuv components to collect statistics about their workload. 
 *
 * From a fuzzing perspective, the goal is to understand whether a choice of fuzzing parameters
 * might influence the outcome of the program.
 * Obviously this can't be known perfectly, since all of the callbacks we handle are black boxes,
 * but if a program has only two threadpool events then varying the TP scheduler parameters is probably
 * not a good route for investigation.
 */

/* For cases where we can change the order of events, we track how many items we have ready at once. 
 * Otherwise, we just track nRegistered and/or nExecuted.
 */
enum statistic_s
{
  STATISTIC_MIN,
  STATISTIC_TIMERS_REGISTERED = STATISTIC_MIN,
  STATISTIC_TIMERS_EXECUTED,

  STATISTIC_EPOLL_SIMULTANEOUS_EVENTS,
  STATISTIC_EPOLL_EVENTS_EXECUTED,

  STATISTIC_TP_SIMULTANEOUS_WORK,
  STATISTIC_TP_WORK_EXECUTED,

  STATISTIC_CLOSING_EXECUTED,

  STATISTIC_CB_EXECUTED,

  STATISTIC_MAX = STATISTIC_CB_EXECUTED
};
typedef enum statistic_s statistic_t;

/* Initialize the statistics module. 
 * Call once. Not thread safe. */
void statistics_init (void);

/* Record a statistic. 
 * Thread safe.
 */
void statistics_record (statistic_t stat, int value);

/* Dump all of the statistics we've recorded to stdout. 
 * Suitable for use with atexit, and registered as such by statistics_init.
 */
void statistics_dump (void);

#endif  /* UV_SRC_STATISTICS_H_ */
