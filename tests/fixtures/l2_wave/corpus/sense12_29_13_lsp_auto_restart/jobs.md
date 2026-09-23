### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara su reinicio
keep: M5
leído: restart lsp for workspace, send request, lsp transport cpp, wait response, read message, reader loop, schedule debounced lsp restart, set reader eof handler
Cerrado:
leído: restart lsp for workspace, send request, lsp transport cpp, wait response, read message, reader loop, schedule debounced lsp restart, set reader eof handler
Abierto:
- ensure backend started

### Trabajo 2
consulta: dónde se inicia el servidor de lenguaje al abrir un espacio de trabajo
keep: M5
leído: restart lsp for workspace, on workspace opened, set workspace clangd options
Cerrado:
leído: restart lsp for workspace, on workspace opened, set workspace clangd options
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara su reinicio
keep: M5
leído: restart lsp for workspace, schedule debounced lsp restart, on transport reader eof, set reader eof handler, reader loop, start
Cerrado:
encontré el mecanismo de detección de caída: es la lectura de EOF en el loop del transport. reader loop detecta el fallo de lectura, invoca el handler registrado on transport reader eof, que marca el cliente como no listo. El reinicio se dispara desde run al verificar el estado del cliente.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: restart lsp for workspace, send request, lsp transport cpp, wait response
    extra: on lsp missing install, on workspace opened
    entre interno: restart lsp for workspace → send request

T2
    visto: restart lsp for workspace, on workspace opened, set workspace clangd options
    extra: on lsp missing install, ensure compile commands for clangd

T3
    visto: restart lsp for workspace, schedule debounced lsp restart, on transport reader eof, set reader eof handler
    extra: on lsp missing install, on workspace opened

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T3  mismo objeto: restart lsp for workspace
  T1=>T3  initialize → send lsp request → wait response
  T2=>T3  mismo objeto: restart lsp for workspace
hacia el resto:
  T1 → on lsp missing install
  T1 → ensure compile commands for clangd
  T2 → on lsp missing install
  T2 → ensure compile commands for clangd
  T3 → on lsp missing install
  T3 → ensure compile commands for clangd
