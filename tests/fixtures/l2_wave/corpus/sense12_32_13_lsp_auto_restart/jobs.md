### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o se reinicia el entorno
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, apply workspace settings, setup build environment watching, process build environment updates
Cerrado:
encontré el mecanismo de reinicio del LSP: se dispara por instalación faltante on lsp missing install, cambio de settings apply workspace settings o cambio de entorno de build process build environment updates vía schedule debounced lsp restart; el efecto es restart lsp for workspace que reabre el workspace y reinicia los documentos.
Abierto:
- reopen workspace documents

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, clangd index healthy, process alive, ready
Cerrado:
encontré el mecanismo de detección de caída del LSP: es una comprobación activa de salud clangd index healthy que verifica si el proceso hijo sigue vivo mediante kill process alive y si el cliente está listo ready . No hay detección pasiva por señal de muerte ni timeout de heartbeat; la detección ocurre cuando se consulta la salud del índice.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se consulta la salud del índice clangd para detectar si el servidor se ha caído
keep: M2
leído: clangd index healthy, ready, process alive
Cerrado:
encontré el mecanismo de consulta de salud: clangd index healthy delega en ready y process alive; este último detecta la caída del servidor verificando si el PID existe y respondiendo a kill pid, 0
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se invoca la comprobación de salud del servidor LSP para detectar si se ha caído
keep: M5
leído: clangd process alive, process alive, clangd index healthy
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la invocación de la comprobación de salud para detectar caída; lo leído son los cuerpos de las funciones de salud y sus llamadas internas, pero no sus callers externos que disparan la detección
Abierto:
- callers de clangd process alive
- callers de clangd index healthy


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive, clangd index healthy
    extra: on lsp missing install, on workspace opened

T3
    visto: clangd index healthy, ready, process alive
    extra: clangd starting, tabular max allowed scroll

T4
    visto: clangd process alive, process alive, clangd index healthy
    extra: read file text, english strings

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T2  run custom event drain → clangd index healthy
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: clangd index healthy
  T2=>T4  mismo objeto: clangd process alive
  T3=>T4  mismo objeto: clangd index healthy
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → clangd starting
  T3 → tabular max allowed scroll
