### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el servidor LSP se ha caído o no responde para disparar el reinicio
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor LSP cuando se cae o no responde
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run, apply workspace settings, setup build environment watching
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la lógica de detección de caída no-respuesta del servidor LSP; lo leído son reinicios por cambio de configuración apply workspace settings, instalación de toolpacks on lsp missing install o cambios en el entorno de build setup build environment watching, pero no hay mecanismo de heartbeat, timeout o detección de proceso muerto en el código revisado
Abierto:
- heartbeat lsp
- lsp process alive
- check lsp health

### Trabajo 3
consulta: dónde se verifica si el proceso del servidor LSP está vivo o responde para detectar una caída
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se verifica si el proceso del servidor LSP está vivo o responde para detectar una caída
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart

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
  T2 → on workspace opened
  T2 → set workspace clangd options
