# Changelog

Todos los cambios notables de **Kate Antigravity** se documentan en este archivo.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es/1.1.0/)
y el proyecto se adhiere a [Versionado Semántico](https://semver.org/lang/es/).

## [1.0.0] - 2026-09-22

Primera versión pública. Complemento nativo de KTextEditor/Kate con
autocompletado *Ghost Text* y panel lateral de chat asistente, en
C++ / Qt 6 / KDE Frameworks 6, impulsado por Google Antigravity (`agy`) y
modelos Gemini / Claude / GPT. Suite de pruebas con `QTest` (13 pruebas).

### Autocompletado en línea (Ghost Text)

- Renderizado de texto fantasma atenuado mediante
  `KTextEditor::InlineNoteProvider`, integrado en el flujo del búfer sin alterar
  el contenido del documento ni el historial de Deshacer/Rehacer hasta aceptar.
- Soporte multilínea con insignia de líneas pendientes (p. ej. `⏎+3`).
- Interacción de teclado:
  - <kbd>Tab</kbd>: acepta e inserta la sugerencia completa.
  - <kbd>Esc</kbd>: descarta la sugerencia.
  - <kbd>Alt</kbd>+<kbd>\</kbd>: fuerza una sugerencia en la posición del cursor.
  - <kbd>Alt</kbd>+<kbd>]</kbd> / <kbd>Alt</kbd>+<kbd>[</kbd>: cicla entre
    sugerencias alternativas (o solicita una nueva).
  - *Typing-through*: al escribir el siguiente carácter predicho, el ghost text
    avanza sin peticiones de red redundantes.
- **Debounce adaptativo**: más rápido tras aceptar una sugerencia, más lento en
  ráfagas de tecleo; configurable (50–2000 ms) por vista.
- **Preset de modelo por tarea**: modelo de completado inline separado del de
  chat (por defecto usa el de chat).

### Motor de contexto (FIM enriquecido)

- Extracción de prefijo/sufijo alrededor del cursor con límites de líneas
  configurables.
- *Pinning* de cabeceras/imports del inicio del archivo cuando el cursor está en
  una línea avanzada.
- Conciencia de pestañas abiertas (contexto de archivos relacionados, p. ej. el
  `.h` de un `.cpp`).
- Detección *mid-line* e indentación de la línea activa.
- Sanitizado de la respuesta y recorte de solapamiento con el sufijo.
- **Enriquecimiento semántico automático**: los identificadores del prompt que
  resuelven a símbolos del proyecto se adjuntan al contexto.

### Conexión dual (backends)

- **CLI de Antigravity** (`agy` en modo `stream-json`): subproceso persistente
  que reutiliza la autenticación y cuotas locales, sin configurar claves. Canal
  NDJSON reutilizable con reciclado asíncrono (no congela la interfaz).
- **API directa de Gemini (REST)** mediante `QNetworkAccessManager`, con
  streaming SSE en el chat. La clave viaja en la cabecera `x-goog-api-key`.
- Selector de modelos (Gemini / Claude / GPT) y catálogo centralizado con mapeo
  de nombres a los modelos servidos por el endpoint REST.

### Panel lateral de chat

- Conversación multi-turno con streaming **incremental** (solo se re-renderiza
  el mensaje en curso).
- Renderizado Markdown a HTML con bloques de código y acciones por *snippet*:
  Copiar, Insertar y Reemplazar (con confirmación y *preview* antes de escribir
  en el editor).
- Barra del chat con selector de modelo en 1 clic e insignia de cuenta de
  Google AI (correo, método de autenticación, cuotas y límites).
- Posición del panel configurable (barra lateral izquierda o derecha).
- **Búsqueda en el historial** con contador de coincidencias.
- **Limpiar chat** persistente por espacio (el ocultado sobrevive a cambios de
  espacio y reinicios) y **borrado permanente** que conserva el contexto del
  agente, con un panel de botones nativos de Kate.
- Acciones de contexto del editor: enviar selección al chat, explicar código,
  refactorizar, buscar bugs y generar tests (menú contextual y acciones XMLGUI).
- **Entrada multimodal**: adjuntar imágenes al chat (backend REST de Gemini).
- **Estadísticas de tokens persistentes**: acumulado histórico y uso diario en
  el panel `/usage`.

### Espacios de trabajo

- Detección heurística del proyecto activo a partir del documento (Git, CMake,
  Cargo, NPM, Gradle, Composer, Go, Python, Maven, Makefile, etc.), con rama Git.
- Cada espacio queda ligado a una **conversación real de `agy`**
  (`--conversation <id>`), reanudando su contexto; utilizable **sin archivos
  abiertos** exponiendo el proyecto con `--add-dir`.
- **Auto-cambio robusto** al proyecto del archivo activo (sin degradar a
  "General"), **anclaje** del espacio, y gestión completa: **añadir** (selector
  de carpeta), **renombrar**, **quitar**. Recuerda el último espacio activo.
- Sesiones de chat separadas por proyecto con persistencia transparente en disco
  (JSON) y métricas de uso de tokens por sesión.

### Aplicar cambios y Git

- **Aplicar cambios como diff**: el asistente emite bloques `agy-edit`
  (`SEARCH`/`REPLACE`) y un diálogo de revisión permite aplicarlos de forma
  selectiva sobre los documentos abiertos (coincidencia exacta con respaldo
  insensible a espacios).
- **`/commit`**: genera un mensaje de commit (estilo Conventional Commits) desde
  el *diff staged* y crea el commit **solo tras confirmación**. Conservador:
  solo lo *staged*, respeta hooks, nunca `add`, `push`, `amend` ni `--no-verify`.

### Menciones `@` con autocompletado

- `@archivo` y `@archivo:20-45` (rango de líneas): archivos del proyecto
  indexados en segundo plano (indexador asíncrono que no congela la GUI).
- `@carpeta/`: inclusión del árbol de un directorio.
- `@selection`: la selección activa del editor.
- `@git-diff`: cambios sin confirmar (staged y unstaged), en solo lectura.
- `@symbol:Nombre`: definición de un símbolo por parseo ligero (C/C++, Python,
  JS/TS, Rust, Go, Java).
- `@diagnostics` (y alias `@errors`, `@warnings`): errores y advertencias
  marcados en los documentos abiertos.
- `@terminal`: captura *best-effort* de la salida del terminal integrado.
- Popup flotante de autocompletado; las menciones no pueden escapar del árbol
  del proyecto (contención de rutas); límites de tamaño con aviso de truncado.

### Comandos slash `/`

- `/help`, `/settings`, `/clear`, `/reset` (alias `/new`), `/project`,
  `/model [nombre]`, `/usage`, `/files`, `/export [archivo.md]`, `/commit`.
- `/help`, `/usage` y `/project` se muestran en diálogos nativos de KDE
  integrados con Breeze.

### Reglas de proyecto

- Archivo `.antigravity` en la raíz del proyecto con instrucciones persistentes
  inyectadas en el *system prompt* del chat (cacheadas por ruta+mtime+tamaño).

### Seguridad y configuración

- Almacenamiento de la clave de API con **KWallet** (cifrado en reposo), con
  migración automática desde KConfig y *fallback* en entornos headless.
- `/commit` y `@git-diff` ejecutan `git` pasando argumentos por *argv* (sin
  shell), evitando inyección.
- Página de preferencias integrada en Kate: modelo de chat, modelo de
  completado, backend, clave de API, *debounce*, líneas de contexto, posición
  del panel y cambio automático de sesión por proyecto.

### Internacionalización

- Interfaz y mensajes internacionalizados en inglés y español con `KI18n`
  (dominio `kateantigravity`), por defecto en español en un escritorio en
  español.

### Calidad

- Suite de pruebas con `QTest`: `test_ghosttext`, `test_contextbuilder`,
  `test_chathtmlrenderer`, `test_projectrules_symbols`, `test_editblocks`,
  `test_gitcommit`, `test_agychannel_integration`, `test_agyclient`,
  `test_chatsession`, `test_slashandmentions`, `test_chatsessionmanager`,
  `test_projectdetector`, y `appstreamtest` para los metadatos KPlugin.

---

[1.0.0]: https://github.com/fralozan/kate-antigravity/releases/tag/v1.0.0
