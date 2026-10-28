#include <stdio.h>
long count_primes(long limit) {
  long count = 0;
  for (long n = 2; n < limit; n++) {
    int p = 1;
    for (long d = 2; d * d <= n; d++) if (n % d == 0) { p = 0; break; }
    count += p;
  }
  return count;
}
int main(void) { printf("%ld\n", count_primes(2000000)); return 0; }
