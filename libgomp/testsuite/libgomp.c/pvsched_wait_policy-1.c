/* Verify GOMP_WAIT_POLICY=active resolves to the active spin/throttle
   constants in env.c's initialize_env().  */
/* { dg-do run } */
/* { dg-additional-options "-Wl,-Bstatic,-lgomp,-Bdynamic" } */
/* { dg-set-target-env-var GOMP_WAIT_POLICY "active" } */

extern int gomp_pv_wait_policy;
extern unsigned long long gomp_spin_count_var, gomp_throttled_spin_count_var;

int
main (void)
{
  if (gomp_pv_wait_policy != 1)
    __builtin_abort ();
  if (gomp_spin_count_var != 30000000000ULL)
    __builtin_abort ();
  if (gomp_throttled_spin_count_var != 1000ULL)
    __builtin_abort ();
  return 0;
}
