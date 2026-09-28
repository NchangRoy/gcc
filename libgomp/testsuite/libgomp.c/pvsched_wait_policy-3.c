/* Verify that conflicting OMP_WAIT_POLICY / GOMP_WAIT_POLICY settings
   are handled by printing a conflict error and safely falling back to
   the default wait policy (gomp_pv_wait_policy = -1, default spin counts).  */
/* { dg-do run } */
/* { dg-additional-options "-Wl,-Bstatic,-lgomp,-Bdynamic" } */
/* { dg-set-target-env-var OMP_WAIT_POLICY "passive" } */
/* { dg-set-target-env-var GOMP_WAIT_POLICY "active" } */
/* { dg-output "Conflicting OMP_WAIT_POLICY and GOMP_WAIT_POLICY" } */

extern int gomp_pv_wait_policy;
extern unsigned long long gomp_spin_count_var, gomp_throttled_spin_count_var;

int
main (void)
{
  if (gomp_pv_wait_policy != -1)
    __builtin_abort ();
  if (gomp_spin_count_var != 300000ULL)
    __builtin_abort ();
  if (gomp_throttled_spin_count_var != 100ULL)
    __builtin_abort ();
  return 0;
}
