#!/usr/bin/env bash
#
# Instala kateantigravity a nivel de sistema (/usr).
#
# Uso RECOMENDADO (ejecutar como tu usuario normal, SIN sudo):
#   ./scripts/install_system.sh
#
# El script compila como tu usuario (así build/ no queda con archivos de root)
# y solo eleva privilegios con sudo para el paso final de instalación en /usr.
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

# --- Si nos ejecutaron con sudo, reejecutar la compilación como el usuario real ---
# Esto evita que build/ quede con archivos propiedad de root (que luego impiden
# recompilar sin sudo). Solo la instalación necesita privilegios.
if [ "${EUID}" -eq 0 ]; then
    if [ -n "${SUDO_USER}" ] && [ "${SUDO_USER}" != "root" ]; then
        echo "==> Detectado sudo. Recompilando como '${SUDO_USER}' para no dejar archivos de root en build/..."
        # Reejecuta este mismo script como el usuario original; el sudo interno
        # de la fase de instalación pedirá la contraseña si hace falta.
        exec sudo -u "${SUDO_USER}" --preserve-env=PATH bash "${BASH_SOURCE[0]}" "$@"
    else
        echo "⚠️  Ejecuta este script como tu usuario normal (sin sudo):"
        echo "      ./scripts/install_system.sh"
        echo "    El script pedirá la contraseña de sudo solo para instalar en /usr."
        exit 1
    fi
fi

echo "==> Compilando kateantigravity para instalación global (como '$(whoami)')..."
cmake -B "${BUILD_DIR}" -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release "${ROOT_DIR}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "==> Instalando plugin a nivel de sistema (requiere sudo)..."
sudo cmake --install "${BUILD_DIR}"

echo ""
echo "✅ Instalación a nivel de sistema completada en /usr/lib/..."
echo "   (build/ pertenece a tu usuario; puedes recompilar sin sudo)"
