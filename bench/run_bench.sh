#!/usr/bin/env bash
# Benchmarks: StolasScript vs C (gcc -O0 / -O2) vs Python 3
set -u
cd "$(dirname "$0")"
S=../s
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

now() { date +%s.%N; }
timeit() { # prints seconds
  local s e; s=$(now); "$@" >"$TMP/out" 2>&1; e=$(now)
  python3 -c "print(f'{$e - $s:.3f}')"
}

printf "%-10s %12s %12s %12s %12s   %s\n" bench StolasScript "C -O2" "C -O0" "Python 3" "result"
for b in fib primes collatz dynamic; do
  $S build $b.stola -o "$TMP/$b" >/dev/null || { echo "build failed: $b"; continue; }
  ts=$(timeit "$TMP/$b"); res=$(cat "$TMP/out")
  if [ -f $b.c ]; then
    gcc -O2 $b.c -o "$TMP/${b}_o2" && gcc -O0 $b.c -o "$TMP/${b}_o0"
    t2=$(timeit "$TMP/${b}_o2"); t0=$(timeit "$TMP/${b}_o0")
  else t2="-"; t0="-"; fi
  tp=$(timeit python3 $b.py)
  printf "%-10s %11ss %11ss %11ss %11ss   %s\n" $b "$ts" "$t2" "$t0" "$tp" "$res"
done
