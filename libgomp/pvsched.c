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

/* Juunansei degree-of-parallelism decision logic for pv-barrier-sync.

   Policy inspired by: Himadri Chhaya-Shailesh, "Scheduler Guided OpenMP
   Execution in Cloud VMs" (PhD thesis),
   https://inria.hal.science/tel-05438117v2  */

#include "libgomp.h"
#include "pvsched.h"

extern int omp_get_num_procs (void);

struct juunansei_knobs juunansei_knobs;
struct juunansei_decision decision;
struct juunansei_status status;
struct juunansei_timestamps timestamps;
int first_decision = 1;

void
juunansei_initialize ()
{
  decision = (struct juunansei_decision) {0};
  status = (struct juunansei_status) {0};
  timestamps = (struct juunansei_timestamps) {0};

  status.total_vcpus = omp_get_num_procs ();
  juunansei_knobs.global_phantoms_detected = 0;
  juunansei_knobs.global_phantoms_increasing = 0;
  juunansei_knobs.consuming_idle = 0;
  juunansei_knobs.stability_requirement = 2;
}


struct juunansei_decision *
juunansei_first_decision (struct juunansei_context *context)
{
  first_decision = 0;

  if (context->phantom_average > 0)
    decision.curr_dop = status.total_vcpus;
  else
    decision.curr_dop = context->min_idle_average;
  if (decision.curr_dop < 1)
    decision.curr_dop = 1;
  decision.active_dop = decision.curr_dop;
  update_juunansei ();
  return &decision;
}

static inline int
juunansei_min_idle_avg (pvsched_u64 * idle_avg)
{
  int n = juunansei_knobs.stability_requirement;
  int ret, min_idle_avg;

  if (n <= 0)
    return -ENODATA;

  pvsched_u64 msgs[n];

  ret = read_n_lastest_watermarks (n, msgs);
  if (ret < n)
    return ret < 0 ? ret : -ENODATA;

  min_idle_avg = (int) (msgs[0] & 0xFFFF);
  for (int i = 1; i < n; i++)
    {
      int cur = (int) (msgs[i] & 0xFFFF);
      if (cur < min_idle_avg)
	        min_idle_avg = cur;
    }

  *idle_avg = min_idle_avg;
  return 0;
}

void
juunanei_cleanup()
{
  gomp_debug (0, "pvsched: disabling GOMP_DYNAMIC_POLICY=pvsched for the "
	      "rest of this process\n");
  gomp_dynamic_policy = 0;
}

static inline int
juunansei_nr_extra_blocked (int *extra_blocked)
{
    pvsched_u64 last_two_watermarks[2];
    int ret,curr_phant_avg,prev_phant_avg;
    ret = read_n_lastest_watermarks(2, last_two_watermarks);
    if (ret < 2)
      {
        gomp_debug (0, "pvsched: nr_extra_blocked: got %d/2 watermarks "
		    "(ret=%d)\n", ret < 0 ? 0 : ret, ret);
        juunanei_cleanup ();
        return ret < 0 ? ret : -ENODATA;
      }
    curr_phant_avg = (last_two_watermarks[0] >> 16) & 0xFFFF;
    prev_phant_avg = (last_two_watermarks[1] >> 16) & 0xFFFF;
    *extra_blocked = curr_phant_avg - prev_phant_avg;
    return 0;
}

static inline int
juunansei_global_phantoms_increasing()
{
  int n = juunansei_knobs.stability_requirement;
  pvsched_u64 watermarks[n];
  int ret;
  int32_t p_newest, p_oldest;
  int64_t t_newest, t_oldest, total_time_diff;

  ret = read_n_lastest_watermarks (n, watermarks);
  if (ret < n)
    {
      gomp_debug (0, "pvsched: global_phantoms_increasing: got %d/%d "
		  "watermarks (ret=%d)\n", ret < 0 ? 0 : ret, n, ret);
      juunanei_cleanup ();
      return ret < 0 ? ret : -ENODATA;
    }

  p_newest = (int32_t) ((watermarks[0] >> 16) & 0xFFFF);
  t_newest = (int64_t) (uint32_t) (watermarks[0] >> 32);

  p_oldest = (int32_t) ((watermarks[n - 1] >> 16) & 0xFFFF);
  t_oldest = (int64_t) (uint32_t) (watermarks[n - 1] >> 32);

  total_time_diff = t_newest - t_oldest;
  if (total_time_diff == 0)
    return 0;

  juunansei_knobs.global_phantoms_increasing =
    ((p_newest - p_oldest) > 0) ? 1 : 0;

  return juunansei_knobs.global_phantoms_increasing;
}

static inline int
juunansei_global_phantoms_detected()
{
  int n = juunansei_knobs.stability_requirement;
  pvsched_u64 watermarks[n];
  int ret, detected = 0;

  ret = read_n_lastest_watermarks (n, watermarks);
  if (ret < n)
    {
      gomp_debug (0, "pvsched: global_phantoms_detected: got %d/%d "
		  "watermarks (ret=%d)\n", ret < 0 ? 0 : ret, n, ret);
      juunanei_cleanup ();
      return ret < 0 ? ret : -ENODATA;
    }

  for (int i = 0; i < n; i++)
    {
      int32_t p = (int32_t) ((watermarks[i] >> 16) & 0xFFFF);
      if (p > 0)
	{
	  detected = 1;
	  break;
	}
    }

  return detected;
}

