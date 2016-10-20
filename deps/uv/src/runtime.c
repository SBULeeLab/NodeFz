#include "runtime.h"

#include <stdlib.h> /* getenv */
#include <assert.h> /* assert */

struct
{
  int silent;
  int print_summary;
} runtime_parms;

static int runtime_initialized = 0;

void runtime_init (void)
{
  int silent_default = 0;
  int print_summary_default = 1;

  {
    /* By default, don't be silent. 
     * UV_SILENT affects other printing parms by default, but will be overridden
     * if they are also supplied. 
     */
    char *silentP = getenv("UV_SILENT");
    if (silentP == NULL)
      runtime_parms.silent = silent_default;
    else
    {
      if (atoi(silentP) == 0)
        runtime_parms.silent = 0; 
      else
      {
        runtime_parms.silent = 1; 
        print_summary_default = 0;
      }
    }
  }

  {
    char *printSummaryP = getenv("UV_PRINT_SUMMARY");
    if (printSummaryP == NULL)
      runtime_parms.print_summary = print_summary_default;
    else
    {
      if (atoi(printSummaryP) == 0)
       runtime_parms.print_summary = 0; 
     else
       runtime_parms.print_summary = 1; 
    }
  }

  runtime_initialized = 1;
}

int runtime_should_be_silent (void)
{
  assert(runtime_initialized);
  return runtime_parms.silent;
}

int runtime_should_print_summary (void)
{
  assert(runtime_initialized);
  return runtime_parms.print_summary;
}
