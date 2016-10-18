#ifndef UV_MYLOG_H
#define UV_MYLOG_H

#ifdef JD_LOG_EE
  #define ENTRY_EXIT_LOG(x) \
    mylog x;
#else
  #define ENTRY_EXIT_LOG(x) \
    do { if (0) mylog x; } while (0)
#endif

enum log_class
{
  LOG_MAIN = 0,
  LOG_CLASS_MIN = LOG_MAIN,
  LOG_LCBN,
  LOG_SCHEDULER,
  LOG_THREADPOOL,
  LOG_TIMER,
  LOG_LIST,
  LOG_MAP,
  LOG_TREE,
  LOG_UV_STREAM,
  LOG_UV_IO,
  LOG_UV_ASYNC,
  LOG_STATISTICS,
  LOG_CLASS_MAX
};

/* Call before any other mylog APIs. 
 * Can safely be called more than once; subsequent calls do nothing.
 * However, not thread safe.
 *
 * After calling this, you can set verbosity with mylog_set[_all]_verbosity.
 * Then you can use mylog and mylog_buf to log things.
 */
void mylog_init (void);

/* Higher values are more verbose. */
void mylog_set_verbosity (enum log_class, int level);
void mylog_set_all_verbosity (int level);
/* Blab. format must end in \n. */
void mylog (enum log_class, int verbosity, const char *format, ...);
/* Print buf as LEN char's. */
void mylog_buf (enum log_class, int verbosity, char *buf, int len);

void mylog_UT (void);

#endif /* UV_MYLOG_H */
