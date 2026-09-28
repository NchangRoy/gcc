/* Verify GOMP_WAIT_POLICY parsing is case-insensitive: "PvSched" must
   be recognized as the pvsched keyword.  There's normally no
   /dev/guest_ivshmem on a test machine, so gomp_pvsched_init() fails
   and initialize_env() resets gomp_pv_wait_policy back to -1 - the
   "Error initializing pvsched" message only appears if the keyword
   was matched in the first place, which is what we're checking.  */
/* { dg-do run } */
/* { dg-additional-options "-Wl,-Bstatic,-lgomp,-Bdynamic" } */
/* { dg-set-target-env-var GOMP_WAIT_POLICY "PvSched" } */
/* { dg-output "Error initializing pvsched" } */

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
