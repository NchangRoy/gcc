/* Copyright (C) 2026 Free Software Foundation, Inc.
   Contributed by NchangRoy <royfru44@gmail.com>,
   Himadri CS <himadrics@protonmail.com>,
   Andrea Righi <righi.andrea@gmail.com>,
   Tobias Burnus <tburnus@baylibre.com>,
   Thomas Schwinge <tschwinge@baylibre.com>, as part of GSoC 2026.

   This file is part of the GNU Offloading and Multi Processing Library
   (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* Support for pv-barrier-sync, a paravirtualized, scheduling-guided
   barrier synchronization mechanism for libgomp.

   Policy inspired by: Himadri Chhaya-Shailesh, "Scheduler Guided OpenMP
   Execution in Cloud VMs" (PhD thesis),
   https://inria.hal.science/tel-05438117v2

   This policy depends on an external component, Phantom Tracker:
   https://github.com/phantom-tracker/phantom-tracker  */

#ifndef PVSCHED_H
#define PVSCHED_H

#include "libgomp.h"
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/sdt.h>
#include <errno.h>

typedef uint64_t pvsched_u64;

struct hg_message {
  pvsched_u64 msg;
  pvsched_u64 padding [7];
};

struct ivshmem_header {
	uint32_t magic;
	uint32_t version;
	uint32_t latest_slot;
	uint64_t h2g_page_offset;
	uint64_t h2g_page_size;
};

extern int guest_ivshmem_fd;
extern void *guest_ivshmem_h2g;
extern struct ivshmem_header *pvsched_ivshmem_header;

static inline int
read_latest_phantom_average (pvsched_u64 *avg)
{
  if (guest_ivshmem_h2g == NULL)
    return -ENODEV;
  *avg = __atomic_load_n (&((struct hg_message *) guest_ivshmem_h2g)[pvsched_ivshmem_header->latest_slot].msg,
			   MEMMODEL_RELAXED);
  DTRACE_PROBE1 (gomp, phantom_average, *avg);
  return 0;
}

#endif