/* Verify GOMP_DYNAMIC_POLICY parsing is case-insensitive: "PvSched" must
   be recognized as the pvsched keyword.  */
/* { dg-do run } */
/* { dg-additional-options "-Wl,-Bstatic,-lgomp,-Bdynamic" } */
/* { dg-set-target-env-var GOMP_DYNAMIC_POLICY "PvSched" } */

extern int gomp_dynamic_policy;

int
main (void)
{
  if (gomp_dynamic_policy != 1)
    __builtin_abort ();
  return 0;
}
