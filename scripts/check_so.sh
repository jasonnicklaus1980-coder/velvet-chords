#!/bin/sh
# Sanity-check an ARM build before it goes near an MPC.
set -e
SO="$1"
[ -f "$SO" ] || { echo "missing $SO"; exit 1; }
RE=${READELF:-readelf}
command -v arm-linux-gnueabihf-readelf >/dev/null 2>&1 && RE=arm-linux-gnueabihf-readelf
echo "== $SO"
$RE -h "$SO" | grep -E "Class|Machine|Flags"
$RE -h "$SO" | grep -q "ELF32" || { echo "error: not a 32-bit ELF"; exit 1; }
$RE -h "$SO" | grep -q "ARM" || { echo "error: not an ARM ELF"; exit 1; }
# hard-float ABI: GNU toolchains record it in build attributes, LLVM/zig in the ELF header flags
if ! $RE -A "$SO" | grep -q "Tag_ABI_VFP_args: VFP registers" && ! $RE -h "$SO" | grep -q "hard-float ABI"; then
  echo "error: not hard-float (armhf)"; exit 1
fi
$RE --dyn-syms -W "$SO" | grep -q " VSTPluginMain$" || { echo "error: VSTPluginMain not exported"; exit 1; }
echo "needed libraries:"; $RE -d "$SO" | grep NEEDED
if $RE -d "$SO" | grep -E "NEEDED.*(libm\.|libstdc\+\+|libgcc_s)"; then echo "error: unexpected library dependency"; exit 1; fi
echo "glibc symbol versions:"
VERS=$($RE --dyn-syms -W "$SO" | grep -o "GLIBC_[0-9.]*" | sort -u -t. -k1,1 -k2,2n -k3,3n)
echo "$VERS"
for v in $VERS; do
  minor=$(echo "$v" | sed 's/GLIBC_2\.\([0-9]*\).*/\1/')
  [ "$minor" -le 17 ] || { echo "error: needs $v; build with an older toolchain or remove that symbol"; exit 1; }
done
echo "ok"
