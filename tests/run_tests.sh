#!/usr/bin/env bash
# StolasScript test suite
#   tests/hosted/*.stola   compiled with the default (hosted) target; stdout
#                          must match the .out file
#   tests/native/*.stola   compiled with --target linux (no libc); stdout plus
#                          "exit N" must match the .out file
#   tests/errors/*.stola   must fail to compile with the `expect-error` text
#   os/kernel.stola        booted in QEMU (if available) and driven via serial
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
S="$ROOT/s"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0

ok()   { pass=$((pass+1)); printf "  \033[32mPASS\033[0m %s\n" "$1"; }
bad()  { fail=$((fail+1)); printf "  \033[31mFAIL\033[0m %s\n" "$1"; }

echo "== hosted"
for f in "$ROOT"/tests/hosted/*.stola; do
  name="$(basename "$f" .stola)"
  if "$S" build "$f" -o "$TMP/$name" >"$TMP/$name.cerr" 2>&1 &&
     (cd "$TMP" && timeout 60 "./$name" >"$TMP/$name.actual" 2>&1) &&
     diff -u "${f%.stola}.out" "$TMP/$name.actual" >"$TMP/$name.diff"; then
    ok "$name"
  else
    bad "$name"; cat "$TMP/$name.cerr" "$TMP/$name.diff" 2>/dev/null | head -30
  fi
done

echo "== native (linux, no libc)"
for f in "$ROOT"/tests/native/*.stola; do
  name="$(basename "$f" .stola)"
  if "$S" build "$f" --target linux -o "$TMP/$name" >"$TMP/$name.cerr" 2>&1; then
    (cd "$TMP" && timeout 60 "./$name" a b >"$TMP/$name.actual" 2>&1; echo "exit $?" >>"$TMP/$name.actual")
    if diff -u "${f%.stola}.out" "$TMP/$name.actual" >"$TMP/$name.diff"; then
      if ldd "$TMP/$name" 2>&1 | grep -q "not a dynamic executable"; then ok "$name (static, no libc)"; else ok "$name"; fi
    else bad "$name"; head -30 "$TMP/$name.diff"; fi
  else
    bad "$name"; head -20 "$TMP/$name.cerr"
  fi
done

echo "== compile errors"
for f in "$ROOT"/tests/errors/*.stola; do
  name="$(basename "$f" .stola)"
  expect="$(sed -n 's|^// expect-error: ||p' "$f" | head -1)"
  target="$(sed -n 's|^// target: ||p' "$f" | head -1)"
  out="$("$S" "$f" "$TMP/$name.s" ${target:+--target "$target"} 2>&1)"
  if [ $? -ne 0 ] && grep -qF "$expect" <<<"$out"; then ok "$name"; else bad "$name"; echo "$out" | head -5; fi
done

echo "== StolaOS (kernel target, QEMU)"
if command -v qemu-system-x86_64 >/dev/null 2>&1; then
  if "$S" build "$ROOT/os/kernel.stola" --target kernel -o "$TMP/stolaos.bin" >"$TMP/os.cerr" 2>&1; then
    (sleep 1; printf 'calc 6 * 7\nfib 20\nwrite a.txt hola\ncat a.txt\nspawn\n'; sleep 2; printf 'ps\nshutdown\n'; sleep 3) |
      timeout 30 qemu-system-x86_64 -kernel "$TMP/stolaos.bin" -serial stdio -display none \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 -no-reboot -m 128 2>/dev/null | tr -d '\r' >"$TMP/os.log"
    good=1
    for want in "Kernel x86-64 escrito 100% en StolasScript" "[ok] IDT y PIC configurados" \
                "= 42" "fib( 20 ) = 6765" "hola" "[2] primos" "apagando StolaOS"; do
      grep -qF "$want" "$TMP/os.log" || { good=0; echo "    missing: $want"; }
    done
    if [ $good = 1 ]; then ok "boot + shell + multitasking"; else bad "stolaos"; tail -20 "$TMP/os.log"; fi
  else
    bad "stolaos build"; cat "$TMP/os.cerr"
  fi
else
  echo "  (skipped: qemu-system-x86_64 not installed)"
fi

echo
echo "passed: $pass  failed: $fail"
[ $fail -eq 0 ]
