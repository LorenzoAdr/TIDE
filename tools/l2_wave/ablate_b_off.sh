#!/usr/bin/env bash
# Apaga B (veredicto estructurado) para ablación → S0+D. Idempotente.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HPP="$ROOT/src/ai/l2_wave.hpp"
if grep -q 'inline constexpr bool kWaveJobVeredictoEstructurado = false;' "$HPP"; then
  echo "B already off"
  exit 0
fi
if ! grep -q 'inline constexpr bool kWaveJobVeredictoEstructurado = true;' "$HPP"; then
  echo "unexpected flag state in $HPP" >&2
  exit 1
fi
perl -i -pe 's/inline constexpr bool kWaveJobVeredictoEstructurado = true;/inline constexpr bool kWaveJobVeredictoEstructurado = false;/' "$HPP"
# C must stay off
if grep -q 'inline constexpr bool kWaveControlCerrarPolos = true;' "$HPP"; then
  perl -i -pe 's/inline constexpr bool kWaveControlCerrarPolos = true;/inline constexpr bool kWaveControlCerrarPolos = false;/' "$HPP"
fi
cmake --build "$ROOT/build" --target l2_harness_cli -j"$(nproc)"
echo "B off; harness rebuilt"
