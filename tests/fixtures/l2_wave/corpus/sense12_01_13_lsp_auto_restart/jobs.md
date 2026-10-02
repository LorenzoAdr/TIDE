### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor LSP ha dejado de responder y se dispara el reinicio
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se inicia el reinicio del servidor LSP tras detectar que falta o ha caído
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, apply workspace settings, setup build environment watching
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se inicia el reinicio del servidor LSP tras detectar que falta o ha caído
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, apply workspace settings, setup build environment watching
Abierto:
- ensure backend started

### Trabajo 3
consulta: dónde se detecta que el proceso LSP ha muerto o no responde y se dispara la acción de reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el proceso LSP ha muerto o no responde y se dispara la acción de reinicio
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
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
