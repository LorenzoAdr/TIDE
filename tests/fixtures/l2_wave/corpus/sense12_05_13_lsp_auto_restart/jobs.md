### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: quién llama a schedule debounced lsp restart cuando el servidor LSP deja de responder
keep: M5
leído: schedule debounced lsp restart
Cerrado:
leído: schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: quién invoca el reinicio diferido del LSP cuando se detecta que el servidor falta o no responde
keep: M5
leído: process build environment updates, restart lsp for workspace, on lsp missing install
Cerrado:
encontré el objeto de la consulta: el reinicio diferido se invoca vía schedule debounced lsp restart cuyo único caller anclado es setup build environment watching disparado por cambios en el entorno de build, no por fallo directo del LSP . El caso de servidor falta lsp missing install invoca restart lsp for workspace directamente sin debounce. La consulta mezcla dos caminos: el diferido entorno y el inmediato falta .
Abierto:
- run custom event drain
- setup build environment watching

### Trabajo 4
consulta: dónde se detecta que el proceso LSP ha muerto o no responde y se dispara el reinicio automático
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
Cerrado:
Lo leído no contestó ESA pregunta. Un hop de Abierto/extra no es un hueco.
no encontré la detección de muerte no-respuesta del LSP; lo leído es un reinicio basado en cambios de entorno de build fingerprint y configuración, no en salud del proceso
Abierto:
- run custom event drain
- lsp health check
- clangd alive
- lsp heartbeat
- process exit handler


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart
    extra: on lsp missing install, on workspace opened

T2
    visto: schedule debounced lsp restart, restart lsp for workspace
    extra: apply workspace settings, setup build environment watching

T3
    visto: process build environment updates, restart lsp for workspace, on lsp missing install, schedule debounced lsp restart
    extra: run custom event drain, active environment fingerprint

T4
    visto: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T1=>T4  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T4  mismo objeto: schedule debounced lsp restart
  T3=>T4  mismo objeto: process build environment updates
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → run custom event drain
  T3 → active environment fingerprint
