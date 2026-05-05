#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# tests/roundtrip.sh — Teste de correctude do refactor de batching.
#
# Verifica que o conteúdo lido após uma escrita é byte-a-byte igual ao
# escrito. Compara md5 do ficheiro de origem (fora do FUSE) com md5 do
# ficheiro escrito/lido através do FUSE.
#
# Pré-requisito: o FUSE está montado em /mnt/fs (via fuse.sh em terminal
# separado ou em background).
#
# Uso:
#   ./tests/roundtrip.sh             # 8 MiB por defeito
#   ./tests/roundtrip.sh 64           # 64 MiB
# -----------------------------------------------------------------------------
set -euo pipefail

MOUNT="/mnt/fs"
SIZE_MB="${1:-8}"

if [[ ! -d "${MOUNT}" ]]; then
  echo "ERRO: ${MOUNT} não existe. Montar o FUSE primeiro com sudo ./fuse.sh." >&2
  exit 1
fi

SRC="$(mktemp /tmp/roundtrip.src.XXXXXX)"
DST="${MOUNT}/roundtrip_test_$$.bin"

cleanup() {
  rm -f "${SRC}"
  rm -f "${DST}" 2>/dev/null || true
}
trap cleanup EXIT

# 1. Gerar conteúdo aleatório de SIZE_MB MiB.
dd if=/dev/urandom of="${SRC}" bs=1M count="${SIZE_MB}" status=none

# 2. Copiar para o ponto de montagem (passa pelo FUSE → write_dedup).
cp "${SRC}" "${DST}"

# 3. Forçar flush ao kernel — embora o FUSE esteja em direct_io, garantimos
#    que não há buffering pendente no userspace.
sync

# 4. Comparar md5 do original com o que foi escrito (e lido de volta) via FUSE.
md5_src="$(md5sum "${SRC}" | awk '{print $1}')"
md5_dst="$(md5sum "${DST}" | awk '{print $1}')"

if [[ "${md5_src}" == "${md5_dst}" ]]; then
  echo "PASS round-trip ${SIZE_MB} MiB (md5=${md5_src})"
  exit 0
else
  echo "FAIL round-trip ${SIZE_MB} MiB" >&2
  echo "  src md5: ${md5_src}" >&2
  echo "  dst md5: ${md5_dst}" >&2
  exit 1
fi
