### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o dejado de responder para disparar un reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor LSP se ha caído o dejado de responder para disparar un reinicio
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor LSP de forma automática tras detectar que se ha caído
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, process build environment updates, run custom event drain
Cerrado:
encontré el mecanismo de reinicio automático tras caída: el latch pending lsp restart se activa en schedule debounced lsp restart y se consume en process build environment updates que verifica el deadline y el fingerprint del entorno antes de llamar a restart lsp for workspace . El disparador de la caída cambio de entorno se detecta comparando active environment fingerprint con last lsp environment fingerprint en process build environment updates .
Abierto:
- pending lsp restart
- active environment fingerprint


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, process build environment updates
    extra: on workspace opened, set workspace clangd options

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T2 → on workspace opened
  T2 → set workspace clangd options
