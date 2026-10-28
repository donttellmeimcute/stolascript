#include <stdio.h>
long longest_collatz(long limit) {
  long best = 0, best_n = 0;
  for (long s = 1; s < limit; s++) {
    long n = s, steps = 0;
    while (n != 1) { if (n % 2 == 0) n = n / 2; else n = 3 * n + 1; steps++; }
    if (steps > best) { best = steps; best_n = s; }
  }
  return best_n;
}
int main(void) { printf("%ld\n", longest_collatz(1000000)); return 0; }
