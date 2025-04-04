// Copyright (C) 2017 The Android Open Source Project
// SPDX-License-Identifier: BSD-2-Clause

#include <limits.h>

#include "header_checks.h"

static void limits_h() {
  // These are only defined if they're constants.
#if !defined(__BIONIC__) && !defined(__GLIBC__) && !defined(ANDROID_HOST_MUSL)
  MACRO(AIO_LISTIO_MAX);
  MACRO(AIO_MAX);
#endif
#if !defined(__BIONIC__) && !defined(ANDROID_HOST_MUSL)
  MACRO(AIO_PRIO_DELTA_MAX);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__)
  MACRO(ARG_MAX);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__) && !defined(ANDROID_HOST_MUSL)
  MACRO(ATEXIT_MAX);
  MACRO(CHILD_MAX);
#endif
#if !defined(__BIONIC__)
  MACRO(DELAYTIMER_MAX);
#endif
  MACRO(HOST_NAME_MAX);
  MACRO(IOV_MAX);
  MACRO(LOGIN_NAME_MAX);
#if !defined(__BIONIC__) && !defined(__GLIBC__) && !defined(ANDROID_HOST_MUSL)
  MACRO(MQ_OPEN_MAX);
#endif
#if !defined(__BIONIC__)
  MACRO(MQ_PRIO_MAX);
#endif
#if defined(__BIONIC__)
  MACRO(NSIG_MAX);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__) && !defined(ANDROID_HOST_MUSL)
  MACRO(OPEN_MAX);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__)
  MACRO(PAGESIZE);
  MACRO(PAGE_SIZE);
#endif
  MACRO(PTHREAD_DESTRUCTOR_ITERATIONS);
  MACRO(PTHREAD_KEYS_MAX);
#if !defined(__BIONIC__)
  MACRO(PTHREAD_STACK_MIN);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__) && !defined(ANDROID_HOST_MUSL)
  MACRO(PTHREAD_THREADS_MAX);
#endif
#if !defined(ANDROID_HOST_MUSL)
  MACRO(RTSIG_MAX);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__)
  MACRO(SEM_NSEMS_MAX);
#endif
  MACRO(SEM_VALUE_MAX);
#if !defined(__BIONIC__) && !defined(__GLIBC__) && !defined(ANDROID_HOST_MUSL)
  MACRO(SIGQUEUE_MAX);
  MACRO(SS_REPL_MAX);
  MACRO(STREAM_MAX);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__)
  MACRO(SYMLOOP_MAX);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__) && !defined(ANDROID_HOST_MUSL)
  MACRO(TIMER_MAX);
#endif
#if !defined(__BIONIC__)
  MACRO(TTY_NAME_MAX);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__)
  MACRO(TZNAME_MAX);
#endif

#if !defined(__BIONIC__) && !defined(__GLIBC__)
  MACRO(FILESIZEBITS);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__) && !defined(ANDROID_HOST_MUSL)
  MACRO(LINK_MAX);
#endif
#if !defined(ANDROID_HOST_MUSL)
  MACRO(MAX_CANON);
  MACRO(MAX_INPUT);
#endif
  MACRO(NAME_MAX);
  MACRO(PATH_MAX);
  MACRO(PIPE_BUF);
#if 0 // No libc has these.
  MACRO(POSIX_ALLOC_SIZE_MIN);
  MACRO(POSIX_REC_INCR_XFER_SIZE);
  MACRO(POSIX_REC_MAX_XFER_SIZE);
  MACRO(POSIX_REC_MIN_XFER_SIZE);
  MACRO(POSIX_REC_XFER_ALIGN);
#endif
#if !defined(__BIONIC__) && !defined(__GLIBC__) && !defined(ANDROID_HOST_MUSL)
  MACRO(SYMLINK_MAX);
#endif

#if !defined(__BIONIC__)
  MACRO(BC_BASE_MAX);
  MACRO(BC_DIM_MAX);
  MACRO(BC_SCALE_MAX);
  MACRO(BC_STRING_MAX);
  MACRO(CHARCLASS_NAME_MAX);
  MACRO(COLL_WEIGHTS_MAX);
  MACRO(EXPR_NEST_MAX);
  MACRO(NGROUPS_MAX);
  MACRO(RE_DUP_MAX);
