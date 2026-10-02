#!/usr/bin/env bash
# Arranca el hub HTML (Lanzamiento + Inspección) y, desde ahí, llama-server
# en el host (Metal en macOS) para que una VM Linux use TIDE en mode=remote.
#
# Uso:
#   ./tools/run_host_llama.sh                 # hub HTML en el navegador
#   ./tools/run_host_llama.sh --llm 7b        # hub; preselecciona chat 7B
#   ./tools/run_host_llama.sh --llm 14b --no-embed
#   ./tools/run_host_llama.sh --ui gui        # listas osascript/zenity (legado)
#   ./tools/run_host_llama.sh --ui text       # menú TTY
#   ./tools/run_host_llama.sh --term xterm    # XQuartz (legado / --ui gui)
#   ./tools/run_host_llama.sh --term headless # nohup, sin ventana
#   ./tools/run_host_llama.sh --foreground    # hub en esta terminal
#   ./tools/run_host_llama.sh --stop          # para hub/spy/llama-server en los puertos tuide
#   ./tools/run_host_llama.sh -y              # autoelige GGUF y abre Inspección
set -euo pipefail

CHAT_PORT="${TUIDE_HOST_CHAT_PORT:-8080}"
EMBED_PORT="${TUIDE_HOST_EMBED_PORT:-18765}"
WEB_PORT="${TUIDE_HOST_WEB_PORT:-18767}"
NGL="${TUIDE_HOST_NGL:-99}"
CHAT_CTX="${TUIDE_HOST_CHAT_CTX:-32768}"
EMBED_CTX="${TUIDE_HOST_EMBED_CTX:-2048}"
EMBED_NP="${TUIDE_HOST_EMBED_NP:-8}"
EMBED_NGL="${TUIDE_HOST_EMBED_NGL:-0}"
FLASH_ATTN="${TUIDE_HOST_FLASH_ATTN:-on}"
CACHE_TYPE="${TUIDE_HOST_CACHE_TYPE:-q8_0}"
CHAT_NP="${TUIDE_HOST_NP:-1}"
DRAFT_N_MAX="${TUIDE_HOST_DRAFT_N_MAX:-16}"
DRAFT_MODE="${TUIDE_HOST_DRAFT:-auto}"
THINKING="${TUIDE_HOST_THINKING:-on}"

CACHE_ROOT="${XDG_CACHE_HOME:-${HOME}/.cache}/tuide/models"
L1_DIR="${CACHE_ROOT}/l1"
L2_DIR="${CACHE_ROOT}/l2"
EMBED_DIR="${CACHE_ROOT}/embed/intent"
RUNTIME_DIR="${CACHE_ROOT}/runtime"
LOG_DIR="${CACHE_ROOT}/host-llama"
TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "${LOG_DIR}"

NONE_LABEL="— no lanzar —"

die() {
  printf '[host-llama] error: %s\n' "$*" >&2
  exit 1
}

log() {
  printf '[host-llama] %s\n' "$*"
}

usage() {
  cat <<EOF
Uso: $(basename "$0") [opciones]

  --llm <id|ruta|none>   GGUF de chat (substring: 7b, 14b, 32b, 70b, o ruta)
  --embed [id|ruta]      GGUF de embeddings (sin valor = el primero / nomic)
  --no-llm               No levantar el servidor de chat
  --no-embed             No levantar el servidor de embeddings
  --ui hub|gui|text      hub HTML (default), listas nativas, o menú TTY
  --term auto|terminal|xterm|headless
                         Dónde corre el hub (Mac: Terminal.app por defecto)
  --no-spy               Sin proxy: no se ven tokens en la terminal
  --no-web               Sin hub HTML; picker legado y llama-server directo
  --foreground           Hub/servidores en esta terminal (no abre otra ventana)
  --stop                 Para hub, spy y llama-server si ocupan los puertos tuide
  -y, --yes              Autoelege (LLM más grande + embeddings) y abre Inspección
  -h, --help             Esta ayuda

Sin flags abre el hub HTML (Lanzamiento | Inspección) en una ventana WebKit
(sin pestañas ni barra de URL). TUIDE_HOST_BROWSER=safari|chrome|system para otro.
GGUF en:
  LLM:  ${L2_DIR}  y  ${L1_DIR}
  Embed: ${EMBED_DIR}

Variables: TUIDE_L2_GGUF, TUIDE_EMBED_GGUF, TUIDE_LLAMA_SERVER,
  TUIDE_HOST_CHAT_PORT, TUIDE_HOST_EMBED_PORT, TUIDE_HOST_WEB_PORT,
  TUIDE_HOST_NGL, TUIDE_HOST_EMBED_NGL, TUIDE_HOST_CHAT_CTX,
  TUIDE_HOST_FLASH_ATTN, TUIDE_HOST_CACHE_TYPE, TUIDE_HOST_THREADS,
  TUIDE_HOST_DRAFT, TUIDE_HOST_DRAFT_GGUF, TUIDE_HOST_THINKING,
  TUIDE_L2_API_MODEL,
  TUIDE_ADVERTISE_HOST, TUIDE_HOST_BROWSER (app|chrome|safari|system).
EOF
}

human_size() {
  awk -v b="$1" 'BEGIN {
    if (b >= 1073741824) printf "%.1fG", b / 1073741824
    else if (b >= 1048576) printf "%.0fM", b / 1048576
    else if (b >= 1024) printf "%.0fK", b / 1024
    else printf "%dB", b
  }'
}

file_size() {
  if [[ "$(uname -s)" == "Darwin" ]]; then
    stat -f%z "$1"
  else
    stat -c%s "$1"
  fi
}

perf_threads() {
  if [[ -n "${TUIDE_HOST_THREADS:-}" ]]; then
    printf '%s\n' "${TUIDE_HOST_THREADS}"
    return 0
  fi
  if [[ "$(uname -s)" == "Darwin" ]]; then
    local n
    n="$(sysctl -n hw.perflevel0.logicalcpu 2>/dev/null || true)"
    if [[ "${n}" =~ ^[1-9][0-9]*$ ]]; then
      printf '%s\n' "${n}"
      return 0
    fi
  fi
  if command -v nproc >/dev/null 2>&1; then
    nproc
    return 0
  fi
  printf '4\n'
}

