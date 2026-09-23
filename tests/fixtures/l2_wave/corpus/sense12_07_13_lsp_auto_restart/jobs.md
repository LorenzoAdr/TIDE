### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, apply workspace settings, setup build environment watching
Cerrado:
leído: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install, apply workspace settings, setup build environment watching
Abierto:
- ensure backend started

### Trabajo 2
consulta: dónde se inicia el backend del servidor LSP para que pueda ser reiniciado
keep: M5
leído: restart lsp for workspace, run, on lsp missing install, ensure backend started
Cerrado:
encontré el objeto de la consulta: el backend del servidor LSP se inicia en restart lsp for workspace vía symbol provider workspace opened y se reinicia desde run inicio o lsp missing install post-instalación . El mecanismo de reinicio está completo.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, run, on lsp missing install
    extra: run level1 async, handle route

T2
    visto: restart lsp for workspace, run, on lsp missing install, ensure backend started
    extra: on workspace opened, set workspace clangd options

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → run level1 async
  T1 → handle route
  T2 → on workspace opened
  T2 → set workspace clangd options