int
juunansei_get_latest_context (struct juunansei_context *ctxt)
{
  pvsched_u64 watermark, min_idle;
  int ret;

  ret = read_watermark_at_slot (pvsched_ivshmem_header->latest_slot, &watermark);
  if (ret < 0)
    {
      gomp_debug (0, "pvsched: read_watermark_at_slot(%u) failed (ret=%d)\n",
		  pvsched_ivshmem_header->latest_slot, ret);
      juunanei_cleanup ();
      return ret;
    }

  decision.host_watermark = watermark;
  decision.timestamp = watermark >> 32;

  ctxt->phantom_average = (int32_t) ((watermark >> 16) & 0xFFFF);
  decision.phantom_average = ctxt->phantom_average;
  ctxt->idle_average = (int32_t) (watermark & 0xFFFF);

  ret = juunansei_min_idle_avg (&min_idle);
  if (ret < 0)
    {
      gomp_debug (0, "pvsched: juunansei_min_idle_avg failed (ret=%d, "
		  "need %ld watermarks)\n", ret,
		  (long) juunansei_knobs.stability_requirement);
      juunanei_cleanup ();
      return ret;
    }

  ctxt->min_idle_average = (int32_t) min_idle;

  juunansei_knobs.global_phantoms_detected = juunansei_global_phantoms_detected ();
  juunansei_knobs.global_phantoms_increasing = juunansei_global_phantoms_increasing ();

  return 0;
}

int
juunansei_active_dop_resolution ()
{
  if (juunansei_global_phantoms_detected ())
    {
      if (juunansei_global_phantoms_increasing ())
	{
	  int extra_blocked;

	  if (juunansei_nr_extra_blocked (&extra_blocked) < 0)
	    decision.active_dop = status.active_dop;
	  else
	    decision.active_dop = status.active_dop - extra_blocked;

	  if (decision.active_dop < 1)
	    decision.active_dop = 1;
	}
      else
	        decision.active_dop = status.active_dop;
    }
  else
    decision.active_dop = status.curr_dop;

  if (decision.active_dop < 1)
    decision.active_dop = 1;

  return decision.active_dop;
}

int
juunansei_curr_dop_resolution (struct juunansei_context *context)
{
  if (juunansei_knobs.consuming_idle)
    {
      if (status.dop_before_idle_consumption > decision.active_dop)
	{
	  int64_t max_stability
	    = (pvsched_ivshmem_header->h2g_page_size
	       / sizeof (struct hg_message)) - 1;

	  juunansei_knobs.stability_requirement *= 2;
	  if (juunansei_knobs.stability_requirement > max_stability)
	    juunansei_knobs.stability_requirement = max_stability;
	}
      else
	juunansei_knobs.stability_requirement = 2;
    }

  if (context->phantom_average == 0 && context->idle_average == 0)
    {
      juunansei_knobs.consuming_idle = 0;
      decision.curr_dop = decision.active_dop;
    }
  else if (context->phantom_average > 0)
    {
      juunansei_knobs.consuming_idle = 0;
      decision.curr_dop = decision.active_dop - context->phantom_average;
      if (decision.curr_dop < 1)
	decision.curr_dop = 1;
    }
  else if (context->idle_average > 0)
    {
      if (juunansei_global_phantoms_detected ())
        {
          juunansei_knobs.consuming_idle = 0;
          decision.curr_dop = decision.active_dop;
        }
      else if (context->min_idle_average > 0)
        {
          juunansei_knobs.consuming_idle = 1;
          decision.curr_dop = decision.active_dop + context->min_idle_average;
          if (decision.curr_dop > status.total_vcpus)
            decision.curr_dop = status.total_vcpus;
        }
      else
        {
          juunansei_knobs.consuming_idle = 0;
          decision.curr_dop = decision.active_dop;
        }
    }

  if (decision.curr_dop < 1)
    decision.curr_dop = 1;

  if (juunansei_knobs.consuming_idle)
    status.dop_before_idle_consumption = status.curr_dop;
  else
    status.dop_before_idle_consumption = decision.curr_dop;

  return decision.curr_dop;
}

void
update_juunansei ()
{
  decision.prev_dop = status.curr_dop;
  status.prev_dop = status.curr_dop;
  status.curr_dop = decision.curr_dop;
  status.active_dop = decision.active_dop;
  status.watermark_used_at_decision = decision.host_watermark;
  timestamps.last_decision = decision.timestamp;
}

int
juunansei_same_tick_decision ()
{
  decision.curr_dop = status.curr_dop;
  decision.prev_dop = status.prev_dop;
  decision.active_dop = status.curr_dop;

  update_juunansei ();

  return decision.curr_dop;
}
