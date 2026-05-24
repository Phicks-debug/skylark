#!/bin/bash
# install.sh — Install bb (Tiny-Habibi) on Linux and macOS
# Usage: curl -sSL https://raw.githubusercontent.com/Phicks-debug/tiny-habibi/main/install.sh | bash
#   or:  bash install.sh --repo Phicks-debug/tiny-habibi

set -e

REPO_URL_BASE=
BIN_DIR=${HOME}/.local/bin
INSTALL_DIR=${BIN_DIR}/bb

while [[ $# -gt 0 ]]; do
  case $1 in
    --repo)
      REPO_URL_BASE=https://github.com/$2/releases/latest
      shift 2 ;;
    --force)   FORCE=true; shift ;;
    --keep-tmp) KEEP_TMP=true; shift ;;
    --help)
      echo -e 'Usage: install.sh [OPTIONS]\n  --repo USER/REPO   GitHub repo (default: Phicks-debug/tiny-habibi)\n  --force           Overwrite existing binary\n  --keep-tmp        Keep downloaded tarball\n  --help            Show this help'
      exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

REPO_URL_BASE=${REPO_URL_BASE:-https://github.com/Phicks-debug/tiny-habibi/releases/latest}
PLATFORM=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

case "${PLATFORM}-${ARCH}" in
  linux-x86_64)   TARBALL=tiny-habibi-linux-x64.tar.gz ;;
  linux-aarch64)  TARBALL=tiny-habibi-linux-arm64.tar.gz ;;
  darwin-x86_64)  TARBALL=tiny-habibi-darwin-x64.tar.gz ;;
  darwin-arm64)   TARBALL=tiny-habibi-darwin-arm64.tar.gz ;;
  *) echo "Unsupported: ${PLATFORM}-${ARCH}"; exit 1 ;;
esac

check_litert() {
  if ! python3 -c "import litert_lm" 2>/dev/null; then
    echo "Warning: litert-lm not installed (pip install litert-lm)"
  fi
}

if [[ -f ${INSTALL_DIR} && ${FORCE:-} != true ]]; then
  echo "Already installed at ${INSTALL_DIR} (use --force to overwrite)"
  exit 0
fi

echo "Installing bb (Tiny-Habibi) (${PLATFORM}-${ARCH})..."
check_litert
mkdir -p ${BIN_DIR}
TMPDIR=$(mktemp -d)
TARBALL_PATH=${TMPDIR}/${TARBALL}

echo "Downloading from GitHub Releases..."
# GitHub Releases store assets at: /releases/download/<tag>/<filename>
curl -L --retry 3 -o ${TARBALL_PATH} "${REPO_URL_BASE}/download/${TARBALL}"

echo "Extracting..."
tar -xzf ${TARBALL_PATH} -C ${BIN_DIR}
rm -rf ${TMPDIR}

if [[ -f ${INSTALL_DIR} ]]; then
  echo -e "\n✓ Installed to ${INSTALL_DIR}"
  echo "  Add to PATH: export PATH=\"${BIN_DIR}:\$PATH\""
  if [[ ${PLATFORM} == linux ]]; then
    echo -e "\nNote: Also run: sudo apt install libportaudio2"
  fi
else
  echo "Error: install failed"
  exit 1
fi
