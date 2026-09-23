### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o se reinicia el entorno
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, apply workspace settings, setup build environment watching
Cerrado:
encontré el mecanismo de reinicio del LSP: se dispara por instalación fallida lsp missing install cambio de configuración apply workspace settings o cambio de entorno de build setup build environment watching vía schedule debounced lsp restart y el efecto es restart lsp for workspace que reabre el workspace y reinicia los documentos.
Abierto:
- ensure backend started
- process build environment updates

### Trabajo 2
consulta: dónde se detecta que el proceso del servidor de lenguaje LSP se ha caído o dejado de responder
keep: M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detecta que el proceso del servidor de lenguaje LSP se ha caído o dejado de responder
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: on workspace opened, set workspace clangd options

T2
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
