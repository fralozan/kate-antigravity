# Kate Antigravity 🚀

[![KDE Frameworks 6](https://img.shields.io/badge/KDE%20Frameworks-6-31a8ff?logo=kde&logoColor=white)](https://develop.kde.org/frameworks/)
[![Qt 6](https://img.shields.io/badge/Qt-6.5+-41cd52?logo=qt&logoColor=white)](https://www.qt.io/)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=white)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![GitHub release](https://img.shields.io/badge/release-v1.0.0-orange.svg)](https://github.com/fralozan/kate-antigravity/releases)

Complemento nativo de autocompletado inteligente de código con **Ghost Text** (texto fantasma estilo GitHub Copilot / Supermaven) y **Panel Lateral de Chat Asistente** con soporte de proyectos para el editor **Kate** y **KTextEditor**, desarrollado en **C++20 / Qt 6 / KDE Frameworks 6** e impulsado por **Google Antigravity (AGY)** y los modelos **Gemini / Claude / GPT**.

*Native AI-powered inline code completion plugin with Ghost Text and Chat sidebar for the Kate text editor, powered by Google Antigravity and Gemini / Claude / GPT models.*

---

## 📑 Tabla de Contenidos / Table of Contents

- [Características Principales](#-características-principales)
- [Requisitos del Sistema](#-requisitos-del-sistema)
- [Instalación Rápida](#-instalación-rápida)
  - [Opción 1: Instalación en el Sistema (Recomendada)](#opción-1-instalación-en-el-sistema-recomendada)
  - [Opción 2: Instalación de Usuario Local](#opción-2-instalación-de-usuario-local)
- [Atajos de Teclado](#-atajos-de-teclado)
- [Configuración en Kate](#-configuración-en-kate)
  - [Modo 1: CLI Persistente de Antigravity (Local)](#modo-1-cli-persistente-de-antigravity-local)
  - [Modo 2: Conexión Directa REST API de Gemini](#modo-2-conexión-directa-rest-api-de-gemini)
- [Reglas de Proyecto (.antigravity)](#-reglas-de-proyecto-antigravity)
- [Arquitectura y Motor de Contexto](#-arquitectura-y-motor-de-contexto)
- [Compilación y Tests Unitarios](#-compilación-y-tests-unitarios)
- [Estructura del Proyecto](#-estructura-del-proyecto)
- [Licencia](#-licencia)

---

## ✨ Características Principales

* 👻 **Ghost Text Nativo con `KTextEditor::InlineNoteProvider`:**
  Renderizado atenuado e integrado directamente en el flujo del búfer de texto. **No** altera el contenido del documento ni ensucia el historial de *Deshacer/Rehacer* hasta que decides aceptar la sugerencia.
* ⌨️ **Interacción de Teclado Fluida:**
  * <kbd>Tab</kbd>: Acepta e inserta la sugerencia completa en el editor.
  * <kbd>Esc</kbd>: Descarta y oculta la sugerencia inmediatamente.
  * **Typing-Through:** Si continuas escribiendo la misma letra predicha, el texto fantasma avanza carácter a carácter sin parpadeos ni peticiones de red redundantes.
  * <kbd>Alt</kbd> + <kbd>\</kbd>: Fuerza la generación manual de una sugerencia en la posición actual del cursor.
* 💬 **Panel Lateral de Chat Nativo e Inteligente:**
  * Conversación interactiva multi-turno con streaming de respuestas **incremental** (solo se re-renderiza el mensaje en curso) para máxima fluidez.
  * Selector de modelos en 1 clic en la barra del chat (`gemini-3.8-flash-low`, `gemini-2.5-pro`, `claude-sonnet-4-6`, `gpt-4o`, etc.).
  * **Búsqueda en el historial** de la conversación con contador de coincidencias.
  * **Limpiar chat** persistente por espacio (el ocultado se conserva entre cambios de espacio y reinicios) y **borrado permanente** que conserva el contexto del agente, con botones nativos de Kate.
  * Insignia de cuenta de Google AI con visualización de cuotas y límites.
* 🗂️ **Espacios de Trabajo con Conversaciones de Antigravity:**
  * Cada espacio de trabajo del panel queda ligado a una **conversación real de `agy`** (`--conversation <id>`): al seleccionarlo, se reanuda su contexto, sin necesidad de cambiar de carpeta manualmente.
  * Los archivos del proyecto se exponen al agente con `--add-dir`, así funciona **aunque no haya archivos abiertos** en el editor.
  * **Auto-cambio** al proyecto del archivo activo; **anclaje** para fijar un espacio; y gestión completa (añadir por selector de carpeta, renombrar, quitar) desde el botón de acciones (⋯) o el clic derecho.
  * Recuerda el último espacio activo entre reinicios.
* 🩹 **Aplicar Cambios como Diff (buscar/reemplazar):**
  * El asistente puede proponer ediciones en bloques `agy-edit` (`SEARCH`/`REPLACE`); un diálogo de revisión muestra el *diff* por bloque y permite aplicarlos de forma **selectiva** sobre los documentos abiertos.
  * Localización tolerante: coincidencia exacta y, como respaldo, insensible a espacios.
* 🔀 **Integración con Git:**
  * `/commit`: genera un mensaje de commit (estilo Conventional Commits) a partir del *diff staged* y crea el commit **solo tras tu confirmación** en un diálogo editable. Respeta los hooks; nunca hace `add`, `push` ni `amend`.
* 📁 **Project Awareness & Multi-sesión:**
  * Detección automática del proyecto activo a partir del documento actual (Git, CMake, Cargo, NPM, Gradle, etc.).
  * Gestión automática de sesiones separadas por proyecto con persistencia transparente en disco JSON.
  * **Reglas de proyecto** vía archivo `.antigravity` en la raíz, inyectadas en el *system prompt*.
* 📎 **Menciones `@` con Autocompletado Instantáneo:**
  * `@archivo` y `@archivo:20-45`: archivos del proyecto (indexados en segundo plano) con soporte de **rango de líneas**.
  * `@carpeta/`: inclusión contextual del árbol de un directorio.
  * `@selection`: la selección activa del editor.
  * `@git-diff`: cambios sin commitear (staged y unstaged), en solo lectura.
  * `@symbol:Nombre`: definición de un símbolo localizada por parseo ligero (C/C++, Python, JS/TS, Rust, Go, Java).
  * `@diagnostics`: errores y advertencias del LSP/marcas del compilador activo.
  * `@terminal`: captura *best-effort* de la salida del terminal embebido (limitada por la API de Kate).
  * **Enriquecimiento automático:** los identificadores del prompt que resuelven a símbolos del proyecto se adjuntan al contexto sin necesidad de `@symbol`.
* ⚡ **Comandos Slash `/` y Diálogos Nativos KDE:**
  * `/help`, `/usage`, `/project`: respuestas en ventanas nativas de KDE (`QDialog`) integradas con Breeze.
  * `/model [nombre]`, `/files`, `/clear`, `/reset`, `/export`, `/commit`.
  * `/usage` incluye ahora **estadísticas históricas de tokens** (acumulado y uso diario reciente).
* 👻 **Completado Inline Avanzado:**
  * **Debounce adaptativo** (más rápido tras aceptar, más lento en ráfagas de tecleo).
  * **Ciclado de alternativas** con <kbd>Alt</kbd>+<kbd>]</kbd> / <kbd>Alt</kbd>+<kbd>[</kbd>.
  * **Preset de modelo por tarea**: un modelo rápido para el ghost text y otro (más potente) para el chat.
* 🖼️ **Entrada Multimodal (Gemini):**
  * Adjunta imágenes al chat (backend REST directo de Gemini) enviadas como `inline_data`.
* 🔒 **Seguridad y KWallet:**
  * Almacenamiento seguro cifrado en reposo mediante **KWallet** con migración automática desde KConfig y fallback en entornos headless.
  * Claves de API enviadas mediante cabeceras seguras (`x-goog-api-key`).
  * Las menciones `@` no pueden escapar del árbol del proyecto (contención de rutas).
* 🌐 **Soporte Bilingüe Nativo:**
  * Interfaz y mensajes completamente internacionalizados en Inglés y Español con `KI18n`.

---

## 📋 Requisitos del Sistema

* **Kate / KTextEditor** >= 24.0 / 26.0 (KDE Frameworks 6)
* **Qt 6** (`Core`, `Gui`, `Widgets`, `Network`, `Test`) >= 6.5
* **KDE Frameworks 6 (KF6)** (`TextEditor`, `CoreAddons`, `I18n`, `XmlGui`, `ConfigCore`, `Wallet`)
* **Extra CMake Modules (ECM)**
* **Google Antigravity CLI (`agy`)** instalado y autenticado (para el modo CLI).

### Instalación de dependencias por distribución

#### Ubuntu / Debian / KDE Neon:
```bash
sudo apt update
sudo apt install git build-essential cmake extra-cmake-modules \
    qt6-base-dev libqt6network6 \
    libkf6texteditor-dev libkf6coreaddons-dev libkf6i18n-dev \
    libkf6xmlgui-dev libkf6config-dev libkf6wallet-dev
```

#### Fedora:
```bash
sudo dnf install git gcc-c++ cmake extra-cmake-modules \
    qt6-qtbase-devel \
    kf6-ktexteditor-devel kf6-kcoreaddons-devel kf6-ki18n-devel \
    kf6-kxmlgui-devel kf6-kconfig-devel kf6-kwallet-devel
```

#### Arch Linux:
```bash
sudo pacman -S base-devel cmake extra-cmake-modules \
    qt6-base ktexteditor kcoreaddons ki18n kxmlgui kconfig kwallet
```

---

## 📦 Instalación Rápida

Clona el repositorio:

```bash
git clone https://github.com/fralozan/kate-antigravity.git
cd kate-antigravity
```

### Opción 1: Instalación en el Sistema (Recomendada)

En muchas distribuciones (como KDE Neon, Kubuntu o Debian), Kate busca complementos únicamente en el directorio del sistema `/usr/lib/x86_64-linux-gnu/qt6/plugins/kf6/ktexteditor/`. La instalación global asegura que Kate lo reconozca de inmediato sin modificar variables de entorno:

```bash
sudo ./scripts/install_system.sh
```

### Opción 2: Instalación de Usuario Local

Si prefieres compilar e instalar en tu directorio de usuario (`~/.local`):

```bash
./scripts/install_user.sh
```

*Nota: Para que Kate detecte los plugins instalados en `~/.local` en esa sesión, ejecuta:*
```bash
export QT_PLUGIN_PATH="${HOME}/.local/lib/x86_64-linux-gnu/qt6/plugins:${QT_PLUGIN_PATH}"
kate
```
*(El instalador local también deja configurado `~/.config/environment.d/10-kate-qt-plugins.conf` para sesiones gráficas futuras).*

---

## ⌨️ Atajos de Teclado

| Atajo | Acción | Descripción |
| :--- | :--- | :--- |
| <kbd>Tab</kbd> | **Aceptar sugerencia** | Inserta el texto o bloque multilínea completo en el editor. |
| <kbd>Esc</kbd> | **Descartar sugerencia** | Oculta la sugerencia activa sin modificar el código. |
| <kbd>Alt</kbd> + <kbd>\</kbd> | **Forzar sugerencia** | Solicita inmediatamente una sugerencia a la IA en la posición del cursor. |
| <kbd>Alt</kbd> + <kbd>]</kbd> | **Siguiente alternativa** | Cicla a la siguiente sugerencia; si no hay más en caché, solicita una nueva. |
| <kbd>Alt</kbd> + <kbd>[</kbd> | **Alternativa anterior** | Vuelve a la sugerencia anterior de la lista de candidatas. |
| <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>A</kbd> | **Alternar chat** | Muestra u oculta el panel lateral de chat de Antigravity. |
| <kbd>Ctrl</kbd> + <kbd>Enter</kbd> | **Enviar mensaje** | Envía el prompt o comando en el panel de chat. |
| *Cualquier tecla* | **Typing-Through** | Si la tecla pulsada coincide con el siguiente carácter del texto fantasma, avanza suavemente. |

---

## ⚙️ Configuración en Kate

1. Inicia **Kate** (o reinícialo si ya estaba abierto).
2. Ve al menú superior **Preferencias** -> **Configurar Kate...**
3. En la columna izquierda, entra en **Complementos** y marca la casilla **Antigravity**.
4. Verás aparecer una nueva sección **Antigravity** en la barra de navegación de preferencias.

### Modo 1: CLI Persistente de Antigravity (Local)

* **¿Cómo funciona?** El plugin inicia un subproceso local con `agy --input-format stream-json --output-format stream-json`.
* **Ventajas:** No requiere configurar claves de API; aprovecha automáticamente la autenticación y cuotas locales de tu instalación de Antigravity.
* **Configuración:**
  * En la página de preferencias, selecciona **Backend:** *CLI de Antigravity (Local persistente)*.
  * Modelo sugerido: `gemini-3.8-flash-low` o `gemini-3.7-flash-low`.

### Modo 2: Conexión Directa REST API de Gemini

* **¿Cómo funciona?** El plugin realiza peticiones HTTP directas con `QNetworkAccessManager` al endpoint oficial de Google Gemini (`generativelanguage.googleapis.com`).
* **Ventajas:** Máxima velocidad, no requiere tener el comando `agy` activo.
* **Configuración:**
  * En la página de preferencias, selecciona **Backend:** *API directa de Gemini (REST)*.
  * Haz clic en el botón **Gestionar API Key en Google AI Studio...** para obtener o consultar tu clave en [Google AI Studio](https://aistudio.google.com/app/api-keys).
  * Pega tu clave en el campo **Clave de API (Gemini)** y haz clic en *Aplicar*.

> 🧩 **Preset de modelo por tarea:** en las preferencias puedes definir un **modelo de chat** y, por separado, un **modelo de completado inline**. Se recomienda un modelo rápido (p. ej. `gemini-3.8-flash-low`) para el ghost text y uno más potente para el chat. Si no configuras el de completado, se usa el de chat.

---

## 📝 Reglas de Proyecto (`.antigravity`)

Puedes definir instrucciones persistentes por proyecto colocando un archivo **`.antigravity`** (o `.antigravityrules`) en la raíz del proyecto. Su contenido se inyecta **verbatim** en el *system prompt* del chat, de modo que el asistente respete las convenciones del repositorio (stack, estilo, idioma de respuesta, etc.).

* Se lee de forma cacheada (por ruta + fecha de modificación + tamaño), sin releer el disco en cada mensaje.
* Límite de ~16 KB por archivo.

**Ejemplo `.antigravity`:**
```text
- Responde siempre en español.
- Este proyecto usa C++20, Qt6 y KDE Frameworks 6.
- Sigue el estilo de KDECompilerSettings: sin excepciones, sin RTTI.
- Prefiere QStringLiteral sobre QString(""). Usa i18n() para texto visible.
```

---

## 🧠 Arquitectura y Motor de Contexto

El plugin está estructurado en módulos desacoplados bajo la arquitectura de plugins de `KTextEditor`:

```text
  [ Escritura en Kate ]
            │
            ▼
    [ ViewHelper ] ──(QTimer Debounce)──► [ ContextBuilder ]
                                                  │
                                                  ├─► Extrae Prefijo / Sufijo FIM
                                                  ├─► Ancla Imports / Cabeceras (<file_imports>)
                                                  ├─► Lee pestañas abiertas (<related_open_file>)
                                                  └─► Detecta Mid-Line & Indentación
                                                  │
                                                  ▼
                                           [ AgyClient ]
                                          /             \
                   (Modo CLI Local)      /               \     (Modo REST API)
                                        ▼                 ▼
                              agy (NDJSON Stream)   Gemini API HTTPS
                                        \                 /
                                         \               /
                                          ▼             ▼
                                  [ Sanitize & Trim Overlap ]
                                                  │
                                                  ▼
                                   [ AgyInlineNoteProvider ]
                                   (Renderizado Ghost Text)
                                                  │
                                                  ▼
                                      [ AgyEventFilter ]
                                 (Tab: Aceptar | Esc: Descartar)
```

---

## 🧪 Compilación y Tests Unitarios

El proyecto cuenta con una suite completa de pruebas unitarias automatizadas con `QTest`:

```bash
# Configurar y compilar
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Ejecutar las pruebas unitarias
ctest --test-dir build --output-on-failure
```

Resultados de la suite de pruebas (100% aprobadas — 13 pruebas):
* `test_ghosttext`: Valida el renderizado de notas de texto fantasma, cálculo de longitudes, badges multilínea y comportamiento de *typing-through*.
* `test_contextbuilder`: Valida la construcción de prompts FIM, Few-Shot priming, extracción de cabeceras, contexto entre pestañas y recorte de solapamiento.
* `test_chathtmlrenderer`: Valida el renderizado HTML del chat, bloques de código con *snippets*, estado vacío y que el render incremental coincide con el completo.
* `test_projectrules_symbols`: Valida las reglas de proyecto `.antigravity`, la búsqueda de símbolos, la contención de rutas y los rangos de línea en menciones.
* `test_editblocks`: Valida el parser de bloques `agy-edit`, la localización (exacta y por espacios) del *applier* y la extracción de identificadores.
* `test_gitcommit`: Valida (en un repo temporal desechable) la detección de repositorio, el *diff staged* y la creación de commits.
* `test_agychannel_integration`: Prueba de integración *end-to-end* del subproceso NDJSON con un `agy` simulado (init/step_update/result).
* `test_agyclient`: Valida el formato de mensajes NDJSON para el CLI, cancelación de solicitudes activas y manejo de modelos.
* `test_chatsession`: Valida el ciclo de vida de turnos, streaming de respuestas, persistencia y métricas de tokens.
* `test_slashandmentions`: Valida el router de comandos slash (`/help`, `/clear`, `/reset`, `/export`, etc.) y la resolución de menciones (`@file`, `@folder`, `@diagnostics`).
* `test_chatsessionmanager`: Valida la gestión de múltiples sesiones simultáneas por proyecto y cambio dinámico de contexto.
* `test_projectdetector`: Valida la heurística de detección de tipos de proyectos (Git, CMake, Cargo, NPM, etc.).
* `appstreamtest`: Valida los metadatos KPlugin según los estándares de KDE.

---

## 📂 Estructura del Proyecto

```text
kate-antigravity/
├── CMakeLists.txt                  # Definición del proyecto en CMake con ECM, Qt6 y KF6
├── kateantigravity.json            # Metadatos del complemento para KTextEditor
├── LICENSE                         # Licencia GNU General Public License v3.0
├── CHANGELOG.md                    # Historial de cambios por versión
├── po/                             # Traducciones internacionales Gettext / KI18n
├── README.md                       # Documentación del proyecto
├── scripts/
│   ├── install_user.sh             # Script para instalación local en ~/.local
│   └── install_system.sh           # Script para instalación global en /usr
├── src/
│   ├── plugin.h / .cpp             # Entrada principal del plugin KTextEditor::Plugin
│   ├── pluginview.h / .cpp         # Integración de vistas y acciones XMLGUI
│   ├── inlinenoteprovider.h / .cpp # Motor de renderizado Ghost Text e insignias ⏎+N
│   ├── eventfilter.h / .cpp        # Filtro de eventos de teclado (Tab, Esc, Typing-Through)
│   ├── viewhelper.h / .cpp         # Coordinador de eventos por vista y temporizador de debounce
│   ├── contextbuilder.h / .cpp     # Extracción de contexto FIM enriquecido y sanitizado
│   ├── agyclient.h / .cpp          # Conector dual asíncrono (CLI stream-json / REST API)
│   ├── agyprocesschannel.h / .cpp  # Canal reutilizable del subproceso agy (NDJSON stream-json)
│   ├── agymodels.h / .cpp          # Catálogo centralizado de modelos y mapeo de endpoints REST
│   ├── agyaccount.h / .cpp         # Monitor de cuenta Google AI (cacheado), cuotas y límites
│   ├── agyinfodialog.h / .cpp      # Diálogos modales/nativos de KDE para comandos informativos
│   ├── chatsession.h / .cpp        # Modelo de conversación multi-turno y cliente del chat
│   ├── chatsessionmanager.h / .cpp # Administrador de sesiones por proyecto y persistencia
│   ├── chatwidget.h / .cpp         # Widget gráfico lateral del chat integrado en el sidebar
│   ├── chathtmlrenderer.h / .cpp   # Renderizador HTML del chat (markdown, snippets, incremental)
│   ├── chatcompletionpopup.h/.cpp  # Popup flotante de autocompletado para menciones y comandos
│   ├── slashcommandrouter.h / .cpp # Enrutador de comandos slash por registro (/help, /commit, etc.)
│   ├── mentionresolver.h / .cpp    # Resolución de menciones (@file, @selection, @git-diff, @symbol...)
│   ├── symbolindex.h / .cpp        # Búsqueda ligera de símbolos por lenguaje (parseo por regex)
│   ├── editblockparser.h / .cpp    # Parser de bloques de edición agy-edit (SEARCH/REPLACE)
│   ├── editblockapplier.h / .cpp   # Localización y aplicación de ediciones sobre documentos
│   ├── gitcommithelper.h / .cpp    # Git de solo lectura + commit conservador para /commit
│   ├── projectrules.h / .cpp       # Lectura cacheada de reglas de proyecto (.antigravity)
│   ├── projectfileindexer.h / .cpp # Indexador asíncrono de archivos del proyecto
│   ├── projectdetector.h / .cpp    # Detección heurística de proyectos y sus raíces
│   ├── tokenstats.h / .cpp         # Estadísticas de tokens persistentes (acumulado y por día)
│   ├── settings.h / .cpp           # Gestión de configuración con KConfig y KWallet
│   ├── configpage.h / .cpp         # Interfaz gráfica en las Preferencias de Kate
│   └── ui.rc                       # Menús y atajos XMLGUI de KDE
└── test/
    ├── test_ghosttext.cpp              # Pruebas del motor de texto fantasma
    ├── test_contextbuilder.cpp         # Pruebas del extractor de contexto y sanitizado
    ├── test_chathtmlrenderer.cpp       # Pruebas del renderizador HTML del chat
    ├── test_projectrules_symbols.cpp   # Pruebas de reglas de proyecto, símbolos, rutas y rangos
    ├── test_editblocks.cpp             # Pruebas del parser/applier de bloques de edición
    ├── test_gitcommit.cpp              # Pruebas de git y /commit en repo temporal
    ├── test_agychannel_integration.cpp # Integración end-to-end del subproceso NDJSON
    ├── test_agyclient.cpp              # Pruebas del cliente de comunicación con el LLM
    ├── test_chatsession.cpp            # Pruebas del gestor de turnos del chat
    ├── test_slashandmentions.cpp       # Pruebas de comandos slash y menciones @
    ├── test_chatsessionmanager.cpp     # Pruebas del administrador multi-sesión
    └── test_projectdetector.cpp        # Pruebas del detector de proyectos
```

---

## 📄 Licencia

Este proyecto está distribuido bajo la licencia **GNU General Public License v3.0 (GPL-3.0-or-later)**. Consulta el archivo [LICENSE](LICENSE) para más detalles.

Desarrollado y mantenido por **Francisco Lozano** ([@fralozan](https://github.com/fralozan)) & **FELINUX SAS** ([info@felinux.com.co](mailto:info@felinux.com.co)).
