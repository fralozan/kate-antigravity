#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

echo "==> Compilando kateantigravity en modo Release..."
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release "${ROOT_DIR}"
cmake --build "${BUILD_DIR}" -j$(nproc)

PLUGIN_SO="${BUILD_DIR}/bin/kf6/ktexteditor/kateantigravity.so"
PLUGIN_JSON="${ROOT_DIR}/kateantigravity.json"
UI_RC="${ROOT_DIR}/src/ui.rc"

# Destinos locales de usuario para Qt6 / KF6
TARGET_PLUGIN_DIR1="${HOME}/.local/lib/x86_64-linux-gnu/qt6/plugins/kf6/ktexteditor"
TARGET_PLUGIN_DIR2="${HOME}/.local/lib/qt6/plugins/kf6/ktexteditor"
TARGET_KXMLGUI_DIR="${HOME}/.local/share/kxmlgui5/kateantigravity"
TARGET_JSON_DIR="${HOME}/.local/share/ktexteditor/plugins"

echo "==> Instalando plugin a nivel de usuario en ~/.local..."
mkdir -p "${TARGET_PLUGIN_DIR1}"
mkdir -p "${TARGET_PLUGIN_DIR2}"
mkdir -p "${TARGET_KXMLGUI_DIR}"
mkdir -p "${TARGET_JSON_DIR}"

cp -v "${PLUGIN_SO}" "${TARGET_PLUGIN_DIR1}/kateantigravity.so"
cp -v "${PLUGIN_SO}" "${TARGET_PLUGIN_DIR2}/kateantigravity.so"
cp -v "${PLUGIN_JSON}" "${TARGET_JSON_DIR}/kateantigravity.json"
cp -v "${UI_RC}" "${TARGET_KXMLGUI_DIR}/ui.rc"

# Instalar catálogos de traducción
if [ -d "${BUILD_DIR}/locale" ]; then
    echo "==> Instalando traducciones en ~/.local/share/locale..."
    mkdir -p "${HOME}/.local/share/locale"
    cp -rv "${BUILD_DIR}/locale"/* "${HOME}/.local/share/locale/"
fi

# Configurar variable de entorno para sesiones de KDE / systemd
ENV_D="${HOME}/.config/environment.d"
mkdir -p "${ENV_D}"
cat << 'EOF' > "${ENV_D}/10-kate-qt-plugins.conf"
QT_PLUGIN_PATH=${HOME}/.local/lib/x86_64-linux-gnu/qt6/plugins:${HOME}/.local/lib/qt6/plugins:${QT_PLUGIN_PATH}
EOF
echo "==> Configurado ${ENV_D}/10-kate-qt-plugins.conf"

# Configurar alias/export en ~/.profile y ~/.bashrc si no existe
if ! grep -q "QT_PLUGIN_PATH.*qt6/plugins" "${HOME}/.profile" 2>/dev/null; then
    echo 'export QT_PLUGIN_PATH="${HOME}/.local/lib/x86_64-linux-gnu/qt6/plugins:${HOME}/.local/lib/qt6/plugins:${QT_PLUGIN_PATH}"' >> "${HOME}/.profile"
fi
if ! grep -q "QT_PLUGIN_PATH.*qt6/plugins" "${HOME}/.bashrc" 2>/dev/null; then
    echo 'export QT_PLUGIN_PATH="${HOME}/.local/lib/x86_64-linux-gnu/qt6/plugins:${HOME}/.local/lib/qt6/plugins:${QT_PLUGIN_PATH}"' >> "${HOME}/.bashrc"
fi

echo ""
echo "✅ Instalación local completada con éxito."
echo ""
echo "IMPORTANTE:"
echo "Para que Kate detecte los plugins locales en esta terminal, ejecuta:"
echo "  export QT_PLUGIN_PATH=\"${TARGET_PLUGIN_DIR1}/../../..:\${QT_PLUGIN_PATH}\""
echo "  kate"
echo ""
echo "O bien, si prefieres instalarlo globalmente en el sistema (exactamente como katecopilot):"
echo "  sudo ./scripts/install_system.sh"
