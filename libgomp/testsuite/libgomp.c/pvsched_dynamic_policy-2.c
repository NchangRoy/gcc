/* Verify GOMP_DYNAMIC_POLICY=default resolves gomp_dynamic_policy to 0.  */
/* { dg-do run } */
/* { dg-additional-options "-Wl,-Bstatic,-lgomp,-Bdynamic" } */
/* { dg-set-target-env-var GOMP_DYNAMIC_POLICY "default" } */

extern int gomp_dynamic_policy;

int
main (void)
{
  if (gomp_dynamic_policy != 0)
    __builtin_abort ();
  return 0;
}
