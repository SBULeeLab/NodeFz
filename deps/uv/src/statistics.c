#include "statistics.h"

#include "uv.h"
#include "uv-common.h"
#include "mylog.h"
#include "runtime.h"

#include <stdlib.h> /* atexit */
#include <string.h> /* memset */
#include <stdio.h> /* printf */

/* To avoid too much overhead, we track the following for each statistic:
 *   - min observed
 *   - max observed
 *   - ongoing sum
 *   - total calls of that type
 *      > "ongoing sum" and "total calls" lets us compute an average over all calls without having to save each data point.
 */

/* Private declarations. */

struct statistic_record_s
{
  int any_observations; /* Set to 1 if any observations made on this statistic. */

  /* All values should be >= 0, so we can maximize range using unsigned long int. */
  unsigned long int min_observed;
  unsigned long int max_observed;

  /* For computing final average. 
   * This is only relevant for non-binary statistics (e.g. STATISTIC_EPOLL_SIMULTANEOUS_EVENTS).
   * For statistics like STATISTIC_TIMERS_REGISTERED, total == n_calls (average == 1).
   */ 
  unsigned long int total;
  unsigned long int n_calls;
};
typedef struct statistic_record_s statistic_record_t;

uv_mutex_t mutex;
statistic_record_t statistics_records[1 + STATISTIC_MAX - STATISTIC_MIN];

static int initialized = 0;

/* Private helpers. */
static void statistics__lock (void);
static void statistics__unlock (void);

static int statistic_valid (statistic_t stat);
static char * statistic_to_string (statistic_t statistic);

/* Public API implementatoin. */

void statistics_init (void)
{
  statistic_t i;
  if (initialized)
    return;

  assert(uv_mutex_init(&mutex) == 0);
  for (i = STATISTIC_MIN; i < 1 + STATISTIC_MAX - STATISTIC_MIN; i++)
  {
    memset(&statistics_records[i], 0, sizeof statistics_records[i]);
    statistics_records[i].any_observations = 0;
  }

  assert(atexit(statistics_dump) == 0);

  initialized = 1;
}

void statistics_record (statistic_t stat, int value)
{
  assert(statistic_valid(stat));
  assert(0 <= value); /* We use unsigned values in statistics counters. */

  mylog(LOG_STATISTICS, 1, "statistics_record: stat %s value %i\n", statistic_to_string(stat), value);

  statistics__lock();

  if (!statistics_records[stat].any_observations)
    statistics_records[stat].any_observations = 1;

  if ((unsigned long int) value < statistics_records[stat].min_observed)
    statistics_records[stat].min_observed = value;

  if (statistics_records[stat].max_observed < (unsigned long int) value)
    statistics_records[stat].max_observed = value;

  statistics_records[stat].total += value;
  statistics_records[stat].n_calls++;

  statistics__unlock();
}

void statistics_dump (void)
{
  assert(initialized);

  if (runtime_should_print_summary())
  {
    statistic_t i;
    fprintf(stderr, "Dumping libuv statistics\n");

    for (i = STATISTIC_MIN; i < 1 + STATISTIC_MAX - STATISTIC_MIN; i++)
    {
      if (statistics_records[i].any_observations)
        fprintf(stderr, "%s: min_observed %lu max_observed %lu total %lu average %lu\n",
          statistic_to_string(i), statistics_records[i].min_observed, statistics_records[i].max_observed, statistics_records[i].total, statistics_records[i].total/statistics_records[i].n_calls);
      else
        fprintf(stderr, "%s: no observations\n",
          statistic_to_string(i));
    }
  }

  return;
}

/* Private API implementatoin. */

static void statistics__lock (void)
{
  assert(initialized);
  uv_mutex_lock(&mutex);
}

static void statistics__unlock (void)
{
  assert(initialized);
  uv_mutex_unlock(&mutex);
}

static int statistic_valid (statistic_t stat)
{
  return (STATISTIC_MIN <= stat && stat <= STATISTIC_MAX);
}


char *statistic_strings[STATISTIC_MAX - STATISTIC_MIN + 1] = 
  {
    "TIMERS_REGISTERED",
    "TIMERS_EXECUTED",

    "EPOLL_SIMULTANEOUS_EVENTS",
    "EPOLL_EVENTS_EXECUTED",

    "TP_SIMULTANEOUS_WORK",
    "TP_WORK_EXECUTED",

    "CLOSING_EXECUTED",

    "CB_EXECUTED"
  };

static char * statistic_to_string (statistic_t statistic)
{
  assert(statistic_valid(statistic));
  return statistic_strings[statistic];
}
