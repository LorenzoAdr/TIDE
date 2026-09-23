### Trabajo 1
consulta: dónde se detecta que el servidor LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
Cerrado:
ESA pregunta no se relanza. Si lo leído es otro mecanismo (no el mapeo pedido), cierra el claim citando lo leído. Si Abierto nombra un hop del pack, un explorador lo lee.
no encontré la detección de caída del servidor LSP timeout error; lo leído es un reinicio programado por cambio de entorno de build process build environment updates y un disparador manual por falta de instalación on lsp missing install
Abierto:
- run custom event drain
- lsp server crash detection
- clangd process monitor

### Trabajo 2
consulta: dónde se detecta que el proceso LSP ha caído o dejado de responder y se dispara el reinicio
keep: M5
leído: clangd process alive, clangd index healthy, lsp client hpp, process alive, transport running
Cerrado:
leído: clangd process alive, clangd index healthy, lsp client hpp, process alive, transport running
Abierto:
- start async worker

### Trabajo 3
consulta: dónde se lee el estado del proceso para detectar que ha muerto o se ha congelado y se dispara la acción de reinicio
keep: M5 M1 M2
leído: clangd process alive, transport running, process alive, async worker main, schedule debounced lsp restart, restart lsp for workspace, run
Cerrado:
leído: clangd process alive, transport running, process alive, async worker main, schedule debounced lsp restart, restart lsp for workspace, run
Abierto:
- start async worker
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, schedule debounced lsp restart, process build environment updates
    extra: on lsp missing install, on workspace opened

T2
    visto: clangd process alive, clangd index healthy, lsp client hpp, process alive
    extra: read file text, english strings

T3
    visto: clangd process alive, transport running, process alive, async worker main
    extra: read file text, english strings

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  mismo objeto: clangd process alive
hacia el resto:
  T1 → on lsp missing install
  T1 → on workspace opened
  T2 → read file text
  T2 → english strings
  T3 → read file text
  T3 → english strings
