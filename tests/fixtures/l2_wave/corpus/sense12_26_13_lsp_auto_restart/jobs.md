### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o dejado de responder para disparar un reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
Abierto:
- ensure backend started

### Trabajo 2
consulta: dónde se asegura que el backend del LSP esté iniciado y se detecta si falta para reiniciarlo
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run, ensure backend started
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
encontré el mecanismo: ensure backend started leído en ola 4 asegura el arranque del backend de debug DAP, no del LSP. La consulta pide el LSP. El sistema LSP se asegura en restart lsp for workspace leído en ola 2 y se detecta su falta en lsp missing install leído en ola 3 . La lógica de detectar si falta para reiniciarlo está en lsp missing install : si la instalación del pack falla o no hay pack, no reinicia; si tiene éxito, llama a restart lsp for workspace . El asegurar que esté iniciado es la responsabilidad de restart lsp for workspace que se llama desde run inicio y lsp missing install post-instalación . No hay un único punto de ensure LSP started como tal, sino un flujo de arranque en run y recuperación en on lsp missing install
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el proceso del servidor LSP se ha caído o dejado de responder para disparar un reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el proceso del servidor LSP se ha caído o dejado de responder para disparar un reinicio
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
    extra: on workspace opened, set workspace clangd options

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → on workspace opened
  T2 → set workspace clangd options
