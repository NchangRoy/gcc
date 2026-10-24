/* Copyright (C) 2005-2025 Free Software Foundation, Inc.
   Contributed by Jakub Jelinek <jakub@redhat.com>.

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

/* This file contains system specific routines related to counting
   online processors and dynamic load balancing.  */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "libgomp.h"
#include "proc.h"
#include "pvsched.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/sdt.h>
#ifdef HAVE_GETLOADAVG
# ifdef HAVE_SYS_LOADAVG_H
#  include <sys/loadavg.h>
# endif
#endif

#ifdef HAVE_PTHREAD_AFFINITY_NP
unsigned long gomp_cpuset_size;
static unsigned long gomp_get_cpuset_size;
cpu_set_t *gomp_cpusetp;

unsigned long
gomp_cpuset_popcount (unsigned long cpusetsize, cpu_set_t *cpusetp)
{
#ifdef CPU_COUNT_S
  /* glibc 2.7 and above provide a macro for this.  */
  return CPU_COUNT_S (cpusetsize, cpusetp);
#else
#ifdef CPU_COUNT
  if (cpusetsize == sizeof (cpu_set_t))
    /* glibc 2.6 and above provide a macro for this.  */
    return CPU_COUNT (cpusetp);
#endif
  size_t i;
  unsigned long ret = 0;
  extern int check[sizeof (cpusetp->__bits[0]) == sizeof (unsigned long int)
		   ? 1 : -1] __attribute__((unused));

  for (i = 0; i < cpusetsize / sizeof (cpusetp->__bits[0]); i++)
    {
      unsigned long int mask = cpusetp->__bits[i];
      if (mask == 0)
	continue;
      ret += __builtin_popcountl (mask);
    }
  return ret;
#endif
}
#endif

/* At startup, determine the default number of threads.  It would seem
   this should be related to the number of cpus online.  */

void
gomp_init_num_threads (void)
{
#ifdef HAVE_PTHREAD_AFFINITY_NP
#if defined (_SC_NPROCESSORS_CONF) && defined (CPU_ALLOC_SIZE)
  gomp_cpuset_size = sysconf (_SC_NPROCESSORS_CONF);
  gomp_cpuset_size = CPU_ALLOC_SIZE (gomp_cpuset_size);
#else
  gomp_cpuset_size = sizeof (cpu_set_t);
#endif

  gomp_cpusetp = (cpu_set_t *) gomp_malloc (gomp_cpuset_size);
  do
    {
      int ret = pthread_getaffinity_np (pthread_self (), gomp_cpuset_size,
					gomp_cpusetp);
      if (ret == 0)
	{
	  /* Count only the CPUs this process can use.  */
	  gomp_global_icv.nthreads_var
	    = gomp_cpuset_popcount (gomp_cpuset_size, gomp_cpusetp);
	  if (gomp_global_icv.nthreads_var == 0)
	    break;
	  gomp_get_cpuset_size = gomp_cpuset_size;
#ifdef CPU_ALLOC_SIZE
	  unsigned long i;
	  for (i = gomp_cpuset_size * 8; i; i--)
	    if (CPU_ISSET_S (i - 1, gomp_cpuset_size, gomp_cpusetp))
	      break;
	  gomp_cpuset_size = CPU_ALLOC_SIZE (i);
#endif
	  return;
	}
      if (ret != EINVAL)
	break;
#ifdef CPU_ALLOC_SIZE
      if (gomp_cpuset_size < sizeof (cpu_set_t))
	gomp_cpuset_size = sizeof (cpu_set_t);
      else
	gomp_cpuset_size = gomp_cpuset_size * 2;
      if (gomp_cpuset_size < 8 * sizeof (cpu_set_t))
	gomp_cpusetp
	  = (cpu_set_t *) gomp_realloc (gomp_cpusetp, gomp_cpuset_size);
      else
	{
	  /* Avoid gomp_fatal if too large memory allocation would be
	     requested, e.g. kernel returning EINVAL all the time.  */
	  void *p = realloc (gomp_cpusetp, gomp_cpuset_size);
	  if (p == NULL)
	    break;
	  gomp_cpusetp = (cpu_set_t *) p;
	}
#else
      break;
#endif
    }
  while (1);
  gomp_cpuset_size = 0;
  gomp_global_icv.nthreads_var = 1;
  free (gomp_cpusetp);
  gomp_cpusetp = NULL;
#endif
#ifdef _SC_NPROCESSORS_ONLN
  gomp_global_icv.nthreads_var = sysconf (_SC_NPROCESSORS_ONLN);
#endif
}

