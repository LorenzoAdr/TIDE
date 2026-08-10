#!/usr/bin/env bash
# Pregenerate tree-sitter-latex parser.c on a host with modern glibc.
# Needed for portable builds on Ubuntu 18.04 (glibc 2.27): npm's tree-sitter-cli
# and GitHub tree-sitter binaries require GLIBC ≥ 2.28/2.29.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Keep in sync with FetchContent GIT_TAG for tree_sitter_latex in cmake/Dependencies.cmake
LATEX_TAG="${TUIDE_TREE_SITTER_LATEX_TAG:-v0.6.0}"
OUT_DIR="${TUIDE_TREE_SITTER_LATEX_GENERATED:-${ROOT}/.cache/tree-sitter-latex-${LATEX_TAG}}"
SRC_DIR="${ROOT}/.cache/tree-sitter-latex-src-${LATEX_TAG}"
TREE_SITTER_CLI_VERSION="${TREE_SITTER_CLI_VERSION:-0.25.8}"

log() {
  printf '[pregenerate-tree-sitter-latex] %s\n' "$*" >&2
}

die() {
  printf '[pregenerate-tree-sitter-latex] error: %s\n' "$*" >&2
  exit 1
}

if [[ -f "${OUT_DIR}/parser.c" && -f "${OUT_DIR}/tree_sitter/parser.h" ]]; then
  log "ya existe ${OUT_DIR}/parser.c ($(du -h "${OUT_DIR}/parser.c" | awk '{print $1}'))"
  printf '%s\n' "${OUT_DIR}"
  exit 0
fi

command -v git >/dev/null 2>&1 || die "hace falta git"
command -v npx >/dev/null 2>&1 || die "hace falta npx (Node.js) en el host"

mkdir -p "${ROOT}/.cache"
if [[ ! -d "${SRC_DIR}/.git" ]]; then
  rm -rf "${SRC_DIR}"
  log "clonando latex-lsp/tree-sitter-latex@${LATEX_TAG}..."
  git clone --depth 1 --branch "${LATEX_TAG}" \
    https://github.com/latex-lsp/tree-sitter-latex.git "${SRC_DIR}"
fi

if [[ ! -f "${SRC_DIR}/src/parser.c" ]]; then
  log "generando parser.c con tree-sitter-cli@${TREE_SITTER_CLI_VERSION}..."
  (cd "${SRC_DIR}" && npx --yes "tree-sitter-cli@${TREE_SITTER_CLI_VERSION}" generate)
fi

[[ -f "${SRC_DIR}/src/parser.c" ]] || die "no se generó parser.c"
[[ -d "${SRC_DIR}/src/tree_sitter" ]] || die "falta src/tree_sitter/ tras generate (parser.h)"

mkdir -p "${OUT_DIR}/tree_sitter"
cp -f "${SRC_DIR}/src/parser.c" "${OUT_DIR}/parser.c"
if [[ -f "${SRC_DIR}/src/scanner.c" ]]; then
  cp -f "${SRC_DIR}/src/scanner.c" "${OUT_DIR}/scanner.c"
fi
cp -f "${SRC_DIR}/src/tree_sitter/"*.h "${OUT_DIR}/tree_sitter/"

log "listo: ${OUT_DIR}/parser.c ($(du -h "${OUT_DIR}/parser.c" | awk '{print $1}'))"
printf '%s\n' "${OUT_DIR}"