find_default_draft_gguf() {
  if [[ -n "${TUIDE_HOST_DRAFT_GGUF:-}" && -f "${TUIDE_HOST_DRAFT_GGUF}" ]]; then
    printf '%s\n' "${TUIDE_HOST_DRAFT_GGUF}"
    return 0
  fi
  local f
  shopt -s nullglob
  for f in "${L1_DIR}"/*1.5b*.gguf "${L1_DIR}"/*1_5b*.gguf; do
    [[ -f "${f}" ]] || continue
    [[ "${f}" == *.partial ]] && continue
    printf '%s\n' "${f}"
    shopt -u nullglob
    return 0
  done
  shopt -u nullglob
  return 1
}

resolve_draft_gguf() {
  local chat="$1"
  local mode
  mode="$(printf '%s' "${DRAFT_MODE}" | tr '[:upper:]' '[:lower:]')"
  case "${mode}" in
    0|off|false|no|none) printf '\n'; return 0 ;;
  esac
  local draft=""
  draft="$(find_default_draft_gguf || true)"
  if [[ -z "${draft}" || -z "${chat}" ]]; then
    printf '\n'
    return 0
  fi
  if [[ "${draft}" == "${chat}" ]]; then
    printf '\n'
    return 0
  fi
  local base
  base="$(basename "${chat}" | tr '[:upper:]' '[:lower:]')"
  if [[ "${base}" == *1.5b* || "${base}" == *1_5b* || "${base}" == *0.5b* ]]; then
    printf '\n'
    return 0
  fi
  case "${mode}" in
    1|on|true|yes)
      printf '%s\n' "${draft}"
      return 0
      ;;
  esac
  local chat_sz draft_sz mem=0 needed
  chat_sz="$(file_size "${chat}")"
  draft_sz="$(file_size "${draft}")"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    mem="$(sysctl -n hw.memsize 2>/dev/null || true)"
  fi
  needed=$((chat_sz + draft_sz + chat_sz / 4 + 3 * 1024 * 1024 * 1024))
  if [[ "${mem}" =~ ^[1-9][0-9]*$ ]] && (( needed > mem * 3 / 4 )); then
    printf '\n'
    return 0
  fi
  printf '%s\n' "${draft}"
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

find_xterm() {
  local candidate
  for candidate in /opt/X11/bin/xterm /usr/X11/bin/xterm; do
    if [[ -x "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  if command -v xterm >/dev/null 2>&1; then
    command -v xterm
    return 0
  fi
  return 1
}

# Appends lines "size\trole\tpath" to stdout, largest first.
scan_ggufs() {
  local role="$1"
  local dir="$2"
  local f base
  [[ -d "${dir}" ]] || return 0
  shopt -s nullglob
  for f in "${dir}"/*.gguf; do
    base="$(basename "${f}")"
    [[ "${base}" == *.partial ]] && continue
    [[ "${base}" == *-00002-of-* ]] && continue
    printf '%s\t%s\t%s\n' "$(file_size "${f}")" "${role}" "${f}"
  done
  shopt -u nullglob
}

list_llm_rows() {
  { scan_ggufs L2 "${L2_DIR}"; scan_ggufs L1 "${L1_DIR}"; } | sort -nr
}

list_embed_rows() {
  scan_ggufs embed "${EMBED_DIR}" | sort -nr
}

print_menu() {
  local title="$1"
  local rows="$2"
  local i=1
  local size role path
  printf '\n%s\n' "${title}"
  if [[ -z "${rows}" ]]; then
    printf '  (ningún GGUF encontrado)\n'
    return 0
  fi
  while IFS=$'\t' read -r size role path; do
    [[ -n "${path}" ]] || continue
    printf '  %d) [%s] %s  (%s)\n' "${i}" "${role}" "$(basename "${path}")" "$(human_size "${size}")"
    i=$((i + 1))
  done <<<"${rows}"
  printf '  0) no lanzar\n'
}

nth_path() {
  local rows="$1"
  local n="$2"
  local i=1
  local size role path
  while IFS=$'\t' read -r size role path; do
    [[ -n "${path}" ]] || continue
    if [[ "${i}" -eq "${n}" ]]; then
      printf '%s\n' "${path}"
      return 0
    fi
    i=$((i + 1))
  done <<<"${rows}"
  return 1
}

row_count() {
  local rows="$1"
  if [[ -z "${rows}" ]]; then
    printf '0\n'
    return 0
  fi
  printf '%s\n' "${rows}" | grep -c .
}

preferred_llm_index() {
  local rows="$1"
  local i=1
  local size role path base
  local fallback=1
  while IFS=$'\t' read -r size role path; do
    [[ -n "${path}" ]] || continue
    base="$(basename "${path}")"
    if [[ "${base}" == *70b* || "${base}" == *70B* ]]; then
      printf '%s\n' "${i}"
      return 0
    fi
    i=$((i + 1))
  done <<<"${rows}"
  i=1
  while IFS=$'\t' read -r size role path; do
    [[ -n "${path}" ]] || continue
    base="$(basename "${path}")"
    if [[ "${base}" == *32b* || "${base}" == *32B* ]]; then
      printf '%s\n' "${i}"
      return 0
    fi
    i=$((i + 1))
  done <<<"${rows}"
  i=1
  while IFS=$'\t' read -r size role path; do
    [[ -n "${path}" ]] || continue
    base="$(basename "${path}")"
    if [[ "${base}" == *14b* || "${base}" == *14B* ]]; then
      printf '%s\n' "${i}"
      return 0
    fi
    i=$((i + 1))
  done <<<"${rows}"
  printf '%s\n' "${fallback}"
}

preferred_embed_index() {
  local rows="$1"
  local i=1
  local size role path base
  while IFS=$'\t' read -r size role path; do
    [[ -n "${path}" ]] || continue
    base="$(basename "${path}")"
    if [[ "${base}" == *nomic-embed* ]]; then
      printf '%s\n' "${i}"
      return 0
    fi
    i=$((i + 1))
  done <<<"${rows}"
  if [[ -n "${rows}" ]]; then
    printf '1\n'
  else
    printf '0\n'
  fi
}

resolve_token() {
  local kind="$1"
  local token="$2"
  local rows="$3"
  local size role path base matches=()
  token="$(printf '%s' "${token}" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
  if [[ -z "${token}" || "${token}" == "none" || "${token}" == "off" || "${token}" == "0" ]]; then
    printf '\n'
    return 0
  fi
  if [[ -f "${token}" ]]; then
    printf '%s\n' "${token}"
    return 0
  fi
  local lower
  lower="$(printf '%s' "${token}" | tr '[:upper:]' '[:lower:]')"
  while IFS=$'\t' read -r size role path; do
    [[ -n "${path}" ]] || continue
    base="$(basename "${path}")"
    if [[ "$(printf '%s' "${base}" | tr '[:upper:]' '[:lower:]')" == *"${lower}"* ]]; then
      matches+=("${path}")
    fi
  done <<<"${rows}"
  if [[ "${#matches[@]}" -eq 1 ]]; then
    printf '%s\n' "${matches[0]}"
    return 0
  fi
  if [[ "${#matches[@]}" -eq 0 ]]; then
    die "no hay GGUF ${kind} que coincida con '${token}'"
  fi
  die "varias coincidencias ${kind} para '${token}': ${matches[*]}"
}

read_choice() {
  local prompt="$1"
  local reply=""
  if [[ -r /dev/tty ]]; then
    read -r -p "${prompt}" reply </dev/tty || true
  elif [[ -t 0 ]]; then
    read -r -p "${prompt}" reply || true
  fi
  printf '%s\n' "${reply}"
}

can_prompt() {
  [[ -t 0 || -r /dev/tty ]]
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

as_quote() {
  local s="$1"
  s="${s//\\/\\\\}"
  s="${s//\"/\\\"}"
  printf '"%s"' "${s}"
}

row_label() {
  local size="$1"
  local role="$2"
  local path="$3"
  printf '[%s] %s  (%s)' "${role}" "$(basename "${path}")" "$(human_size "${size}")"
}

have_osascript_gui() {
  [[ "$(uname -s)" == "Darwin" ]] && command -v osascript >/dev/null 2>&1
}

have_zenity_gui() {
  command -v zenity >/dev/null 2>&1 && [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]
}

gui_choose_osascript() {
  local title="$1"
  local prompt="$2"
  local default="$3"
  shift 3
  local item as_list="" sep=""
  for item in "$@"; do
    as_list+="${sep}$(as_quote "${item}")"
    sep=", "
  done
  local result
  result="$(osascript <<EOF
set theOpts to {${as_list}}
set theDefault to {$(as_quote "${default}")}
try
  set theChoice to choose from list theOpts with title $(as_quote "${title}") with prompt $(as_quote "${prompt}") default items theDefault OK button name "Siguiente" cancel button name "Cancelar"
  if theChoice is false then
    return "CANCEL"
  end if
  return item 1 of theChoice as text
on error
  return "CANCEL"
end try
EOF
)"
  printf '%s\n' "${result}"
}

gui_choose_zenity() {
  local title="$1"
  local prompt="$2"
  local default="$3"
  shift 3
  local args=() item flag
  for item in "$@"; do
    if [[ "${item}" == "${default}" ]]; then
      flag=TRUE
    else
      flag=FALSE
    fi
    args+=("${flag}" "${item}")
  done
  zenity --list --radiolist --title "${title}" --text "${prompt}" \
    --ok-label "Siguiente" --cancel-label "Cancelar" \
    --width 640 --height 360 --column "" --column "Modelo" "${args[@]}"
}

# Prints selected path (empty = none). Exit 2 if cancelled.
gui_pick_path() {
  local kind="$1"
  local title="$2"
  local prompt="$3"
  local rows="$4"
  local default_index="$5"
  local labels=()
  local paths=()
  local size role path label i=1 default_label="${NONE_LABEL}"
  while IFS=$'\t' read -r size role path; do
    [[ -n "${path}" ]] || continue
    label="$(row_label "${size}" "${role}" "${path}")"
    labels+=("${label}")
    paths+=("${path}")
    if [[ "${i}" -eq "${default_index}" ]]; then
      default_label="${label}"
    fi
    i=$((i + 1))
  done <<<"${rows}"
  labels+=("${NONE_LABEL}")
  paths+=("")
  if [[ "${default_index}" == "0" || "${#labels[@]}" -eq 1 ]]; then
    default_label="${NONE_LABEL}"
  fi

  local chosen=""
  if have_osascript_gui; then
    chosen="$(gui_choose_osascript "${title}" "${prompt}" "${default_label}" "${labels[@]}")"
  elif have_zenity_gui; then
    chosen="$(gui_choose_zenity "${title}" "${prompt}" "${default_label}" "${labels[@]}")" || chosen="CANCEL"
  else
    die "no hay GUI (osascript/zenity); usa --ui text"
  fi
  if [[ "${chosen}" == "CANCEL" || -z "${chosen}" ]]; then
    return 2
  fi
  local idx
  for idx in "${!labels[@]}"; do
    if [[ "${labels[idx]}" == "${chosen}" ]]; then
      printf '%s\n' "${paths[idx]}"
      return 0
    fi
  done
  die "selección ${kind} no reconocida: ${chosen}"
}

write_session_script() {
  local dest="$1"
  local server="$2"
  local lib_dir="$3"
  local chat_gguf="$4"
  local embed_gguf="$5"
  local chat_alias="$6"
  local advertise="$7"
  local spy_py="$8"
  local spy_on="$9"
  local chat_backend="${10}"
  local embed_backend="${11}"
  umask 077
  cat >"${dest}" <<EOF
#!/usr/bin/env bash
# Generado por run_host_llama.sh — no editar a mano.
set -euo pipefail
export DYLD_LIBRARY_PATH=$(printf '%q' "${lib_dir}")\${DYLD_LIBRARY_PATH:+:\$DYLD_LIBRARY_PATH}
export LD_LIBRARY_PATH=$(printf '%q' "${lib_dir}")\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}
SERVER=$(printf '%q' "${server}")
CHAT_GGUF=$(printf '%q' "${chat_gguf}")
EMBED_GGUF=$(printf '%q' "${embed_gguf}")
CHAT_ALIAS=$(printf '%q' "${chat_alias}")
CHAT_PORT=$(printf '%q' "${CHAT_PORT}")
EMBED_PORT=$(printf '%q' "${EMBED_PORT}")
CHAT_BACKEND=$(printf '%q' "${chat_backend}")
EMBED_BACKEND=$(printf '%q' "${embed_backend}")
NGL=$(printf '%q' "${NGL}")
EMBED_NGL=$(printf '%q' "${EMBED_NGL}")
CHAT_CTX=$(printf '%q' "${CHAT_CTX}")
EMBED_CTX=$(printf '%q' "${EMBED_CTX}")
EMBED_NP=$(printf '%q' "${EMBED_NP}")
FLASH_ATTN=$(printf '%q' "${FLASH_ATTN}")
CACHE_TYPE=$(printf '%q' "${CACHE_TYPE}")
CHAT_NP=$(printf '%q' "${CHAT_NP}")
THREADS=$(printf '%q' "${THREADS}")
DRAFT_GGUF=$(printf '%q' "${DRAFT_GGUF}")
DRAFT_N_MAX=$(printf '%q' "${DRAFT_N_MAX}")
LOG_DIR=$(printf '%q' "${LOG_DIR}")
ADVERTISE=$(printf '%q' "${advertise}")
SPY_PY=$(printf '%q' "${spy_py}")
SPY_ON=$(printf '%q' "${spy_on}")
SPY_JSONL=$(printf '%q' "${LOG_DIR}/spy.jsonl")
WEB_ON=$(printf '%q' "${WEB_ON}")
WEB_LISTEN=$(printf '%q' "${WEB_LISTEN}")
WEB_URL=$(printf '%q' "${WEB_URL}")
mkdir -p "\${LOG_DIR}"
if [[ ! -x "\${SERVER}" ]] && ! command -v "\${SERVER}" >/dev/null 2>&1; then
  printf '[host-llama] no hay llama-server (%s)\\n' "\${SERVER}"
  printf 'Compílalo con Metal, ponlo en PATH o TUIDE_LLAMA_SERVER.\\n'
  printf 'Enter para cerrar.\\n'
  read -r _ </dev/tty || true
  exit 1
fi
PYTHON="\$(command -v python3 || true)"
if [[ "\${SPY_ON}" == "1" && ( -z "\${PYTHON}" || ! -f "\${SPY_PY}" ) ]]; then
  printf '[host-llama] spy desactivado (falta python3 o host_llama_spy.py)\\n'
  SPY_ON=0
fi
CHAT_BIND_HOST="0.0.0.0"
CHAT_BIND_PORT="\${CHAT_PORT}"
EMBED_BIND_HOST="0.0.0.0"
EMBED_BIND_PORT="\${EMBED_PORT}"
if [[ "\${SPY_ON}" == "1" ]]; then
  CHAT_BIND_HOST="127.0.0.1"
  CHAT_BIND_PORT="\${CHAT_BACKEND}"
  EMBED_BIND_HOST="127.0.0.1"
  EMBED_BIND_PORT="\${EMBED_BACKEND}"
fi
CHAT_PID=""
EMBED_PID=""
SPY_CHAT_PID=""
SPY_EMBED_PID=""
cleanup() {
  if [[ -n "\${SPY_CHAT_PID}" ]]; then kill "\${SPY_CHAT_PID}" 2>/dev/null || true; fi
  if [[ -n "\${SPY_EMBED_PID}" ]]; then kill "\${SPY_EMBED_PID}" 2>/dev/null || true; fi
  if [[ -n "\${CHAT_PID}" ]]; then kill "\${CHAT_PID}" 2>/dev/null || true; fi
  if [[ -n "\${EMBED_PID}" ]]; then kill "\${EMBED_PID}" 2>/dev/null || true; fi
}
trap cleanup EXIT INT TERM
wait_http() {
  local url="\$1"
  local i
  for i in \$(seq 1 90); do
    if curl -sf --max-time 1 "\$url" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.4
  done
  return 1
}
printf '[host-llama] llama-server: %s\\n' "\${SERVER}"
if [[ -n "\${CHAT_GGUF}" ]]; then
  CHAT_EXTRA=(-np "\${CHAT_NP}" -t "\${THREADS}" -tb "\${THREADS}" -fa "\${FLASH_ATTN}")
  if [[ -n "\${CACHE_TYPE}" && "\${CACHE_TYPE}" != "off" && "\${CACHE_TYPE}" != "0" && "\${CACHE_TYPE}" != "f16" && "\${CACHE_TYPE}" != "fp16" ]]; then
    CHAT_EXTRA+=(-ctk "\${CACHE_TYPE}" -ctv "\${CACHE_TYPE}")
  fi
  if [[ -n "\${DRAFT_GGUF}" ]]; then
    CHAT_EXTRA+=(-md "\${DRAFT_GGUF}" -ngld "\${NGL}" --spec-draft-n-max "\${DRAFT_N_MAX}")
    if [[ -n "\${CACHE_TYPE}" && "\${CACHE_TYPE}" != "off" && "\${CACHE_TYPE}" != "0" && "\${CACHE_TYPE}" != "f16" && "\${CACHE_TYPE}" != "fp16" ]]; then
      CHAT_EXTRA+=(-ctkd "\${CACHE_TYPE}" -ctvd "\${CACHE_TYPE}")
    fi
    printf '[host-llama] chat draft %s\\n' "\${DRAFT_GGUF}"
  else
    printf '[host-llama] chat sin draft (descarga Qwen2.5 1.5B L1 para speculative decoding)\\n'
  fi
  printf '[host-llama] chat llama-server %s:%s  %s\\n' "\${CHAT_BIND_HOST}" "\${CHAT_BIND_PORT}" "\${CHAT_GGUF}"
  "\${SERVER}" -m "\${CHAT_GGUF}" --host "\${CHAT_BIND_HOST}" --port "\${CHAT_BIND_PORT}" \\
    -ngl "\${NGL}" -c "\${CHAT_CTX}" --alias "\${CHAT_ALIAS}" --metrics "\${CHAT_EXTRA[@]}" \\
    >"\${LOG_DIR}/chat.log" 2>&1 &
  CHAT_PID=\$!
fi
if [[ -n "\${EMBED_GGUF}" ]]; then
  printf '[host-llama] embed llama-server %s:%s  %s\\n' "\${EMBED_BIND_HOST}" "\${EMBED_BIND_PORT}" "\${EMBED_GGUF}"
  "\${SERVER}" -m "\${EMBED_GGUF}" --host "\${EMBED_BIND_HOST}" --port "\${EMBED_BIND_PORT}" \\
    --embedding --pooling mean -c "\${EMBED_CTX}" -np "\${EMBED_NP}" -ngl "\${EMBED_NGL}" --metrics \\
    >"\${LOG_DIR}/embed.log" 2>&1 &
  EMBED_PID=\$!
fi
sleep 1
if [[ -n "\${CHAT_PID}" ]] && ! kill -0 "\${CHAT_PID}" 2>/dev/null; then
  printf '[host-llama] chat murió al arrancar. Ver %s\\n' "\${LOG_DIR}/chat.log"
  cat "\${LOG_DIR}/chat.log" 2>/dev/null || true
fi
if [[ -n "\${EMBED_PID}" ]] && ! kill -0 "\${EMBED_PID}" 2>/dev/null; then
  printf '[host-llama] embed murió al arrancar. Ver %s\\n' "\${LOG_DIR}/embed.log"
  cat "\${LOG_DIR}/embed.log" 2>/dev/null || true
fi
if [[ "\${SPY_ON}" == "1" ]]; then
  CHAT_WEB=""
  EMBED_WEB=""
  if [[ "\${WEB_ON}" == "1" ]]; then
    if [[ -n "\${CHAT_GGUF}" ]]; then
      CHAT_WEB="--web \${WEB_LISTEN}"
    elif [[ -n "\${EMBED_GGUF}" ]]; then
      EMBED_WEB="--web \${WEB_LISTEN}"
    fi
  fi
  if [[ -n "\${CHAT_GGUF}" ]]; then
    wait_http "http://127.0.0.1:\${CHAT_BACKEND}/health" || printf '[host-llama] chat /health aún no lista\\n'
    "\${PYTHON}" "\${SPY_PY}" --listen "0.0.0.0:\${CHAT_PORT}" --backend "127.0.0.1:\${CHAT_BACKEND}" --tag chat --jsonl "\${SPY_JSONL}" \${CHAT_WEB} &
    SPY_CHAT_PID=\$!
  fi
  if [[ -n "\${EMBED_GGUF}" ]]; then
    wait_http "http://127.0.0.1:\${EMBED_BACKEND}/health" || printf '[host-llama] embed /health aún no lista\\n'
    "\${PYTHON}" "\${SPY_PY}" --listen "0.0.0.0:\${EMBED_PORT}" --backend "127.0.0.1:\${EMBED_BACKEND}" --tag embed --jsonl "\${SPY_JSONL}" \${EMBED_WEB} &
    SPY_EMBED_PID=\$!
  fi
  printf '\\n[host-llama] en esta terminal verás el texto del modelo en vivo (la VM sigue por red).\\n'
  if [[ "\${WEB_ON}" == "1" && -n "\${CHAT_WEB}\${EMBED_WEB}" ]]; then
    printf '[host-llama] visor HTML: %s\\n' "\${WEB_URL}"
    if [[ "\$(uname -s)" == "Darwin" ]]; then
      (sleep 0.8; open "\${WEB_URL}") >/dev/null 2>&1 &
    fi
  fi
else
  if [[ -n "\${CHAT_PID}" ]]; then
    tail -n +1 -F "\${LOG_DIR}/chat.log" 2>/dev/null | awk '{print "[chat] " \$0; fflush()}' &
  fi
  if [[ -n "\${EMBED_PID}" ]]; then
    tail -n +1 -F "\${LOG_DIR}/embed.log" 2>/dev/null | awk '{print "[embed] " \$0; fflush()}' &
  fi
fi
printf '\\n[host-llama] VM Linux:\\n'
if [[ -n "\${CHAT_GGUF}" ]]; then
  printf '  export TUIDE_L2_API_BASE=http://%s:%s/v1\\n' "\${ADVERTISE}" "\${CHAT_PORT}"
  printf '  export TUIDE_L2_API_MODEL=%s\\n' "\${CHAT_ALIAS}"
fi
if [[ -n "\${EMBED_GGUF}" ]]; then
  printf '  export TUIDE_EMBED_HOST=%s\\n' "\${ADVERTISE}"
  printf '  export TUIDE_EMBED_PORT=%s\\n' "\${EMBED_PORT}"
fi
printf '\\nCtrl+C para parar los servidores.\\n\\n'
PIDS=()
[[ -n "\${CHAT_PID}" ]] && PIDS+=("\${CHAT_PID}")
[[ -n "\${EMBED_PID}" ]] && PIDS+=("\${EMBED_PID}")
[[ -n "\${SPY_CHAT_PID}" ]] && PIDS+=("\${SPY_CHAT_PID}")
[[ -n "\${SPY_EMBED_PID}" ]] && PIDS+=("\${SPY_EMBED_PID}")
if [[ "\${#PIDS[@]}" -gt 0 ]]; then
  wait "\${PIDS[@]}" || true
fi
printf '\\n[host-llama] terminado.\\n'
if [[ "\${TUIDE_HOST_HOLD:-1}" == "1" && -t 0 ]]; then
  printf 'Enter para cerrar.\\n'
  read -r _ || true
fi
EOF
  chmod +x "${dest}"
}

resolve_term_mode() {
  local mode="${TERM_MODE}"
  if [[ "${mode}" != "auto" ]]; then
    printf '%s\n' "${mode}"
    return 0
  fi
  if [[ "$(uname -s)" == "Darwin" ]] && [[ -d /System/Applications/Utilities/Terminal.app ]]; then
    printf 'terminal\n'
    return 0
  fi
  if [[ -n "${DISPLAY:-}" ]] && find_xterm >/dev/null; then
    printf 'xterm\n'
    return 0
  fi
  if command -v gnome-terminal >/dev/null 2>&1; then
    printf 'gnome\n'
    return 0
  fi
  printf 'headless\n'
}

open_in_xterm() {
  local session="$1"
  local xterm=""
  if [[ -z "${DISPLAY:-}" && -S /tmp/.X11-unix/X0 ]]; then
    export DISPLAY=:0
  fi
  xterm="$(find_xterm || true)"
  [[ -n "${xterm}" && -n "${DISPLAY:-}" ]] || return 1
  log "abriendo xterm: ${xterm}"
  "${xterm}" -title "tuide host-llama" -geometry 120x36+80+40 \
    -e /bin/bash "${session}" >/dev/null 2>&1 &
  disown $! 2>/dev/null || true
  return 0
}

open_background_terminal() {
  local session="$1"
  local mode
  mode="$(resolve_term_mode)"
  case "${mode}" in
    terminal)
      # Launch Services: instant if Terminal.app already running (no XQuartz).
      log "abriendo Terminal.app"
      open -a Terminal "${session}"
      ;;
    iterm)
      log "abriendo iTerm"
      if [[ -d /Applications/iTerm.app ]]; then
        open -a iTerm "${session}"
      else
        open -a iTerm.app "${session}"
      fi
      ;;
    xterm)
      open_in_xterm "${session}" || die "no hay xterm/DISPLAY. Prueba --term terminal."
      ;;
    gnome)
      gnome-terminal --title="tuide host-llama" -- /bin/bash "${session}" &
      disown $! 2>/dev/null || true
      ;;
    headless)
      log "headless (nohup, sin ventana). Logs en ${LOG_DIR}"
      TUIDE_HOST_HOLD=0 nohup /bin/bash "${session}" >/dev/null 2>&1 &
      disown $! 2>/dev/null || true
      ;;
    *)
      die "--term inválido: ${mode} (auto|terminal|iterm|xterm|headless)"
      ;;
  esac
}

print_vm_hint() {
  local advertise="$1"
  local chat_alias="$2"
  printf '\n[host-llama] listo (procesos en la otra terminal).\n\n'
  if [[ -n "${CHAT_GGUF}" ]]; then
    printf '  export TUIDE_L2_API_BASE=http://%s:%s/v1\n' "${advertise}" "${CHAT_PORT}"
    printf '  export TUIDE_L2_API_MODEL=%s\n' "${chat_alias}"
  fi
  if [[ -n "${EMBED_GGUF}" ]]; then
    printf '  export TUIDE_EMBED_HOST=%s\n' "${advertise}"
    printf '  export TUIDE_EMBED_PORT=%s\n' "${EMBED_PORT}"
  fi
  printf '\nLogs: %s\n' "${LOG_DIR}"
  if [[ "${SPY_ON}" -eq 1 && "${WEB_ON}" -eq 1 ]]; then
    printf 'Visor HTML: %s\n' "${WEB_URL}"
  fi
}

is_tuide_llama_cmd() {
  case "$1" in
    *host_llama_hub.py*|*host_llama_spy.py*|*llama-server*|*host-llama/hub.command*)
      return 0
      ;;
  esac
  return 1
}

listen_pids() {
  # pipefail: lsof sale 1 si el puerto está libre
  lsof -nP -t -iTCP:"$1" -sTCP:LISTEN 2>/dev/null | sort -u || true
}

proc_args() {
  ps -p "$1" -ww -o args= 2>/dev/null || true
}

write_stop_script() {
  local dest="${LOG_DIR}/stop.command"
  umask 077
  {
    printf '#!/usr/bin/env bash\n'
    printf 'set -euo pipefail\n'
    printf 'export TUIDE_HOST_CHAT_PORT=%q\n' "${CHAT_PORT}"
    printf 'export TUIDE_HOST_EMBED_PORT=%q\n' "${EMBED_PORT}"
    printf 'export TUIDE_HOST_WEB_PORT=%q\n' "${WEB_PORT}"
    printf 'exec %q --stop\n' "${TOOLS_DIR}/run_host_llama.sh"
  } >"${dest}"
  chmod +x "${dest}"
}

stop_host_stack() {
  local chat_backend=$((CHAT_PORT + 10000))
  local embed_backend=$((EMBED_PORT + 10000))
  local ports=( "${WEB_PORT}" "${CHAT_PORT}" "${EMBED_PORT}" "${chat_backend}" "${embed_backend}" )
  local port pid cmd line
  local ours="" skipped=0

  write_stop_script

  for port in "${ports[@]}"; do
    while IFS= read -r pid; do
      [[ -n "${pid}" ]] || continue
      cmd="$(proc_args "${pid}")"
      if is_tuide_llama_cmd "${cmd}"; then
        ours="${ours}${port} ${pid} ${cmd}"$'\n'
      else
        skipped=1
        log ":${port} pid ${pid} no es llama/hub/spy — no toco"
        if [[ -n "${cmd}" ]]; then
          log "  ${cmd}"
        fi
      fi
    done <<< "$(listen_pids "${port}")"
  done

  if [[ -z "${ours}" ]]; then
    if [[ "${skipped}" -eq 0 ]]; then
      log "nada que parar en :${WEB_PORT} :${CHAT_PORT} :${EMBED_PORT}"
    fi
    return 0
  fi

  # INT al hub primero: cleanup() para spy y llama-server hijos.
  while IFS= read -r line; do
    [[ -n "${line}" ]] || continue
    pid="$(printf '%s\n' "${line}" | awk '{print $2}')"
    cmd="$(printf '%s\n' "${line}" | sed 's/^[^ ]* [^ ]* //')"
    case "${cmd}" in
      *host_llama_hub.py*)
        log "SIGINT  pid ${pid}  ${cmd}"
        kill -INT "${pid}" 2>/dev/null || true
        ;;
    esac
  done <<< "${ours}"
  sleep 0.6

  ours=""
  for port in "${ports[@]}"; do
    while IFS= read -r pid; do
      [[ -n "${pid}" ]] || continue
      cmd="$(proc_args "${pid}")"
      if is_tuide_llama_cmd "${cmd}"; then
        ours="${ours}${port} ${pid} ${cmd}"$'\n'
      fi
    done <<< "$(listen_pids "${port}")"
  done

  while IFS= read -r line; do
    [[ -n "${line}" ]] || continue
    pid="$(printf '%s\n' "${line}" | awk '{print $2}')"
    cmd="$(printf '%s\n' "${line}" | sed 's/^[^ ]* [^ ]* //')"
    if kill -0 "${pid}" 2>/dev/null; then
      log "SIGTERM pid ${pid}  ${cmd}"
      kill -TERM "${pid}" 2>/dev/null || true
    fi
  done <<< "${ours}"
  sleep 0.5

  for port in "${ports[@]}"; do
    while IFS= read -r pid; do
      [[ -n "${pid}" ]] || continue
      cmd="$(proc_args "${pid}")"
      if is_tuide_llama_cmd "${cmd}"; then
        log "SIGKILL pid ${pid}  ${cmd}"
        kill -KILL "${pid}" 2>/dev/null || true
      fi
    done <<< "$(listen_pids "${port}")"
  done

  local busy=0
  for port in "${WEB_PORT}" "${CHAT_PORT}" "${EMBED_PORT}"; do
    while IFS= read -r pid; do
      [[ -n "${pid}" ]] || continue
      cmd="$(proc_args "${pid}")"
      if is_tuide_llama_cmd "${cmd}"; then
        log "sigue en :${port} pid ${pid}"
        busy=1
      fi
    done <<< "$(listen_pids "${port}")"
  done
  if [[ "${busy}" -eq 0 ]]; then
    log "puertos tuide libres (:${WEB_PORT} hub, :${CHAT_PORT} chat, :${EMBED_PORT} embed)"
  else
    die "no se pudieron liberar todos los puertos tuide"
  fi
}

write_hub_script() {
  local dest="$1"
  local py="$2"
  shift 2
  umask 077
  {
    printf '#!/usr/bin/env bash\n'
    printf 'set -euo pipefail\n'
    printf 'cd %q\n' "${TOOLS_DIR}"
    printf 'export TUIDE_HOST_CHAT_PORT=%q\n' "${CHAT_PORT}"
    printf 'export TUIDE_HOST_EMBED_PORT=%q\n' "${EMBED_PORT}"
    printf 'export TUIDE_HOST_WEB_PORT=%q\n' "${WEB_PORT}"
    printf 'export TUIDE_HOST_NGL=%q\n' "${NGL}"
    printf 'export TUIDE_HOST_EMBED_NGL=%q\n' "${EMBED_NGL}"
    printf 'export TUIDE_HOST_CHAT_CTX=%q\n' "${CHAT_CTX}"
    printf 'export TUIDE_HOST_EMBED_CTX=%q\n' "${EMBED_CTX}"
    printf 'export TUIDE_HOST_EMBED_NP=%q\n' "${EMBED_NP}"
    printf 'export TUIDE_HOST_FLASH_ATTN=%q\n' "${FLASH_ATTN}"
    printf 'export TUIDE_HOST_CACHE_TYPE=%q\n' "${CACHE_TYPE}"
    printf 'export TUIDE_HOST_THREADS=%q\n' "${THREADS}"
    printf 'export TUIDE_HOST_NP=%q\n' "${CHAT_NP}"
    printf 'export TUIDE_HOST_DRAFT=%q\n' "${DRAFT_MODE}"
    printf 'export TUIDE_HOST_DRAFT_N_MAX=%q\n' "${DRAFT_N_MAX}"
    printf 'export TUIDE_HOST_THINKING=%q\n' "${THINKING}"
    if [[ -n "${TUIDE_HOST_DRAFT_GGUF:-}" ]]; then
      printf 'export TUIDE_HOST_DRAFT_GGUF=%q\n' "${TUIDE_HOST_DRAFT_GGUF}"
    fi
    printf 'export TUIDE_HOST_SPY=%q\n' "${SPY_ON}"
    printf 'exec %q %q' "${py}" "${TOOLS_DIR}/host_llama_hub.py"
    local a
    for a in "$@"; do
      printf ' %q' "${a}"
    done
    printf '\n'
  } >"${dest}"
  chmod +x "${dest}"
}

run_hub() {
  local py
  py="$(command -v python3 || true)"
  [[ -n "${py}" ]] || die "hace falta python3 para el hub HTML"
  [[ -f "${TOOLS_DIR}/host_llama_hub.py" ]] || die "falta ${TOOLS_DIR}/host_llama_hub.py"
  if [[ "${TUIDE_HOST_SPY:-1}" == "0" ]]; then
    SPY_ON=0
  fi
  export TUIDE_HOST_SPY="${SPY_ON}"
  local args=(--listen "127.0.0.1:${WEB_PORT}" --open-browser)
  if [[ "${AUTO_YES}" -eq 1 ]]; then
    args+=(--autostart --mode inspect)
  else
    args+=(--mode launch)
  fi
  if [[ "${LLM_SET}" -eq 1 ]]; then
    args+=(--chat "${LLM_TOKEN:-none}")
  fi
  if [[ "${EMBED_SET}" -eq 1 ]]; then
    args+=(--embed "${EMBED_TOKEN:-none}")
  fi
  WEB_URL="http://127.0.0.1:${WEB_PORT}"
  export TUIDE_HOST_CHAT_PORT="${CHAT_PORT}"
  export TUIDE_HOST_EMBED_PORT="${EMBED_PORT}"
  export TUIDE_HOST_WEB_PORT="${WEB_PORT}"
  export TUIDE_HOST_NGL="${NGL}"
  export TUIDE_HOST_EMBED_NGL="${EMBED_NGL}"
  export TUIDE_HOST_CHAT_CTX="${CHAT_CTX}"
  export TUIDE_HOST_EMBED_CTX="${EMBED_CTX}"
  export TUIDE_HOST_EMBED_NP="${EMBED_NP}"
  export TUIDE_HOST_FLASH_ATTN="${FLASH_ATTN}"
  export TUIDE_HOST_CACHE_TYPE="${CACHE_TYPE}"
  export TUIDE_HOST_THREADS="${THREADS}"
  export TUIDE_HOST_NP="${CHAT_NP}"
  export TUIDE_HOST_DRAFT="${DRAFT_MODE}"
  export TUIDE_HOST_DRAFT_N_MAX="${DRAFT_N_MAX}"
  export TUIDE_HOST_THINKING="${THINKING}"
  export TUIDE_HOST_SPY="${SPY_ON}"
  log "hub HTML: ${WEB_URL}  (Lanzamiento | Inspección)"
  if [[ "${FOREGROUND}" -eq 1 ]]; then
    exec "${py}" "${TOOLS_DIR}/host_llama_hub.py" "${args[@]}"
  fi
  local session="${LOG_DIR}/hub.command"
  write_hub_script "${session}" "${py}" "${args[@]}"
  write_stop_script
  open_background_terminal "${session}"
  printf '\n[host-llama] hub en otra terminal.\n'
  printf '  UI: %s\n' "${WEB_URL}"
  printf '  stop: %s --stop\n' "${TOOLS_DIR}/run_host_llama.sh"
  printf '        o %s\n' "${LOG_DIR}/stop.command"
  printf 'Logs: %s\n' "${LOG_DIR}"
}

LLM_TOKEN=""
EMBED_TOKEN=""
LLM_SET=0
EMBED_SET=0
AUTO_YES=0
UI_MODE="auto"
FOREGROUND=0
STOP=0
TERM_MODE="${TUIDE_HOST_TERM:-auto}"
SPY_ON=1
WEB_ON=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    -y|--yes)
      AUTO_YES=1
      shift
      ;;
    --foreground)
      FOREGROUND=1
      shift
      ;;
    --stop)
      STOP=1
      shift
      ;;
    --no-spy)
      SPY_ON=0
      shift
      ;;
    --no-web)
      WEB_ON=0
      shift
      ;;
    --term)
      [[ $# -ge 2 ]] || die "--term requiere auto|terminal|iterm|xterm|headless"
      TERM_MODE="$2"
      shift 2
      ;;
    --ui)
      [[ $# -ge 2 ]] || die "--ui requiere hub, gui o text"
      UI_MODE="$2"
      shift 2
      ;;
    --no-llm)
      LLM_TOKEN="none"
      LLM_SET=1
      shift
      ;;
    --no-embed)
      EMBED_TOKEN="none"
      EMBED_SET=1
      shift
      ;;
    --llm)
      [[ $# -ge 2 ]] || die "--llm requiere un valor (id, ruta o none)"
      LLM_TOKEN="$2"
      LLM_SET=1
      shift 2
      ;;
    --embed)
      EMBED_SET=1
      if [[ $# -ge 2 && "$2" != -* ]]; then
        EMBED_TOKEN="$2"
        shift 2
      else
        EMBED_TOKEN=""
        shift
      fi
      ;;
    *)
      die "flag desconocida: $1 (prueba --help)"
      ;;
  esac
done

case "${UI_MODE}" in
  auto|hub|gui|text) ;;
  *) die "--ui debe ser auto, hub, gui o text" ;;
esac
case "${TERM_MODE}" in
  auto|terminal|iterm|xterm|headless|gnome) ;;
  *) die "--term debe ser auto|terminal|iterm|xterm|headless" ;;
esac

if [[ "${STOP}" -eq 1 ]]; then
  stop_host_stack
  exit 0
fi

if [[ "${LLM_SET}" -eq 0 && -n "${TUIDE_L2_GGUF:-}" ]]; then
  LLM_TOKEN="${TUIDE_L2_GGUF}"
  LLM_SET=1
fi
if [[ "${EMBED_SET}" -eq 0 && -n "${TUIDE_EMBED_GGUF:-}" ]]; then
  EMBED_TOKEN="${TUIDE_EMBED_GGUF}"
  EMBED_SET=1
fi

LLM_ROWS="$(list_llm_rows || true)"
EMBED_ROWS="$(list_embed_rows || true)"
LLM_N="$(row_count "${LLM_ROWS}")"
EMBED_N="$(row_count "${EMBED_ROWS}")"

THREADS="$(perf_threads)"

USE_HUB=0
if [[ "${WEB_ON}" -eq 1 && "${UI_MODE}" != "gui" && "${UI_MODE}" != "text" ]]; then
  if command -v python3 >/dev/null 2>&1 && [[ -f "${TOOLS_DIR}/host_llama_hub.py" ]]; then
    USE_HUB=1
  elif [[ "${UI_MODE}" == "hub" ]]; then
    die "hace falta python3 y tools/host_llama_hub.py"
  fi
fi
if [[ "${USE_HUB}" -eq 1 ]]; then
  run_hub
  exit 0
fi

HAVE_GUI=0
if have_osascript_gui || have_zenity_gui; then
  HAVE_GUI=1
fi

USE_GUI=0
USE_TEXT=0
if [[ "${AUTO_YES}" -eq 0 && ( "${LLM_SET}" -eq 0 || "${EMBED_SET}" -eq 0 ) ]]; then
  if [[ "${UI_MODE}" == "gui" ]]; then
    USE_GUI=1
  elif [[ "${UI_MODE}" == "text" ]]; then
    USE_TEXT=1
  elif [[ "${HAVE_GUI}" -eq 1 ]]; then
    USE_GUI=1
  elif can_prompt; then
    USE_TEXT=1
  fi
fi

CHAT_GGUF=""
EMBED_GGUF=""

pick_llm_from_menu() {
  print_menu "LLM (chat, puerto ${CHAT_PORT})" "${LLM_ROWS}"
  local default
  default="$(preferred_llm_index "${LLM_ROWS}")"
  if [[ "${LLM_N}" -eq 0 ]]; then
    default=0
  fi
  local choice
  choice="$(read_choice "LLM [${default}]: ")"
  choice="${choice:-${default}}"
  if [[ "${choice}" == "0" ]]; then
    CHAT_GGUF=""
    return 0
  fi
  [[ "${choice}" =~ ^[0-9]+$ ]] || die "opción LLM inválida: ${choice}"
  CHAT_GGUF="$(nth_path "${LLM_ROWS}" "${choice}")" || die "opción LLM fuera de rango: ${choice}"
}

pick_embed_from_menu() {
  print_menu "Embeddings (puerto ${EMBED_PORT})" "${EMBED_ROWS}"
  local default
  default="$(preferred_embed_index "${EMBED_ROWS}")"
  local choice
  choice="$(read_choice "Embeddings [${default}]: ")"
  choice="${choice:-${default}}"
  if [[ "${choice}" == "0" ]]; then
    EMBED_GGUF=""
    return 0
  fi
  [[ "${choice}" =~ ^[0-9]+$ ]] || die "opción embeddings inválida: ${choice}"
  EMBED_GGUF="$(nth_path "${EMBED_ROWS}" "${choice}")" || \
    die "opción embeddings fuera de rango: ${choice}"
}

if [[ "${USE_GUI}" -eq 1 ]]; then
  log "GUI de lanzamiento"
  if [[ "${LLM_SET}" -eq 1 ]]; then
    CHAT_GGUF="$(resolve_token llm "${LLM_TOKEN}" "${LLM_ROWS}")"
  else
    local_default="$(preferred_llm_index "${LLM_ROWS}")"
    if [[ "${LLM_N}" -eq 0 ]]; then
      local_default=0
    fi
    CHAT_GGUF="$(gui_pick_path llm "tuide · LLM" \
      "Modelo de chat (llama-server :${CHAT_PORT}). El 7B en descarga no aparece hasta que termine." \
      "${LLM_ROWS}" "${local_default}")" || die "cancelado"
  fi
  if [[ "${EMBED_SET}" -eq 1 ]]; then
    if [[ -z "${EMBED_TOKEN}" ]]; then
      EMBED_GGUF="$(nth_path "${EMBED_ROWS}" "$(preferred_embed_index "${EMBED_ROWS}")" || true)"
    else
      EMBED_GGUF="$(resolve_token embed "${EMBED_TOKEN}" "${EMBED_ROWS}")"
    fi
  else
    EMBED_GGUF="$(gui_pick_path embed "tuide · embeddings" \
      "Modelo de embeddings (llama-server :${EMBED_PORT})." \
      "${EMBED_ROWS}" "$(preferred_embed_index "${EMBED_ROWS}")")" || die "cancelado"
  fi
elif [[ "${USE_TEXT}" -eq 1 ]]; then
  printf '\n[host-llama] elige qué servidores levantar\n'
  printf '  cache: %s\n' "${CACHE_ROOT}"
  if [[ "${LLM_SET}" -eq 1 ]]; then
    CHAT_GGUF="$(resolve_token llm "${LLM_TOKEN}" "${LLM_ROWS}")"
  else
    pick_llm_from_menu
  fi
  if [[ "${EMBED_SET}" -eq 1 ]]; then
    if [[ -z "${EMBED_TOKEN}" ]]; then
      EMBED_GGUF="$(nth_path "${EMBED_ROWS}" "$(preferred_embed_index "${EMBED_ROWS}")" || true)"
    else
      EMBED_GGUF="$(resolve_token embed "${EMBED_TOKEN}" "${EMBED_ROWS}")"
    fi
  else
    pick_embed_from_menu
  fi
else
  if [[ "${LLM_SET}" -eq 1 ]]; then
    CHAT_GGUF="$(resolve_token llm "${LLM_TOKEN}" "${LLM_ROWS}")"
  elif [[ "${LLM_N}" -gt 0 ]]; then
    CHAT_GGUF="$(nth_path "${LLM_ROWS}" "$(preferred_llm_index "${LLM_ROWS}")")"
  fi
  if [[ "${EMBED_SET}" -eq 1 ]]; then
    if [[ -z "${EMBED_TOKEN}" ]]; then
      EMBED_GGUF="$(nth_path "${EMBED_ROWS}" "$(preferred_embed_index "${EMBED_ROWS}")" || true)"
    else
      EMBED_GGUF="$(resolve_token embed "${EMBED_TOKEN}" "${EMBED_ROWS}")"
    fi
  elif [[ "${EMBED_N}" -gt 0 ]]; then
    EMBED_GGUF="$(nth_path "${EMBED_ROWS}" "$(preferred_embed_index "${EMBED_ROWS}")")"
  fi
fi

if [[ -z "${CHAT_GGUF}" && -z "${EMBED_GGUF}" ]]; then
  die "nada que lanzar. Pon GGUF en ${L2_DIR} / ${EMBED_DIR} o elige otra opción."
fi

SERVER="$(find_llama_server || true)"
if [[ -z "${SERVER}" ]]; then
  log "aviso: no hay llama-server en PATH/cache; la xterm lo dirá al arrancar"
  SERVER="llama-server"
fi
CHAT_ALIAS="${TUIDE_L2_API_MODEL:-}"
if [[ -n "${CHAT_GGUF}" && -z "${CHAT_ALIAS}" ]]; then
  CHAT_ALIAS="$(alias_from_gguf "${CHAT_GGUF}")"
fi
ADVERTISE="$(lan_ip)"
LIB_DIR="$(cd "$(dirname "${SERVER}")" && pwd)"

CHAT_BACKEND="${TUIDE_HOST_CHAT_BACKEND:-$((CHAT_PORT + 10000))}"
EMBED_BACKEND="${TUIDE_HOST_EMBED_BACKEND:-$((EMBED_PORT + 10000))}"
SPY_PY="${TOOLS_DIR}/host_llama_spy.py"
if [[ -f "${SPY_PY}" ]]; then
  cp "${SPY_PY}" "${LOG_DIR}/host_llama_spy.py"
else
  SPY_PY="${LOG_DIR}/host_llama_spy.py"
fi
if [[ "${TUIDE_HOST_SPY:-1}" == "0" ]]; then
  SPY_ON=0
fi
if [[ "${TUIDE_HOST_WEB:-1}" == "0" || "${SPY_ON}" -eq 0 ]]; then
  WEB_ON=0
fi
WEB_LISTEN="127.0.0.1:${WEB_PORT}"
WEB_URL="http://${WEB_LISTEN}"

DRAFT_GGUF="$(resolve_draft_gguf "${CHAT_GGUF}")"
if [[ -n "${CHAT_GGUF}" && -n "${DRAFT_GGUF}" ]]; then
  log "draft: ${DRAFT_GGUF}"
elif [[ -n "${CHAT_GGUF}" ]]; then
  log "sin draft (descarga Qwen2.5 1.5B L1 o TUIDE_HOST_DRAFT_GGUF)"
fi

SESSION="${LOG_DIR}/session.command"
write_session_script "${SESSION}" "${SERVER}" "${LIB_DIR}" "${CHAT_GGUF}" \
  "${EMBED_GGUF}" "${CHAT_ALIAS}" "${ADVERTISE}" \
  "${SPY_PY}" "${SPY_ON}" "${CHAT_BACKEND}" "${EMBED_BACKEND}"

if [[ "${FOREGROUND}" -eq 1 ]]; then
  log "foreground (spy=$([[ ${SPY_ON} -eq 1 ]] && echo on || echo off))"
  exec /bin/bash "${SESSION}"
fi

open_background_terminal "${SESSION}"
print_vm_hint "${ADVERTISE}" "${CHAT_ALIAS}"
if [[ "${SPY_ON}" -eq 1 ]]; then
  log "en Terminal.app verás tokens del modelo en vivo (la VM sigue por HTTP)"
fi
if [[ "${WEB_ON}" -eq 1 ]]; then
  log "visor HTML: ${WEB_URL}  (JSONL ${LOG_DIR}/spy.jsonl)"
fi
log "sesión: ${SESSION}"
