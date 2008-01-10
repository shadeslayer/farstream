

#ifndef __CHECK_THREADSAFE_H__
#define __CHECK_THREADSAFE_H__

#include <gst/check/gstcheck.h>

/* Define thread safe versions of the tests */

#define ts_fail_unless(...)             \
  G_STMT_START {                        \
    g_mutex_lock (check_mutex);         \
    fail_unless (__VA_ARGS__);          \
    g_mutex_unlock (check_mutex);       \
  } G_STMT_END


#define ts_fail_if(...)                 \
  G_STMT_START {                        \
    g_mutex_lock (check_mutex);         \
    fail_if (__VA_ARGS__);              \
    g_mutex_unlock (check_mutex);       \
  } G_STMT_END


#define ts_fail(...)    \
  G_STMT_START {                        \
    g_mutex_lock (check_mutex);         \
    fail (__VA_ARGS__);                 \
    g_mutex_unlock (check_mutex);       \
  } G_STMT_END

#endif /* __CHECK_THREADSAFE_H__ */
