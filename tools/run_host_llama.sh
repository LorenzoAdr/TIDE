#!/usr/bin/env bash
# Arranca llama-server en el host (Metal en macOS) para que una VM Linux use TIDE
# en mode=remote + embeddings attach-only.
set -euo pipefail

CHAT_PORT="${TUIDE_HOST_CHAT_PORT:-8080}"
EMBED_PORT="${TUIDE_HOST_EMBED_PORT:-18765}"
NGL="${TUIDE_HOST_NGL:-99}"
CHAT_CTX="${TUIDE_HOST_CHAT_CTX:-32768}"
EMBED_CTX="${TUIDE_HOST_EMBED_CTX:-2048}"
EMBED_NP="${TUIDE_HOST_EMBED_NP:-8}"

CACHE_ROOT="${XDG_CACHE_HOME:-${HOME}/.cache}/tuide/models"
L2_DIR="${CACHE_ROOT}/l2"
EMBED_DIR="${CACHE_ROOT}/embed/intent"
RUNTIME_DIR="${CACHE_ROOT}/runtime"
LOG_DIR="${CACHE_ROOT}/host-llama"
mkdir -p "${LOG_DIR}"

die() {
  printf '[host-llama] error: %s\n' "$*" >&2
  exit 1
}

log() {
  printf '[host-llama] %s\n' "$*"
}

