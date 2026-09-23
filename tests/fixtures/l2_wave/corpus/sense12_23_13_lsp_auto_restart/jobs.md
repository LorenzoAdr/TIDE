### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o se reinicia el entorno
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, apply workspace settings, setup build environment watching
Cerrado:
encontré el mecanismo de reinicio del LSP: se dispara por cambio de entorno setup build environment watching → schedule debounced lsp restart o por instalación faltante on lsp missing install, y el efecto es restart lsp for workspace que reabre el workspace y reinicia los documentos.
Abierto:
- process build environment updates

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se verifica si el proceso del servidor de lenguaje está vivo y qué ocurre si no responde
keep: M5
leído: clangd process alive
Cerrado:
leído: clangd process alive
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se llama a la verificación de vida del proceso clangd y qué se hace si falla
keep: M5
leído: clangd process alive
Cerrado:
leído: clangd process alive
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, clangd process alive
    extra: on lsp missing install, on workspace opened

T3
    visto: clangd process alive
    extra: read file text, english strings

T4
    visto: clangd process alive
    extra: read file text, write terminal init script

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: clangd process alive
  T2=>T4  mismo objeto: clangd process alive
  T3=>T4  mismo objeto: clangd process alive
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → read file text
  T3 → english strings
