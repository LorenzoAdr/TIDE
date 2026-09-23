### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o deja de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
Abierto:
- ensure backend started

### Trabajo 2
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M5
leído: ensure backend started
Cerrado:
leído: ensure backend started
Abierto:
- set backend epoch
- register backend wake callback

### Trabajo 3
consulta: dónde se llama a reiniciar el servidor de lenguaje LSP tras detectar que se ha caído o dejado de responder
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, ensure backend started
Cerrado:
encontré el objeto de la consulta: el reinicio tras caída se dispara en lsp missing install llamado desde run que invoca a restart lsp for workspace el efecto es la reconfiguración del provider y reapertura de documentos
Abierto:
- on lsp missing install


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, on lsp missing install, run
    extra: on workspace opened, set workspace clangd options

T2
    visto: ensure backend started
    extra: set backend epoch, debug adapter kind for program

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, ensure backend started
    extra: on lsp missing install, on workspace opened
    entre interno: restart lsp for workspace → ensure backend started

entre abiertas:
  T1=>T2  extra toca visto: ensure backend started
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: ensure backend started
hacia el resto:
  T1 → on workspace opened
  T1 → set workspace clangd options
  T2 → set backend epoch
  T2 → debug adapter kind for program
  T3 → on workspace opened
  T3 → set workspace clangd options
