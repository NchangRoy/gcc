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

#define WAKE_CONTEXT 0
#define WAIT_CONTEXT 1

struct juunansei_knobs {
  unsigned long long consuming_idle : 1;
  unsigned long long : 63;
  uint8_t global_phantoms_detected;
  uint8_t global_phantoms_increasing;
  uint8_t _rsvd0[6];
  int64_t stability_requirement;
  uint8_t _pad[40];
} __attribute__((aligned(64)));

struct juunansei_decision {
  uint64_t host_watermark;
  uint64_t timestamp;
  int32_t phantom_average;
  int32_t decision_nr;
  int32_t curr_dop;
  int32_t prev_dop;
  int32_t active_dop;
  uint8_t padding[28];
} __attribute__((packed, aligned(64)));


struct juunansei_context {
   int32_t phantom_average;
   int32_t idle_average;
   int32_t min_idle_average;
   uint8_t padding[52];
} __attribute__ ((aligned(64)));

struct juunansei_timestamps {
  uint64_t last_decision;
  uint8_t padding[56];
} __attribute__((packed, aligned(64)));

struct juunansei_status {
  uint64_t watermark_used_at_decision;
  int64_t total_vcpus;
  int64_t active_dop;
  int64_t prev_dop;
  int64_t curr_dop;
  int64_t decision_count;
  int64_t dop_before_idle_consumption;
  uint64_t padding;
} __attribute__((packed, aligned(64)));

extern struct juunansei_knobs juunansei_knobs;
extern int first_decision;
extern struct juunansei_decision decision;
extern struct juunansei_status status;
extern struct juunansei_timestamps timestamps;

extern void juunansei_initialize (void);

extern struct juunansei_decision *
juunansei_first_decision (struct juunansei_context *context);

extern void update_juunansei ();

extern int juunansei_active_dop_resolution (void);

extern int juunansei_curr_dop_resolution (struct juunansei_context *context);

extern int juunansei_get_latest_context (struct juunansei_context *ctxt);

extern int juunansei_same_tick_decision (void);

extern void juunanei_cleanup (void);

static inline int
read_latest_phantom_average (pvsched_u64 *avg)
{
  pvsched_u64 watermark;
  if (guest_ivshmem_h2g == NULL)
    return -ENODEV;
  watermark = __atomic_load_n (&((struct hg_message *) guest_ivshmem_h2g)[pvsched_ivshmem_header->latest_slot].msg,
				MEMMODEL_RELAXED);
  *avg = (watermark >> 16) & 0xffff;
  DTRACE_PROBE1 (gomp, phantom_average, *avg);
  return 0;
}

static inline int
read_watermark_at_slot (uint32_t slot, pvsched_u64 * msg)
{
  if (guest_ivshmem_h2g == NULL)
    return -ENODEV;
  *msg = __atomic_load_n (&((struct hg_message *) guest_ivshmem_h2g)[slot].msg,
				MEMMODEL_RELAXED);
  return 0;
}

static inline int
read_n_lastest_watermarks (uint32_t n, pvsched_u64 *msgs)
{
  uint32_t nr_slots, latest_slot_pos_in_his, best_ts, slot, i;
  pvsched_u64 watermark;

  if (guest_ivshmem_h2g == NULL)
    return -ENODEV;

  nr_slots = pvsched_ivshmem_header->h2g_page_size / sizeof (struct hg_message);
  read_watermark_at_slot (pvsched_ivshmem_header->latest_slot, &watermark);
  latest_slot_pos_in_his = 1;
  best_ts = (uint32_t) (watermark >> 32);

  for (slot = 1; slot < nr_slots; slot++)
    {
      uint32_t ts;

      read_watermark_at_slot (slot, &watermark);
      ts = (uint32_t) (watermark >> 32);

      if (ts == best_ts)
        {
          latest_slot_pos_in_his = slot;
          break;
        }
    }

  slot = latest_slot_pos_in_his;
  for (i = 0; i < n; i++)
    {
      read_watermark_at_slot (slot, &msgs[i]);
      slot = (slot == 1) ? nr_slots - 1 : slot - 1;
    }

  return (int) n;
}

#endif
