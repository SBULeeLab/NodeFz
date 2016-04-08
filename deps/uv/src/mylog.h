#ifndef UV_MYLOG_H
#define UV_MYLOG_H

enum log_class
{
  LOG_MAIN = 0,
  LOG_CLASS_MIN = LOG_MAIN,
  LOG_LCBN,
  LOG_SCHEDULER,
  LOG_THREADPOOL,
  LOG_STREAM,
  LOG_LIST,
  LOG_MAP,
  LOG_TREE,
  LOG_CLASS_MAX
};

void mylog (enum log_class, int verbosity, const char *format, ...);
void init_log (void);
void set_verbosity (enum log_class, int);

#endif /* UV_MYLOG_H */
