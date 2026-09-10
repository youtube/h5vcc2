// Copyright 2024 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "starboard/shared/modular/starboard_layer_posix_unistd_abi_wrappers.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "starboard/shared/modular/starboard_layer_posix_errno_abi_wrappers.h"
#include "starboard/shared/modular/starboard_layer_posix_fcntl_abi_wrappers.h"
#include "starboard/shared/modular/starboard_layer_posix_stat_abi_wrappers.h"
#include "starboard/shared/modular/starboard_layer_posix_unistd_abi_wrappers.h"

namespace {
int musl_conf_to_platform_conf(int name) {
  switch (name) {
#if defined(_PC_LINK_MAX)
    case MUSL_PC_LINK_MAX:
      return _PC_LINK_MAX;
#endif  // defined(_PC_LINK_MAX)
#if defined(_PC_MAX_CANON)
    case MUSL_PC_MAX_CANON:
      return _PC_MAX_CANON;
#endif  // defined(_PC_MAX_CANON)
#if defined(_PC_MAX_INPUT)
    case MUSL_PC_MAX_INPUT:
      return _PC_MAX_INPUT;
#endif  // defined(_PC_MAX_INPUT)
#if defined(_PC_NAME_MAX)
    case MUSL_PC_NAME_MAX:
      return _PC_NAME_MAX;
#endif  // defined(_PC_NAME_MAX)
#if defined(_PC_PATH_MAX)
    case MUSL_PC_PATH_MAX:
      return _PC_PATH_MAX;
#endif  // defined(_PC_PATH_MAX)
#if defined(_PC_PIPE_BUF)
    case MUSL_PC_PIPE_BUF:
      return _PC_PIPE_BUF;
#endif  // defined(_PC_PIPE_BUF)
#if defined(_PC_CHOWN_RESTRICTED)
    case MUSL_PC_CHOWN_RESTRICTED:
      return _PC_CHOWN_RESTRICTED;
#endif  // defined(_PC_CHOWN_RESTRICTED)
#if defined(_PC_NO_TRUNC)
    case MUSL_PC_NO_TRUNC:
      return _PC_NO_TRUNC;
#endif  // defined(_PC_NO_TRUNC)
#if defined(_PC_VDISABLE)
    case MUSL_PC_VDISABLE:
      return _PC_VDISABLE;
#endif  // defined(_PC_VDISABLE)
#if defined(_PC_SYNC_IO)
    case MUSL_PC_SYNC_IO:
      return _PC_SYNC_IO;
#endif  // defined(_PC_SYNC_IO)
#if defined(_PC_ASYNC_IO)
    case MUSL_PC_ASYNC_IO:
      return _PC_ASYNC_IO;
#endif  // defined(_PC_ASYNC_IO)
#if defined(_PC_PRIO_IO)
    case MUSL_PC_PRIO_IO:
      return _PC_PRIO_IO;
#endif  // defined(_PC_PRIO_IO)
#if defined(_PC_SOCK_MAXBUF)
    case MUSL_PC_SOCK_MAXBUF:
      return _PC_SOCK_MAXBUF;
#endif  // defined(_PC_SOCK_MAXBUF)
#if defined(_PC_FILESIZEBITS)
    case MUSL_PC_FILESIZEBITS:
      return _PC_FILESIZEBITS;
#endif  // defined(_PC_FILESIZEBITS)
#if defined(_PC_REC_INCR_XFER_SIZE)
    case MUSL_PC_REC_INCR_XFER_SIZE:
      return _PC_REC_INCR_XFER_SIZE;
#endif  // defined(_PC_REC_INCR_XFER_SIZE)
#if defined(_PC_REC_MAX_XFER_SIZE)
    case MUSL_PC_REC_MAX_XFER_SIZE:
      return _PC_REC_MAX_XFER_SIZE;
#endif  // defined(_PC_REC_MAX_XFER_SIZE)
#if defined(_PC_REC_MIN_XFER_SIZE)
    case MUSL_PC_REC_MIN_XFER_SIZE:
      return _PC_REC_MIN_XFER_SIZE;
#endif  // defined(_PC_REC_MIN_XFER_SIZE)
#if defined(_PC_REC_XFER_ALIGN)
    case MUSL_PC_REC_XFER_ALIGN:
      return _PC_REC_XFER_ALIGN;
#endif  // defined(_PC_REC_XFER_ALIGN)
#if defined(_PC_ALLOC_SIZE_MIN)
    case MUSL_PC_ALLOC_SIZE_MIN:
      return _PC_ALLOC_SIZE_MIN;
#endif  // defined(_PC_ALLOC_SIZE_MIN)
#if defined(_PC_SYMLINK_MAX)
    case MUSL_PC_SYMLINK_MAX:
      return _PC_SYMLINK_MAX;
#endif  // defined(_PC_SYMLINK_MAX)
#if defined(_PC_2_SYMLINKS)
    case MUSL_PC_2_SYMLINKS:
      return _PC_2_SYMLINKS;
#endif        // defined(_PC_2_SYMLINKS)
    default:  // Explicitly handle unsupported names
      errno = EINVAL;
      return -1;
  }
}

int access_helper(int musl_amode) {
  int platform_amode = 0;
  if (musl_amode == MUSL_F_OK) {
    return F_OK;
  }
  if (musl_amode & MUSL_R_OK) {
    platform_amode |= R_OK;
  }
  if (musl_amode & MUSL_W_OK) {
    platform_amode |= W_OK;
  }
  if (musl_amode & MUSL_X_OK) {
    platform_amode |= X_OK;
  }
  return platform_amode;
}

int musl_unlink_flag_to_platform_flag(int musl_flag) {
  switch (musl_flag) {
    case 0:
      return 0;
    case MUSL_AT_REMOVEDIR:
      return AT_REMOVEDIR;
    default:
      errno = EINVAL;
      return -1;
  }
}

#if defined(__ANDROID__)
// Recognized musl names bionic can't resolve. POSIX reserves EINVAL for invalid
// names, so return -1 (unsupported) without EINVAL translating bionic's correct
// semantics to a still conformant implementation to match the tests
// expectations. For the pathconf/fpathconf case, EINVAL can be optionally set
// for an unsupported association, and that's what bionic does. It's conformant,
// but returning -1 without setting EINVAL also conforms and matches the test
// expectations.
//   pathconf() docs sate that it *shall fail* with EINVAL when "The value of
//   name is not valid." and that it *may fail* with it when "The implementation
//   does not support an association of the variable name with the specified
//   file."
//   Ref:
//     https://pubs.opengroup.org/onlinepubs/9799919799/functions/fpathconf.html
bool IsUnsupportedAndroidSysconfName(int name) {
  // Options: _SC_V6_* aren't defined in the NDK; legacy _SC_XBS5_* defined but
  // rejected by bionic. <unistd.h> uses value -1 for "not supported"; POSIX
  // leaves an unsupported option's result unspecified, so -1/no-EINVAL is
  // conformant.
  //
  // sysconf() docs state, for the return value, that "If the variable
  // corresponding to name is dependent on an unsupported option, the results
  // are unspecified."
  // Ref:
  //   https://pubs.opengroup.org/onlinepubs/9799919799/functions/sysconf.html
  // And on <unistd.h>, section "Constants for Options and Option Groups":
  // "If a symbolic constant is not defined or is defined with the value -1,
  // the option is not supported for compilation."
  // Ref:
  //   https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/unistd.h.html
  // Issue 8 renamed _SC_V6_* to _SC_V7_* and dropped the legacy _SC_XBS5_*, so
  // those spellings are documented in Issue 7 and Issue 6.
  return name == MUSL_SC_V6_ILP32_OFF32 || name == MUSL_SC_V6_ILP32_OFFBIG ||
         name == MUSL_SC_V6_LP64_OFF64 || name == MUSL_SC_V6_LPBIG_OFFBIG ||
         name == MUSL_SC_XBS5_ILP32_OFF32 ||
         name == MUSL_SC_XBS5_ILP32_OFFBIG || name == MUSL_SC_XBS5_LP64_OFF64 ||
         name == MUSL_SC_XBS5_LPBIG_OFFBIG;
}

bool IsUnsupportedAndroidPathconfName(int name) {
  // _PC_REC_*_XFER_SIZE: <limits.h> vars with no limit for the file -> -1
  // without errno. Translates bionic's EINVAL to conformant behavior that
  // matches NPLB tests expectations.
  // _PC_SOCK_MAXBUF: non-POSIX extension the NDK lacks, handled the same way.
  //   pathconf() RETURN VALUE: "If the variable corresponding to name is
  //   described in <limits.h> as a maximum or minimum value and the variable
  //   has no limit for the path or file descriptor, both pathconf() and
  //   fpathconf() shall return -1 without changing errno."
  //   https://pubs.opengroup.org/onlinepubs/9799919799/functions/fpathconf.html
  //   <limits.h>, "Pathname Variable Values", lists both
  //   {POSIX_REC_INCR_XFER_SIZE} and {POSIX_REC_MAX_XFER_SIZE}.
  //   https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/limits.h.html
  return name == MUSL_PC_SOCK_MAXBUF || name == MUSL_PC_REC_INCR_XFER_SIZE ||
         name == MUSL_PC_REC_MAX_XFER_SIZE;
}
#endif  // defined(__ANDROID__)
}  // namespace

