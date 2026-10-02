### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o deja de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o falta para disparar el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída del servidor LSP; lo leído es la instalación de un LSP faltante on lsp missing install y el reinicio tras cambios de entorno schedule debounced lsp restart, pero no hay código que detecte un crash activo del proceso LSP
Abierto:
- on lsp server crashed
- lsp server died
- on lsp connection lost

### Trabajo 3
consulta: dónde se detecta que el proceso del servidor de lenguaje LSP se ha caído o ha muerto
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el proceso del servidor de lenguaje LSP se ha caído o ha muerto
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

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