#endif
  MACRO(LINE_MAX);

  MACRO_VALUE(_POSIX_CLOCKRES_MIN, 20000000);

  MACRO_VALUE(_POSIX_AIO_LISTIO_MAX, 2);
  MACRO_VALUE(_POSIX_AIO_MAX, 1);
  MACRO_VALUE(_POSIX_ARG_MAX, 4096);
  MACRO_VALUE(_POSIX_CHILD_MAX, 25);
  MACRO_VALUE(_POSIX_DELAYTIMER_MAX, 32);
  MACRO_VALUE(_POSIX_HOST_NAME_MAX, 255);
  MACRO_VALUE(_POSIX_LINK_MAX, 8);
  MACRO_VALUE(_POSIX_LOGIN_NAME_MAX, 9);
  MACRO_VALUE(_POSIX_MAX_CANON, 255);
  MACRO_VALUE(_POSIX_MAX_INPUT, 255);
  MACRO_VALUE(_POSIX_MQ_OPEN_MAX, 8);
  MACRO_VALUE(_POSIX_MQ_PRIO_MAX, 32);
  MACRO_VALUE(_POSIX_NAME_MAX, 14);
  MACRO_VALUE(_POSIX_NGROUPS_MAX, 8);
  MACRO_VALUE(_POSIX_OPEN_MAX, 20);
  MACRO_VALUE(_POSIX_PATH_MAX, 256);
  MACRO_VALUE(_POSIX_PIPE_BUF, 512);
  MACRO_VALUE(_POSIX_RE_DUP_MAX, 255);
  MACRO_VALUE(_POSIX_RTSIG_MAX, 8);
  MACRO_VALUE(_POSIX_SEM_NSEMS_MAX, 256);
  MACRO_VALUE(_POSIX_SEM_VALUE_MAX, 32767);
  MACRO_VALUE(_POSIX_SIGQUEUE_MAX, 32);
  MACRO_VALUE(_POSIX_SSIZE_MAX, 32767);
#if !defined(__GLIBC__)
  MACRO_VALUE(_POSIX_SS_REPL_MAX, 4);
#endif
  MACRO_VALUE(_POSIX_STREAM_MAX, 8);
  MACRO_VALUE(_POSIX_SYMLINK_MAX, 255);
  MACRO_VALUE(_POSIX_SYMLOOP_MAX, 8);
  MACRO_VALUE(_POSIX_THREAD_DESTRUCTOR_ITERATIONS, 4);
  MACRO_VALUE(_POSIX_THREAD_KEYS_MAX, 128);
  MACRO_VALUE(_POSIX_THREAD_THREADS_MAX, 64);
  MACRO_VALUE(_POSIX_TIMER_MAX, 32);
#if !defined(__GLIBC__)
  MACRO_VALUE(_POSIX_TRACE_EVENT_NAME_MAX, 30);
  MACRO_VALUE(_POSIX_TRACE_NAME_MAX, 8);
  MACRO_VALUE(_POSIX_TRACE_SYS_MAX, 8);
  MACRO_VALUE(_POSIX_TRACE_USER_EVENT_MAX, 32);
#endif
  MACRO_VALUE(_POSIX_TTY_NAME_MAX, 9);
  MACRO_VALUE(_POSIX_TZNAME_MAX, 6);
  MACRO_VALUE(_POSIX2_BC_BASE_MAX, 99);
  MACRO_VALUE(_POSIX2_BC_DIM_MAX, 2048);
  MACRO_VALUE(_POSIX2_BC_SCALE_MAX, 99);
  MACRO_VALUE(_POSIX2_BC_STRING_MAX, 1000);
  MACRO_VALUE(_POSIX2_CHARCLASS_NAME_MAX, 14);
  MACRO_VALUE(_POSIX2_COLL_WEIGHTS_MAX, 2);
  MACRO_VALUE(_POSIX2_EXPR_NEST_MAX, 32);
  MACRO_VALUE(_POSIX2_LINE_MAX, 2048);
  MACRO_VALUE(_POSIX2_RE_DUP_MAX, 255);
#if !defined(__GLIBC__)
  MACRO_VALUE(_XOPEN_IOV_MAX, 16);
  MACRO_VALUE(_XOPEN_NAME_MAX, 255);
  MACRO_VALUE(_XOPEN_PATH_MAX, 1024);
#endif

  MACRO_VALUE(CHAR_BIT, 8);
  MACRO(CHAR_MAX);
  MACRO(CHAR_MIN);
  MACRO(INT_MAX);
  MACRO(INT_MIN);
  MACRO(LLONG_MAX);
  MACRO(LLONG_MIN);
  MACRO(LONG_BIT);
  MACRO(LONG_MAX);
  MACRO(LONG_MIN);
  MACRO(MB_LEN_MAX);
  MACRO_VALUE(SCHAR_MAX, 127);
  MACRO_VALUE(SCHAR_MIN, -128);
  MACRO(SHRT_MAX);
  MACRO(SHRT_MIN);
  MACRO(SSIZE_MAX);
  MACRO_VALUE(UCHAR_MAX, 255);
  MACRO(UINT_MAX);
  MACRO(ULLONG_MAX);
  MACRO(ULONG_MAX);
  MACRO(USHRT_MAX);
  MACRO(WORD_BIT);

#if defined(__BIONIC__)
  MACRO(GETENTROPY_MAX);
#endif

  MACRO(NL_ARGMAX);
  MACRO(NL_LANGMAX);
  MACRO(NL_MSGMAX);
  MACRO(NL_SETMAX);
  MACRO(NL_TEXTMAX);
  MACRO(NZERO);
}