int __abi_wrap_ftruncate(int fildes, musl_off_t length) {
  return ftruncate(fildes, static_cast<off_t>(length));
}

musl_off_t __abi_wrap_lseek(int fildes, musl_off_t offset, int whence) {
  return static_cast<off_t>(lseek(fildes, static_cast<off_t>(offset), whence));
}

long __abi_wrap_sysconf(int name) {
#if defined(__ANDROID__)
  if (IsUnsupportedAndroidSysconfName(name)) {
    return -1;
  }
#endif
  switch (name) {
#if defined(_SC_ARG_MAX)
    case MUSL_SC_ARG_MAX:
      return sysconf(_SC_ARG_MAX);
#endif  // defined(_SC_ARG_MAX)
#if defined(_SC_CHILD_MAX)
    case MUSL_SC_CHILD_MAX:
      return sysconf(_SC_CHILD_MAX);
#endif  // defined(_SC_CHILD_MAX)
    case MUSL_SC_CLK_TCK:
      return sysconf(_SC_CLK_TCK);
#if defined(_SC_NGROUPS_MAX)
    case MUSL_SC_NGROUPS_MAX:
      return sysconf(_SC_NGROUPS_MAX);
#endif  // defined(_SC_NGROUPS_MAX)
#if defined(_SC_OPEN_MAX)
    case MUSL_SC_OPEN_MAX:
      return sysconf(_SC_OPEN_MAX);
#endif  // defined(_SC_OPEN_MAX)
#if defined(_SC_STREAM_MAX)
    case MUSL_SC_STREAM_MAX:
      return sysconf(_SC_STREAM_MAX);
#endif  // defined(_SC_STREAM_MAX)
#if defined(_SC_TZNAME_MAX)
    case MUSL_SC_TZNAME_MAX:
      return sysconf(_SC_TZNAME_MAX);
#endif  // defined(_SC_TZNAME_MAX)
#if defined(_SC_JOB_CONTROL)
    case MUSL_SC_JOB_CONTROL:
      return sysconf(_SC_JOB_CONTROL);
#endif  // defined(_SC_JOB_CONTROL)
#if defined(_SC_SAVED_IDS)
    case MUSL_SC_SAVED_IDS:
      return sysconf(_SC_SAVED_IDS);
#endif  // defined(_SC_SAVED_IDS)
#if defined(_SC_REALTIME_SIGNALS)
    case MUSL_SC_REALTIME_SIGNALS:
      return sysconf(_SC_REALTIME_SIGNALS);
#endif  // defined(_SC_REALTIME_SIGNALS)
#if defined(_SC_PRIORITY_SCHEDULING)
    case MUSL_SC_PRIORITY_SCHEDULING:
      return sysconf(_SC_PRIORITY_SCHEDULING);
#endif  // defined(_SC_PRIORITY_SCHEDULING)
#if defined(_SC_TIMERS)
    case MUSL_SC_TIMERS:
      return sysconf(_SC_TIMERS);
#endif  // defined(_SC_TIMERS)
#if defined(_SC_ASYNCHRONOUS_IO)
    case MUSL_SC_ASYNCHRONOUS_IO:
      return sysconf(_SC_ASYNCHRONOUS_IO);
#endif  // defined(_SC_ASYNCHRONOUS_IO)
#if defined(_SC_PRIORITIZED_IO)
    case MUSL_SC_PRIORITIZED_IO:
      return sysconf(_SC_PRIORITIZED_IO);
#endif  // defined(_SC_PRIORITIZED_IO)
#if defined(_SC_SYNCHRONIZED_IO)
    case MUSL_SC_SYNCHRONIZED_IO:
      return sysconf(_SC_SYNCHRONIZED_IO);
#endif  // defined(_SC_SYNCHRONIZED_IO)
#if defined(_SC_FSYNC)
    case MUSL_SC_FSYNC:
      return sysconf(_SC_FSYNC);
#endif  // defined(_SC_FSYNC)
#if defined(_SC_MAPPED_FILES)
    case MUSL_SC_MAPPED_FILES:
      return sysconf(_SC_MAPPED_FILES);
#endif  // defined(_SC_MAPPED_FILES)
#if defined(_SC_MEMLOCK)
    case MUSL_SC_MEMLOCK:
      return sysconf(_SC_MEMLOCK);
#endif  // defined(_SC_MEMLOCK)
#if defined(_SC_MEMLOCK_RANGE)
    case MUSL_SC_MEMLOCK_RANGE:
      return sysconf(_SC_MEMLOCK_RANGE);
#endif  // defined(_SC_MEMLOCK_RANGE)
#if defined(_SC_MEMORY_PROTECTION)
    case MUSL_SC_MEMORY_PROTECTION:
      return sysconf(_SC_MEMORY_PROTECTION);
#endif  // defined(_SC_MEMORY_PROTECTION)
#if defined(_SC_MESSAGE_PASSING)
    case MUSL_SC_MESSAGE_PASSING:
      return sysconf(_SC_MESSAGE_PASSING);
#endif  // defined(_SC_MESSAGE_PASSING)
#if defined(_SC_SEMAPHORES)
    case MUSL_SC_SEMAPHORES:
      return sysconf(_SC_SEMAPHORES);
#endif  // defined(_SC_SEMAPHORES)
#if defined(_SC_SHARED_MEMORY_OBJECTS)
    case MUSL_SC_SHARED_MEMORY_OBJECTS:
      return sysconf(_SC_SHARED_MEMORY_OBJECTS);
#endif  // defined(_SC_SHARED_MEMORY_OBJECTS)
#if defined(_SC_AIO_LISTIO_MAX)
    case MUSL_SC_AIO_LISTIO_MAX:
      return sysconf(_SC_AIO_LISTIO_MAX);
#endif  // defined(_SC_AIO_LISTIO_MAX)
#if defined(_SC_AIO_MAX)
    case MUSL_SC_AIO_MAX:
      return sysconf(_SC_AIO_MAX);
#endif  // defined(_SC_AIO_MAX)
#if defined(_SC_AIO_PRIO_DELTA_MAX)
    case MUSL_SC_AIO_PRIO_DELTA_MAX:
      return sysconf(_SC_AIO_PRIO_DELTA_MAX);
#endif  // defined(_SC_AIO_PRIO_DELTA_MAX)
#if defined(_SC_DELAYTIMER_MAX)
    case MUSL_SC_DELAYTIMER_MAX:
      return sysconf(_SC_DELAYTIMER_MAX);
#endif  // defined(_SC_DELAYTIMER_MAX)
#if defined(_SC_MQ_OPEN_MAX)
    case MUSL_SC_MQ_OPEN_MAX:
      return sysconf(_SC_MQ_OPEN_MAX);
#endif  // defined(_SC_MQ_OPEN_MAX)
#if defined(_SC_MQ_PRIO_MAX)
    case MUSL_SC_MQ_PRIO_MAX:
      return sysconf(_SC_MQ_PRIO_MAX);
#endif  // defined(_SC_MQ_PRIO_MAX)
#if defined(_SC_VERSION)
    case MUSL_SC_VERSION:
      return sysconf(_SC_VERSION);
#endif  // defined(_SC_VERSION)
    case MUSL_SC_PAGESIZE:
      return sysconf(_SC_PAGESIZE);
#if defined(_SC_RTSIG_MAX)
    case MUSL_SC_RTSIG_MAX:
      return sysconf(_SC_RTSIG_MAX);
#endif  // defined(_SC_RTSIG_MAX)
#if defined(_SC_SEM_NSEMS_MAX)
    case MUSL_SC_SEM_NSEMS_MAX:
      return sysconf(_SC_SEM_NSEMS_MAX);
#endif  // defined(_SC_SEM_NSEMS_MAX)
#if defined(_SC_SEM_VALUE_MAX)
    case MUSL_SC_SEM_VALUE_MAX:
      return sysconf(_SC_SEM_VALUE_MAX);
#endif  // defined(_SC_SEM_VALUE_MAX)
#if defined(_SC_SIGQUEUE_MAX)
    case MUSL_SC_SIGQUEUE_MAX:
      return sysconf(_SC_SIGQUEUE_MAX);
#endif  // defined(_SC_SIGQUEUE_MAX)
#if defined(_SC_TIMER_MAX)
    case MUSL_SC_TIMER_MAX:
      return sysconf(_SC_TIMER_MAX);
#endif  // defined(_SC_TIMER_MAX)
#if defined(_SC_BC_BASE_MAX)
    case MUSL_SC_BC_BASE_MAX:
      return sysconf(_SC_BC_BASE_MAX);
#endif  // defined(_SC_BC_BASE_MAX)
#if defined(_SC_BC_DIM_MAX)
    case MUSL_SC_BC_DIM_MAX:
      return sysconf(_SC_BC_DIM_MAX);
#endif  // defined(_SC_BC_DIM_MAX)
#if defined(_SC_BC_SCALE_MAX)
    case MUSL_SC_BC_SCALE_MAX:
      return sysconf(_SC_BC_SCALE_MAX);
#endif  // defined(_SC_BC_SCALE_MAX)
#if defined(_SC_BC_STRING_MAX)
    case MUSL_SC_BC_STRING_MAX:
      return sysconf(_SC_BC_STRING_MAX);
#endif  // defined(_SC_BC_STRING_MAX)
#if defined(_SC_COLL_WEIGHTS_MAX)
    case MUSL_SC_COLL_WEIGHTS_MAX:
      return sysconf(_SC_COLL_WEIGHTS_MAX);
#endif  // defined(_SC_COLL_WEIGHTS_MAX)
#if defined(_SC_EXPR_NEST_MAX)
    case MUSL_SC_EXPR_NEST_MAX:
      return sysconf(_SC_EXPR_NEST_MAX);
#endif  // defined(_SC_EXPR_NEST_MAX)
#if defined(_SC_LINE_MAX)
    case MUSL_SC_LINE_MAX:
      return sysconf(_SC_LINE_MAX);
#endif  // defined(_SC_LINE_MAX)
#if defined(_SC_RE_DUP_MAX)
    case MUSL_SC_RE_DUP_MAX:
      return sysconf(_SC_RE_DUP_MAX);
#endif  // defined(_SC_RE_DUP_MAX)
#if defined(_SC_2_VERSION)
    case MUSL_SC_2_VERSION:
      return sysconf(_SC_2_VERSION);
#endif  // defined(_SC_2_VERSION)
#if defined(_SC_2_C_BIND)
    case MUSL_SC_2_C_BIND:
      return sysconf(_SC_2_C_BIND);
#endif  // defined(_SC_2_C_BIND)
#if defined(_SC_2_C_DEV)
    case MUSL_SC_2_C_DEV:
      return sysconf(_SC_2_C_DEV);
#endif  // defined(_SC_2_C_DEV)
#if defined(_SC_2_FORT_DEV)
    case MUSL_SC_2_FORT_DEV:
      return sysconf(_SC_2_FORT_DEV);
#endif  // defined(_SC_2_FORT_DEV)
#if defined(_SC_2_FORT_RUN)
    case MUSL_SC_2_FORT_RUN:
      return sysconf(_SC_2_FORT_RUN);
#endif  // defined(_SC_2_FORT_RUN)
#if defined(_SC_2_SW_DEV)
    case MUSL_SC_2_SW_DEV:
      return sysconf(_SC_2_SW_DEV);
#endif  // defined(_SC_2_SW_DEV)
#if defined(_SC_2_LOCALEDEF)
    case MUSL_SC_2_LOCALEDEF:
      return sysconf(_SC_2_LOCALEDEF);
#endif  // defined(_SC_2_LOCALEDEF)
#if defined(_SC_IOV_MAX)
    case MUSL_SC_IOV_MAX:
      return sysconf(_SC_IOV_MAX);
#endif  // defined(_SC_IOV_MAX)
#if defined(_SC_THREADS)
    case MUSL_SC_THREADS:
      return sysconf(_SC_THREADS);
#endif  // defined(_SC_THREADS)
#if defined(_SC_THREAD_SAFE_FUNCTIONS)
    case MUSL_SC_THREAD_SAFE_FUNCTIONS:
      return sysconf(_SC_THREAD_SAFE_FUNCTIONS);
#endif  // defined(_SC_THREAD_SAFE_FUNCTIONS)
#if defined(_SC_GETGR_R_SIZE_MAX)
    case MUSL_SC_GETGR_R_SIZE_MAX:
      return sysconf(_SC_GETGR_R_SIZE_MAX);
#endif  // defined(_SC_GETGR_R_SIZE_MAX)
#if defined(_SC_GETPW_R_SIZE_MAX)
    case MUSL_SC_GETPW_R_SIZE_MAX:
      return sysconf(_SC_GETPW_R_SIZE_MAX);
#endif  // defined(_SC_GETPW_R_SIZE_MAX)
#if defined(_SC_LOGIN_NAME_MAX)
    case MUSL_SC_LOGIN_NAME_MAX:
      return sysconf(_SC_LOGIN_NAME_MAX);
#endif  // defined(_SC_LOGIN_NAME_MAX)
#if defined(_SC_TTY_NAME_MAX)
    case MUSL_SC_TTY_NAME_MAX:
      return sysconf(_SC_TTY_NAME_MAX);
#endif  // defined(_SC_TTY_NAME_MAX)
#if defined(_SC_THREAD_DESTRUCTOR_ITERATIONS)
    case MUSL_SC_THREAD_DESTRUCTOR_ITERATIONS:
      return sysconf(_SC_THREAD_DESTRUCTOR_ITERATIONS);
#endif  // defined(_SC_THREAD_DESTRUCTOR_ITERATIONS)
#if defined(_SC_THREAD_KEYS_MAX)
    case MUSL_SC_THREAD_KEYS_MAX:
      return sysconf(_SC_THREAD_KEYS_MAX);
#endif  // defined(_SC_THREAD_KEYS_MAX)
#if defined(_SC_THREAD_STACK_MIN)
    case MUSL_SC_THREAD_STACK_MIN:
      return sysconf(_SC_THREAD_STACK_MIN);
#endif  // defined(_SC_THREAD_STACK_MIN)
#if defined(_SC_THREAD_THREADS_MAX)
    case MUSL_SC_THREAD_THREADS_MAX:
      return sysconf(_SC_THREAD_THREADS_MAX);
#endif  // defined(_SC_THREAD_THREADS_MAX)
#if defined(_SC_THREAD_ATTR_STACKADDR)
    case MUSL_SC_THREAD_ATTR_STACKADDR:
      return sysconf(_SC_THREAD_ATTR_STACKADDR);
#endif  // defined(_SC_THREAD_ATTR_STACKADDR)
#if defined(_SC_THREAD_ATTR_STACKSIZE)
    case MUSL_SC_THREAD_ATTR_STACKSIZE:
      return sysconf(_SC_THREAD_ATTR_STACKSIZE);
#endif  // defined(_SC_THREAD_ATTR_STACKSIZE)
#if defined(_SC_THREAD_PRIORITY_SCHEDULING)
    case MUSL_SC_THREAD_PRIORITY_SCHEDULING:
      return sysconf(_SC_THREAD_PRIORITY_SCHEDULING);
#endif  // defined(_SC_THREAD_PRIORITY_SCHEDULING)
#if defined(_SC_THREAD_PRIO_INHERIT)
    case MUSL_SC_THREAD_PRIO_INHERIT:
      return sysconf(_SC_THREAD_PRIO_INHERIT);
#endif  // defined(_SC_THREAD_PRIO_INHERIT)
#if defined(_SC_THREAD_PRIO_PROTECT)
    case MUSL_SC_THREAD_PRIO_PROTECT:
      return sysconf(_SC_THREAD_PRIO_PROTECT);
#endif  // defined(_SC_THREAD_PRIO_PROTECT)
#if defined(_SC_THREAD_PROCESS_SHARED)
    case MUSL_SC_THREAD_PROCESS_SHARED:
      return sysconf(_SC_THREAD_PROCESS_SHARED);
#endif  // defined(_SC_THREAD_PROCESS_SHARED)
#if defined(_SC_NPROCESSORS_CONF)
    case MUSL_SC_NPROCESSORS_CONF:
      return sysconf(_SC_NPROCESSORS_CONF);
#endif  // defined(_SC_NPROCESSORS_CONF)
    case MUSL_SC_NPROCESSORS_ONLN:
      return sysconf(_SC_NPROCESSORS_ONLN);
    case MUSL_SC_PHYS_PAGES:
      return sysconf(MUSL_SC_PHYS_PAGES);
#if defined(_SC_ATEXIT_MAX)
    case MUSL_SC_ATEXIT_MAX:
      return sysconf(_SC_ATEXIT_MAX);
#endif  // defined(_SC_ATEXIT_MAX)
#if defined(_SC_XOPEN_VERSION)
    case MUSL_SC_XOPEN_VERSION:
      return sysconf(_SC_XOPEN_VERSION);
#endif  // defined(_SC_XOPEN_VERSION)
#if defined(_SC_XOPEN_UNIX)
    case MUSL_SC_XOPEN_UNIX:
      return sysconf(_SC_XOPEN_UNIX);
#endif  // defined(_SC_XOPEN_UNIX)
#if defined(_SC_XOPEN_CRYPT)
    case MUSL_SC_XOPEN_CRYPT:
      return sysconf(_SC_XOPEN_CRYPT);
#endif  // defined(_SC_XOPEN_CRYPT)
#if defined(_SC_XOPEN_ENH_I18N)
    case MUSL_SC_XOPEN_ENH_I18N:
      return sysconf(_SC_XOPEN_ENH_I18N);
#endif  // defined(_SC_XOPEN_ENH_I18N)
#if defined(_SC_XOPEN_SHM)
    case MUSL_SC_XOPEN_SHM:
      return sysconf(_SC_XOPEN_SHM);
#endif  // defined(_SC_XOPEN_SHM)
#if defined(_SC_2_CHAR_TERM)
    case MUSL_SC_2_CHAR_TERM:
      return sysconf(_SC_2_CHAR_TERM);
#endif  // defined(_SC_2_CHAR_TERM)
#if defined(_SC_2_UPE)
    case MUSL_SC_2_UPE:
      return sysconf(_SC_2_UPE);
#endif  // defined(_SC_2_UPE)
#if defined(_SC_XBS5_ILP32_OFF32)
    case MUSL_SC_XBS5_ILP32_OFF32:
      return sysconf(_SC_XBS5_ILP32_OFF32);
#endif  // defined(_SC_XBS5_ILP32_OFF32)
#if defined(_SC_XBS5_ILP32_OFFBIG)
    case MUSL_SC_XBS5_ILP32_OFFBIG:
      return sysconf(_SC_XBS5_ILP32_OFFBIG);
#endif  // defined(_SC_XBS5_ILP32_OFFBIG)
#if defined(_SC_XBS5_LP64_OFF64)
    case MUSL_SC_XBS5_LP64_OFF64:
      return sysconf(_SC_XBS5_LP64_OFF64);
#endif  // defined(_SC_XBS5_LP64_OFF64)
#if defined(_SC_XBS5_LPBIG_OFFBIG)
    case MUSL_SC_XBS5_LPBIG_OFFBIG:
      return sysconf(_SC_XBS5_LPBIG_OFFBIG);
#endif  // defined(_SC_XBS5_LPBIG_OFFBIG)
#if defined(_SC_XOPEN_LEGACY)
    case MUSL_SC_XOPEN_LEGACY:
      return sysconf(_SC_XOPEN_LEGACY);
#endif  // defined(_SC_XOPEN_LEGACY)
#if defined(_SC_XOPEN_REALTIME)
    case MUSL_SC_XOPEN_REALTIME:
      return sysconf(_SC_XOPEN_REALTIME);
#endif  // defined(_SC_XOPEN_REALTIME)
#if defined(_SC_XOPEN_REALTIME_THREADS)
    case MUSL_SC_XOPEN_REALTIME_THREADS:
      return sysconf(_SC_XOPEN_REALTIME_THREADS);
#endif  // defined(_SC_XOPEN_REALTIME_THREADS)
#if defined(_SC_ADVISORY_INFO)
    case MUSL_SC_ADVISORY_INFO:
      return sysconf(_SC_ADVISORY_INFO);
#endif  // defined(_SC_ADVISORY_INFO)
#if defined(_SC_BARRIERS)
    case MUSL_SC_BARRIERS:
      return sysconf(_SC_BARRIERS);
#endif  // defined(_SC_BARRIERS)
#if defined(_SC_CLOCK_SELECTION)
    case MUSL_SC_CLOCK_SELECTION:
      return sysconf(_SC_CLOCK_SELECTION);
#endif  // defined(_SC_CLOCK_SELECTION)
#if defined(_SC_CPUTIME)
    case MUSL_SC_CPUTIME:
      return sysconf(_SC_CPUTIME);
#endif  // defined(_SC_CPUTIME)
#if defined(_SC_THREAD_CPUTIME)
    case MUSL_SC_THREAD_CPUTIME:
      return sysconf(_SC_THREAD_CPUTIME);
#endif  // defined(_SC_THREAD_CPUTIME)
#if defined(_SC_MONOTONIC_CLOCK)
    case MUSL_SC_MONOTONIC_CLOCK:
      return sysconf(_SC_MONOTONIC_CLOCK);
#endif  // defined(_SC_MONOTONIC_CLOCK)
#if defined(_SC_READER_WRITER_LOCKS)
    case MUSL_SC_READER_WRITER_LOCKS:
      return sysconf(_SC_READER_WRITER_LOCKS);
#endif  // defined(_SC_READER_WRITER_LOCKS)
#if defined(_SC_SPIN_LOCKS)
    case MUSL_SC_SPIN_LOCKS:
      return sysconf(_SC_SPIN_LOCKS);
#endif  // defined(_SC_SPIN_LOCKS)
#if defined(_SC_REGEXP)
    case MUSL_SC_REGEXP:
      return sysconf(_SC_REGEXP);
#endif  // defined(_SC_REGEXP)
#if defined(_SC_SHELL)
    case MUSL_SC_SHELL:
      return sysconf(_SC_SHELL);
#endif  // defined(_SC_SHELL)
#if defined(_SC_SPAWN)
    case MUSL_SC_SPAWN:
      return sysconf(_SC_SPAWN);
#endif  // defined(_SC_SPAWN)
#if defined(_SC_SPORADIC_SERVER)
    case MUSL_SC_SPORADIC_SERVER:
      return sysconf(_SC_SPORADIC_SERVER);
#endif  // defined(_SC_SPORADIC_SERVER)
#if defined(_SC_THREAD_SPORADIC_SERVER)
    case MUSL_SC_THREAD_SPORADIC_SERVER:
      return sysconf(_SC_THREAD_SPORADIC_SERVER);
#endif  // defined(_SC_THREAD_SPORADIC_SERVER)
#if defined(_SC_TIMEOUTS)
    case MUSL_SC_TIMEOUTS:
      return sysconf(_SC_TIMEOUTS);
#endif  // defined(_SC_TIMEOUTS)
#if defined(_SC_TYPED_MEMORY_OBJECTS)
    case MUSL_SC_TYPED_MEMORY_OBJECTS:
      return sysconf(_SC_TYPED_MEMORY_OBJECTS);
#endif  // defined(_SC_TYPED_MEMORY_OBJECTS)
#if defined(_SC_2_PBS)
    case MUSL_SC_2_PBS:
      return sysconf(_SC_2_PBS);
#endif  // defined(_SC_2_PBS)
#if defined(_SC_2_PBS_ACCOUNTING)
    case MUSL_SC_2_PBS_ACCOUNTING:
      return sysconf(_SC_2_PBS_ACCOUNTING);
#endif  // defined(_SC_2_PBS_ACCOUNTING)
#if defined(_SC_2_PBS_LOCATE)
    case MUSL_SC_2_PBS_LOCATE:
      return sysconf(_SC_2_PBS_LOCATE);
#endif  // defined(_SC_2_PBS_LOCATE)
#if defined(_SC_2_PBS_MESSAGE)
    case MUSL_SC_2_PBS_MESSAGE:
      return sysconf(_SC_2_PBS_MESSAGE);
#endif  // defined(_SC_2_PBS_MESSAGE)
#if defined(_SC_2_PBS_TRACK)
    case MUSL_SC_2_PBS_TRACK:
      return sysconf(_SC_2_PBS_TRACK);
#endif  // defined(_SC_2_PBS_TRACK)
#if defined(_SC_SYMLOOP_MAX)
    case MUSL_SC_SYMLOOP_MAX:
      return sysconf(_SC_SYMLOOP_MAX);
#endif  // defined(_SC_SYMLOOP_MAX)
#if defined(_SC_2_PBS_CHECKPOINT)
    case MUSL_SC_2_PBS_CHECKPOINT:
      return sysconf(_SC_2_PBS_CHECKPOINT);
#endif  // defined(_SC_2_PBS_CHECKPOINT)
#if defined(_SC_V6_ILP32_OFF32)
    case MUSL_SC_V6_ILP32_OFF32:
      return sysconf(_SC_V6_ILP32_OFF32);
#endif  // defined(_SC_V6_ILP32_OFF32)
#if defined(_SC_V6_ILP32_OFFBIG)
    case MUSL_SC_V6_ILP32_OFFBIG:
      return sysconf(_SC_V6_ILP32_OFFBIG);
#endif  // defined(_SC_V6_ILP32_OFFBIG)
#if defined(_SC_V6_LP64_OFF64)
    case MUSL_SC_V6_LP64_OFF64:
      return sysconf(_SC_V6_LP64_OFF64);
#endif  // defined(_SC_V6_LP64_OFF64)
#if defined(_SC_V6_LPBIG_OFFBIG)
    case MUSL_SC_V6_LPBIG_OFFBIG:
      return sysconf(_SC_V6_LPBIG_OFFBIG);
#endif  // defined(_SC_V6_LPBIG_OFFBIG)
#if defined(_SC_HOST_NAME_MAX)
    case MUSL_SC_HOST_NAME_MAX:
      return sysconf(_SC_HOST_NAME_MAX);
#endif  // defined(_SC_HOST_NAME_MAX)
#if defined(_SC_TRACE)
    case MUSL_SC_TRACE:
      return sysconf(_SC_TRACE);
#endif  // defined(_SC_TRACE)
#if defined(_SC_TRACE_EVENT_FILTER)
    case MUSL_SC_TRACE_EVENT_FILTER:
      return sysconf(_SC_TRACE_EVENT_FILTER);
#endif  // defined(_SC_TRACE_EVENT_FILTER)
#if defined(_SC_TRACE_INHERIT)
    case MUSL_SC_TRACE_INHERIT:
      return sysconf(_SC_TRACE_INHERIT);
#endif  // defined(_SC_TRACE_INHERIT)
#if defined(_SC_TRACE_LOG)
    case MUSL_SC_TRACE_LOG:
      return sysconf(_SC_TRACE_LOG);
#endif  // defined(_SC_TRACE_LOG)
#if defined(_SC_IPV6)
    case MUSL_SC_IPV6:
      return sysconf(_SC_IPV6);
#endif  // defined(_SC_IPV6)
#if defined(_SC_RAW_SOCKETS)
    case MUSL_SC_RAW_SOCKETS:
      return sysconf(_SC_RAW_SOCKETS);
#endif  // defined(_SC_RAW_SOCKETS)
#if defined(_SC_V7_ILP32_OFF32)
    case MUSL_SC_V7_ILP32_OFF32:
      return sysconf(_SC_V7_ILP32_OFF32);
#endif  // defined(_SC_V7_ILP32_OFF32)
#if defined(_SC_V7_ILP32_OFFBIG)
    case MUSL_SC_V7_ILP32_OFFBIG:
      return sysconf(_SC_V7_ILP32_OFFBIG);
#endif  // defined(_SC_V7_ILP32_OFFBIG)
#if defined(_SC_V7_LP64_OFF64)
    case MUSL_SC_V7_LP64_OFF64:
      return sysconf(_SC_V7_LP64_OFF64);
#endif  // defined(_SC_V7_LP64_OFF64)
#if defined(_SC_V7_LPBIG_OFFBIG)
    case MUSL_SC_V7_LPBIG_OFFBIG:
      return sysconf(_SC_V7_LPBIG_OFFBIG);
#endif  // defined(_SC_V7_LPBIG_OFFBIG)
#if defined(_SC_SS_REPL_MAX)
    case MUSL_SC_SS_REPL_MAX:
      return sysconf(_SC_SS_REPL_MAX);
#endif  // defined(_SC_SS_REPL_MAX)
#if defined(_SC_TRACE_EVENT_NAME_MAX)
    case MUSL_SC_TRACE_EVENT_NAME_MAX:
      return sysconf(_SC_TRACE_EVENT_NAME_MAX);
#endif  // defined(_SC_TRACE_EVENT_NAME_MAX)
#if defined(_SC_TRACE_NAME_MAX)
    case MUSL_SC_TRACE_NAME_MAX:
      return sysconf(_SC_TRACE_NAME_MAX);
#endif  // defined(_SC_TRACE_NAME_MAX)
#if defined(_SC_TRACE_SYS_MAX)
    case MUSL_SC_TRACE_SYS_MAX:
      return sysconf(_SC_TRACE_SYS_MAX);
#endif  // defined(_SC_TRACE_SYS_MAX)
#if defined(_SC_TRACE_USER_EVENT_MAX)
    case MUSL_SC_TRACE_USER_EVENT_MAX:
      return sysconf(_SC_TRACE_USER_EVENT_MAX);
#endif  // defined(_SC_TRACE_USER_EVENT_MAX)
#if defined(_SC_XOPEN_STREAMS)
    case MUSL_SC_XOPEN_STREAMS:
      return sysconf(_SC_XOPEN_STREAMS);
#endif  // defined(_SC_XOPEN_STREAMS)
#if defined(_SC_THREAD_ROBUST_PRIO_INHERIT)
    case MUSL_SC_THREAD_ROBUST_PRIO_INHERIT:
      return sysconf(_SC_THREAD_ROBUST_PRIO_INHERIT);
#endif  // defined(_SC_THREAD_ROBUST_PRIO_INHERIT)
#if defined(_SC_THREAD_ROBUST_PRIO_PROTECT)
    case MUSL_SC_THREAD_ROBUST_PRIO_PROTECT:
      return sysconf(_SC_THREAD_ROBUST_PRIO_PROTECT);
#endif        // defined(_SC_THREAD_ROBUST_PRIO_PROTECT)
    default:  // Explicitly handle unsupported names
      errno = EINVAL;
      return -1;
  }
}

