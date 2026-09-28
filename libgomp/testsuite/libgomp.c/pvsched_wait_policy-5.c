/* Verify an unrecognized GOMP_WAIT_POLICY value is rejected and
   safely falls back to the default wait policy.  */
/* { dg-do run } */
/* { dg-additional-options "-Wl,-Bstatic,-lgomp,-Bdynamic" } */
/* { dg-set-target-env-var GOMP_WAIT_POLICY "bogus" } */
/* { dg-output "Invalid value for environment variable GOMP_WAIT_POLICY" } */

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
