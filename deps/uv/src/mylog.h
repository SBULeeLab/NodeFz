#ifndef UV_MYLOG_H
#define UV_MYLOG_H

enum log_class
{
  LOG_MAIN = 0,
  LOG_CLASS_MIN = LOG_MAIN,
  LOG_LCBN,
  LOG_SCHEDULER,
  LOG_THREADPOOL,
  LOG_LIST,
  LOG_MAP,
  LOG_TREE,
  LOG_UV_STREAM,
  LOG_UV_IO,
  LOG_CLASS_MAX
};

/* Call before any other mylog APIs. */
void mylog_init (void);

/* Higher values are more verbose. */
void mylog_set_verbosity (enum log_class, int level);
void mylog_set_all_verbosity (int level);
/* Blab. */
void mylog (enum log_class, int verbosity, const char *format, ...);

void mylog_UT (void);

#endif /* UV_MYLOG_H */
