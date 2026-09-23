### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dispara el reinicio del LSP cuando falta la instalación o al abrir el workspace
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
Cerrado:
encontré el disparo del reinicio del LSP: 1 al abrir el workspace, run llama a restart lsp for workspace vía ensure backend started o flujo de inicio, aunque el cuerpo de run está truncado, el circuito y los callers lo confirman como punto de entrada principal; 2 cuando falta la instalación, lsp missing install instala el paquete y, si tiene éxito result ok llama a restart lsp for workspace . El efecto es la reconfiguración del LSP y la reapertura de documentos.
Abierto:
- ensure backend started

### Trabajo 3
consulta: dónde se detecta que el proceso LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se detecta que el proceso LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el proceso LSP ha caído o dejado de responder y se dispara el reinicio
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
no encontré la detección de caída timeout del LSP; lo leído muestra el disparo manual schedule debounced lsp restart y el efecto restart lsp for workspace, pero falta el loop que lee pending lsp restart y lsp restart deadline para decidir reiniciar
Abierto:
- pending lsp restart
- lsp restart deadline
- check lsp restart deadline
- ui tick lsp check


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
    extra: on workspace opened, set workspace clangd options

T3
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T4
    visto: restart lsp for workspace, schedule debounced lsp restart, run
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T1=>T4  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: restart lsp for workspace
  T2=>T4  mismo objeto: restart lsp for workspace
  T3=>T4  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → on workspace opened
  T3 → set workspace clangd options
