#!/usr/bin/env bash
# Ejemplo mínimo para outline / LSP / DAP Bash.
set -euo pipefail

greet() {
  local name="${1:-world}"
  printf 'hello, %s\n' "${name}"
}

main() {
  greet "tide"
}

main "$@"