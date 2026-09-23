### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, run, on lsp missing install, schedule debounced lsp restart, apply workspace settings, setup build environment watching
Cerrado:
leído: restart lsp for workspace, run, on lsp missing install, schedule debounced lsp restart, apply workspace settings, setup build environment watching
Abierto:
- ensure backend started

### Trabajo 2
consulta: dónde se inicia el backend del servidor LSP tras detectar que falta o ha caído
keep: M5
leído: restart lsp for workspace, on lsp missing install, schedule debounced lsp restart, run, ensure backend started
Cerrado:
encontré el mecanismo de inicio del backend LSP tras detectar falta o caída: el disparo es lsp missing install tras instalación exitosa o run bucle principal, ambos llaman a restart lsp for workspace que ejecuta workspace opened en el symbol provider para iniciar reiniciar el backend. ensure backend started es para el backend de debug DAP, no para LSP.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, run, on lsp missing install, schedule debounced lsp restart
    extra: run level1 async, handle route

T2
    visto: restart lsp for workspace, on lsp missing install, schedule debounced lsp restart, run
    extra: on workspace opened, set workspace clangd options

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → run level1 async
  T1 → handle route
  T2 → on workspace opened
  T2 → set workspace clangd options
