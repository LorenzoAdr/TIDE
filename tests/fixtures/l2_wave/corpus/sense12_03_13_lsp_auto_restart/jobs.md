### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se inicia el servidor LSP al abrir el espacio de trabajo
keep: M5
leído: run, ensure backend started, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
encontré el objeto de la consulta: el servidor LSP se inicia en restart lsp for workspace llamada a workspace opened que es disparada desde run al iniciar la app y desde lsp missing install tras la instalación. ensure backend started inicia el backend DAP, no el LSP.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el proceso LSP ha muerto o dejado de responder y se dispara el reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el proceso LSP ha muerto o dejado de responder y se dispara el reinicio
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T2
    visto: run, ensure backend started, restart lsp for workspace, schedule debounced lsp restart
    extra: enable click drag mouse reporting, enable extended key reporting

T3
    visto: restart lsp for workspace, schedule debounced lsp restart

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → enable click drag mouse reporting
  T2 → enable extended key reporting
