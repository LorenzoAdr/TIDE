### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
ESA pregunta no se relanza. Si lo leído es otro mecanismo (no el mapeo pedido), cierra el claim citando lo leído. Si Abierto nombra un hop del pack, un explorador lo lee.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se inicia el reinicio diferido del servidor LSP tras una caída
keep: M5
leído: schedule debounced lsp restart, restart lsp for workspace, run, ensure backend started
Cerrado:
encontré el mecanismo de inicio del reinicio diferido: se programa en schedule debounced lsp restart tras cambios de entorno o configuración y se ejecuta en restart lsp for workspace cuando el bucle principal run detecta que ha vencido el deadline lsp restart deadline .
Abierto:
- lsp restart deadline

### Trabajo 3
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: schedule debounced lsp restart, restart lsp for workspace, run, on lsp missing install
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace, run, on lsp missing install
Abierto:
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart

T2
    visto: schedule debounced lsp restart, restart lsp for workspace, run, ensure backend started
    extra: apply workspace settings, setup build environment watching

T3
    visto: schedule debounced lsp restart, restart lsp for workspace, run, on lsp missing install
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → apply workspace settings
  T3 → setup build environment watching