find_llama_server() {
  if [[ -n "${TUIDE_LLAMA_SERVER:-}" && -x "${TUIDE_LLAMA_SERVER}" ]]; then
    printf '%s\n' "${TUIDE_LLAMA_SERVER}"
    return 0
  fi
  if command -v llama-server >/dev/null 2>&1; then
    command -v llama-server
    return 0
  fi
  local candidate
  for candidate in \
      "${RUNTIME_DIR}/llama-b10333/llama-server" \
      "${RUNTIME_DIR}/llama-b10333-vulkan/llama-server" \
      /opt/homebrew/bin/llama-server \
      /usr/local/bin/llama-server; do
    if [[ -x "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  return 1
}

pick_gguf() {
  local dir="$1"
  local preferred="${2:-}"
  if [[ -n "${preferred}" && -f "${preferred}" ]]; then
    printf '%s\n' "${preferred}"
    return 0
  fi
  local f
  for f in \
      "${dir}"/qwen2.5-coder-32b*.gguf \
      "${dir}"/qwen2.5-coder-14b*.gguf \
      "${dir}"/qwen2.5-coder-7b*.gguf \
      "${dir}"/*.gguf; do
    if [[ -f "${f}" ]]; then
      printf '%s\n' "${f}"
      return 0
    fi
  done
  return 1
}

pick_embed_gguf() {
  local preferred="${1:-}"
  if [[ -n "${preferred}" && -f "${preferred}" ]]; then
    printf '%s\n' "${preferred}"
    return 0
  fi
  local f
  for f in "${EMBED_DIR}"/nomic-embed*.gguf "${EMBED_DIR}"/*.gguf; do
    if [[ -f "${f}" ]]; then
      printf '%s\n' "${f}"
      return 0
    fi
  done
  return 1
}

alias_from_gguf() {
  local base
  base="$(basename "$1")"
  base="${base%.gguf}"
  printf '%s\n' "${base}"
}

lan_ip() {
  if [[ -n "${TUIDE_ADVERTISE_HOST:-}" ]]; then
    printf '%s\n' "${TUIDE_ADVERTISE_HOST}"
    return 0
  fi
  local ip
  if [[ "$(uname -s)" == "Darwin" ]]; then
    ip="$(ipconfig getifaddr en0 2>/dev/null || true)"
    if [[ -n "${ip}" ]]; then
      printf '%s\n' "${ip}"
      return 0
    fi
    ip="$(ipconfig getifaddr en1 2>/dev/null || true)"
    if [[ -n "${ip}" ]]; then
      printf '%s\n' "${ip}"
      return 0
    fi
  fi
  ip="$(hostname -I 2>/dev/null | awk '{print $1}' || true)"
  if [[ -n "${ip}" ]]; then
    printf '%s\n' "${ip}"
    return 0
  fi
  printf '%s\n' "192.168.64.1"
}

SERVER="$(find_llama_server)" || die "no hay llama-server. Compílalo con Metal, ponlo en PATH o TUIDE_LLAMA_SERVER."
CHAT_GGUF="$(pick_gguf "${L2_DIR}" "${TUIDE_L2_GGUF:-}")" || \
  die "falta GGUF L2 en ${L2_DIR} (o TUIDE_L2_GGUF=/ruta/modelo.gguf)"
EMBED_GGUF="$(pick_embed_gguf "${TUIDE_EMBED_GGUF:-}")" || \
  die "falta GGUF embeddings en ${EMBED_DIR} (o TUIDE_EMBED_GGUF=/ruta/nomic.gguf)"

CHAT_ALIAS="${TUIDE_L2_API_MODEL:-$(alias_from_gguf "${CHAT_GGUF}")}"
ADVERTISE="$(lan_ip)"
LIB_DIR="$(cd "$(dirname "${SERVER}")" && pwd)"

export DYLD_LIBRARY_PATH="${LIB_DIR}${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export LD_LIBRARY_PATH="${LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

CHAT_PID=""
EMBED_PID=""
cleanup() {
  if [[ -n "${CHAT_PID}" ]]; then
    kill "${CHAT_PID}" 2>/dev/null || true
  fi
  if [[ -n "${EMBED_PID}" ]]; then
    kill "${EMBED_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

log "llama-server: ${SERVER}"
log "chat GGUF:    ${CHAT_GGUF}  alias=${CHAT_ALIAS}  :${CHAT_PORT}  ngl=${NGL}"
log "embed GGUF:   ${EMBED_GGUF}  :${EMBED_PORT}"
log "bind 0.0.0.0  (Metal -ngl ${NGL})"

"${SERVER}" -m "${CHAT_GGUF}" --host 0.0.0.0 --port "${CHAT_PORT}" \
  -ngl "${NGL}" -c "${CHAT_CTX}" --alias "${CHAT_ALIAS}" --log-disable \
  >"${LOG_DIR}/chat.log" 2>&1 &
CHAT_PID=$!

"${SERVER}" -m "${EMBED_GGUF}" --host 0.0.0.0 --port "${EMBED_PORT}" \
  --embedding --pooling mean -c "${EMBED_CTX}" -np "${EMBED_NP}" -ngl "${NGL}" \
  --log-disable >"${LOG_DIR}/embed.log" 2>&1 &
EMBED_PID=$!

sleep 1
if ! kill -0 "${CHAT_PID}" 2>/dev/null; then
  die "chat llama-server murió al arrancar. Ver ${LOG_DIR}/chat.log"
fi
if ! kill -0 "${EMBED_PID}" 2>/dev/null; then
  die "embed llama-server murió al arrancar. Ver ${LOG_DIR}/embed.log"
fi

cat <<EOF

[host-llama] listo. En la VM Linux (tuide):

  export TUIDE_L2_API_BASE=http://${ADVERTISE}:${CHAT_PORT}/v1
  export TUIDE_L2_API_MODEL=${CHAT_ALIAS}
  export TUIDE_EMBED_HOST=${ADVERTISE}
  export TUIDE_EMBED_PORT=${EMBED_PORT}

  ./build/llama_host_comm_test

O en .tuide/config.json:

  "ai": {
    "level2": {
      "mode": "remote",
      "api_base": "http://${ADVERTISE}:${CHAT_PORT}/v1",
      "api_model": "${CHAT_ALIAS}",
      "n_ctx_remote": ${CHAT_CTX}
    },
    "level0": {
      "embeddings": {
        "server_host": "${ADVERTISE}",
        "server_port": ${EMBED_PORT}
      }
    }
  }

UTM suele usar 192.168.64.1; OrbStack, host.orb.internal.
Logs: ${LOG_DIR}/chat.log  ${LOG_DIR}/embed.log
Ctrl+C para parar ambos servidores.

EOF

wait
