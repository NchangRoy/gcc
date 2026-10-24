/* Verify an unrecognized GOMP_DYNAMIC_POLICY value is rejected and
   safely falls back to the default (-1).  */
/* { dg-do run } */
/* { dg-additional-options "-Wl,-Bstatic,-lgomp,-Bdynamic" } */
/* { dg-set-target-env-var GOMP_DYNAMIC_POLICY "bogus" } */
/* { dg-output "Invalid value for environment variable GOMP_DYNAMIC_POLICY" } */

extern int gomp_dynamic_policy;

int
main (void)
{
  if (gomp_dynamic_policy != -1)
    __builtin_abort ();
  return 0;
}