// If |musl_conf_to_platform_conf| returns -1,
// just return -1 (errno is set to EINVAL by musl_conf_to_platform_conf()).
long __abi_wrap_pathconf(const char* path, int name) {
#if defined(__ANDROID__)
  if (IsUnsupportedAndroidPathconfName(name)) {
    return -1;
  }
#endif
  int converted_name = musl_conf_to_platform_conf(name);
  return (converted_name == -1) ? -1 : pathconf(path, converted_name);
}

musl_uid_t __abi_wrap_geteuid() {
  return static_cast<musl_uid_t>(geteuid());
}

musl_uid_t __abi_wrap_getuid() {
  return static_cast<musl_uid_t>(getuid());
}

musl_pid_t __abi_wrap_getpid() {
  return static_cast<musl_pid_t>(getpid());
}

musl_pid_t __abi_wrap_gettid() {
#if defined(gettid)
  return static_cast<musl_pid_t>(gettid());
#else
  return static_cast<musl_pid_t>(syscall(SYS_gettid));
#endif
}

int __abi_wrap_access(const char* path, int amode) {
  return access(path, access_helper(amode));
}

int __abi_wrap_fchown(int fd, musl_uid_t owner, musl_gid_t group) {
  return fchown(fd, static_cast<uid_t>(owner), static_cast<gid_t>(group));
}

int __abi_wrap_unlinkat(int fildes, const char* path, int musl_flag) {
  fildes = (fildes == MUSL_AT_FDCWD) ? AT_FDCWD : fildes;
  int flag = musl_unlink_flag_to_platform_flag(musl_flag);
  if (flag == -1) {
    return -1;
  }
  return unlinkat(fildes, path, flag);
}

ssize_t __abi_wrap_pread(int fd, void* buf, size_t size, musl_off_t ofs) {
  return pread(fd, buf, size, static_cast<off_t>(ofs));
}

ssize_t __abi_wrap_pwrite(int fd,
                          const void* buf,
                          size_t size,
                          musl_off_t ofs) {
  return pwrite(fd, buf, size, static_cast<off_t>(ofs));
}
