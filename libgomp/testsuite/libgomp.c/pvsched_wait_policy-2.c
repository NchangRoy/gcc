/* Verify GOMP_WAIT_POLICY=passive resolves to a zero spin count.  */
/* { dg-do run } */
/* { dg-additional-options "-Wl,-Bstatic,-lgomp,-Bdynamic" } */
/* { dg-set-target-env-var GOMP_WAIT_POLICY "passive" } */

extern int gomp_pv_wait_policy;
extern unsigned long long gomp_spin_count_var;

int
main (void)
{
  if (gomp_pv_wait_policy != 0)
    __builtin_abort ();
  if (gomp_spin_count_var != 0)
    __builtin_abort ();
  return 0;
}