static int
get_num_procs (void)
{
#ifdef HAVE_PTHREAD_AFFINITY_NP
  if (gomp_places_list == NULL)
    {
      /* Count only the CPUs this process can use.  */
      if (gomp_cpusetp
	  && pthread_getaffinity_np (pthread_self (), gomp_get_cpuset_size,
				     gomp_cpusetp) == 0)
	{
	  int ret = gomp_cpuset_popcount (gomp_get_cpuset_size, gomp_cpusetp);
	  return ret != 0 ? ret : 1;
	}
    }
  else
    {
      /* We can't use pthread_getaffinity_np in this case
	 (we have changed it ourselves, it binds to just one CPU).
	 Count instead the number of different CPUs we are
	 using.  gomp_init_affinity updated gomp_available_cpus to
	 the number of CPUs in the GOMP_AFFINITY mask that we are
	 allowed to use though.  */
      return gomp_available_cpus;
    }
#endif
#ifdef _SC_NPROCESSORS_ONLN
  return sysconf (_SC_NPROCESSORS_ONLN);
#else
  return gomp_icv (false)->nthreads_var;
#endif
}

/* When OMP_DYNAMIC is set, at thread launch determine the number of
   threads we should spawn for this team.  */
/* ??? I have no idea what best practice for this is.  Surely some
   function of the number of processors that are *still* online and
   the load average.  Here I use the number of processors online
   minus the 15 minute load average.  */

unsigned
pv_gomp_dynamic_max_threads(void)
{
  struct juunansei_context context;
  struct juunansei_decision *d;
  struct juunansei_decision decision_snapshot;
  struct juunansei_status status_snapshot;
  int ret;

  ret = juunansei_get_latest_context (&context);
  if (ret < 0)
    return -1;

  if (first_decision)
    {
      d = juunansei_first_decision (&context);
      status_snapshot = status;
      DTRACE_PROBE3 (gomp, dop_decision, d, &status_snapshot, &context);
      return (unsigned) d->curr_dop;
    }

  if (decision.timestamp <= timestamps.last_decision)
    {
      unsigned same_tick_dop = (unsigned) juunansei_same_tick_decision ();
      decision_snapshot = decision;
      status_snapshot = status;
      DTRACE_PROBE3 (gomp, dop_decision, &decision_snapshot, &status_snapshot, &context);
      return same_tick_dop;
    }

  decision.active_dop = juunansei_active_dop_resolution ();
  decision.curr_dop = juunansei_curr_dop_resolution (&context);

  update_juunansei ();

  decision_snapshot = decision;
  status_snapshot = status;
  DTRACE_PROBE3 (gomp, dop_decision, &decision_snapshot, &status_snapshot, &context);

  return (unsigned) decision.curr_dop;
}

unsigned
gomp_dynamic_max_threads (void)
{
  int dop_res;
  unsigned n_onln, loadavg, nthreads_var = gomp_icv (false)->nthreads_var;
  unsigned result;

  if ( gomp_dynamic_policy == 1)
    {
      dop_res = pv_gomp_dynamic_max_threads();
      if(dop_res < 0)
        {
          gomp_debug (0, "pvsched: pv_gomp_dynamic_max_threads failed "
		      "(dop_res=%d), falling back to default heuristic\n",
		      dop_res);
          juunanei_cleanup();
        }
      else
        {
          DTRACE_PROBE1 (gomp, dyn_max_threads, dop_res);
          return dop_res;
        }
    }

  n_onln = get_num_procs ();
  if (n_onln > nthreads_var)
    n_onln = nthreads_var;

  loadavg = 0;
#ifdef HAVE_GETLOADAVG
  {
    double dloadavg[3];
    if (getloadavg (dloadavg, 3) == 3)
      {
	/* Add 0.1 to get a kind of biased rounding.  */
	loadavg = dloadavg[2] + 0.1;
      }
  }
#endif

  if (loadavg >= n_onln)
    result = 1;
  else
    result = n_onln - loadavg;

  DTRACE_PROBE1 (gomp, dyn_max_threads, result);
  return result;
}

int
omp_get_num_procs (void)
{
  return get_num_procs ();
}

ialias (omp_get_num_procs)
