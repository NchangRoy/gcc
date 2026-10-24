/* Verify GOMP_DYNAMIC_POLICY=pvsched resolves gomp_dynamic_policy to 1 in
   env.c's initialize_env().  */
/* { dg-do run } */
/* { dg-additional-options "-Wl,-Bstatic,-lgomp,-Bdynamic" } */
/* { dg-set-target-env-var GOMP_DYNAMIC_POLICY "pvsched" } */

extern int gomp_dynamic_policy;

int
main (void)
{
  if (gomp_dynamic_policy != 1)
    __builtin_abort ();
  return 0;
}
