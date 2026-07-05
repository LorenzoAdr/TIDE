#!/usr/bin/env bash
# Audit for user-facing string literals that should use i18n::tr().
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

echo "=== Spanish accents in src/ (exclude i18n catalogs and tests) ==="
grep -rEn '"[^"]*[áéíóúñÁÉÍÓÚÑ][^"]*"' src \
  --include='*.cpp' --include='*.hpp' \
  --exclude='strings_es.cpp' --exclude='strings_en.cpp' 2>/dev/null | head -30 || true

echo
echo "=== MakePanel with string literal ==="
grep -rn 'MakePanel("' src/ui 2>/dev/null || true

echo
echo "=== text(\"...\") with 8+ chars in src/ui (sample) ==="
grep -rEn 'text\(\s*"[^"]{8,}"' src/ui --include='*.cpp' 2>/dev/null | grep -v key_bindings | head -20 || true

echo
echo "=== set_status / status_message literals ==="
grep -rEn 'set_status\("[^"]|status_message\s*=\s*"[^"]|stderr_text\s*=\s*"[^"]|last_error\s*=\s*"[^"]' ../src --include='*.cpp' 2>/dev/null | grep -v i18n/strings_ || true

echo
echo "Done. Review hits above; user-facing strings should use i18n::tr()."
