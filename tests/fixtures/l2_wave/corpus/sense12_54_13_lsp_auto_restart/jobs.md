### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M1 M3
leído: restart lsp for workspace, on lsp missing install, notify lsp status
Cerrado:
leído: restart lsp for workspace, on lsp missing install, notify lsp status
Abierto:
- finish python lsp start locked

### Trabajo 2
consulta: cómo se reinicia el servidor LSP automáticamente cuando se cae o no responde
keep: M1
leído: restart lsp for workspace, schedule debounced lsp restart, run
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección automática de caída no-respuesta del LSP; lo leído muestra que el reinicio es manual o reactivo a cambios de configuración entorno apply workspace settings, setup build environment watching, on lsp missing install, pero no hay un watchdog, ping timeout o handler de error en el cliente LSP que dispare schedule debounced lsp restart
Abierto:
- on exit
- on error
- on timeout
- on crash

### Trabajo 3
consulta: dónde se maneja la salida inesperada o el error del proceso LSP para disparar un reinicio
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se maneja la salida inesperada o el error del proceso LSP para disparar un reinicio
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, on lsp missing install, notify lsp status
    extra: on workspace opened, set workspace clangd options

T2
    visto: restart lsp for workspace, schedule debounced lsp restart, run
    extra: on lsp missing install, on workspace opened

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
