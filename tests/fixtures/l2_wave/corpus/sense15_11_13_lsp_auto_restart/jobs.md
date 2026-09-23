### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M2 M5
leído: on lsp notification, spawn language server
Cerrado:
leído: on lsp notification, spawn language server
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se programa el reinicio del servidor LSP cuando se detecta que falta o no responde
keep: M1 M2
leído: restart lsp for workspace, on lsp missing install, on lsp notification
Cerrado:
leído: restart lsp for workspace, on lsp missing install, on lsp notification
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se detecta que el servidor LSP se ha caído o dejado de responder
keep: M1 M7
leído: wait response, start, on transport reader eof
Cerrado:
encontré los dos mecanismos de detección: 1 transport reader eof detecta la caída del proceso cierre de socket EOF y limpia el PID; 2 wait response detecta la falta de respuesta timeout mediante timeout en la variable de condición. No hay un mecanismo unificado de salud activo, sino detección reactiva por fallo de transporte o espera agotada.
Abierto:
- send request


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on lsp notification, spawn language server
    extra: contains, is lsp trackable path

T2
    visto: restart lsp for workspace, on lsp missing install, on lsp notification
    extra: on workspace opened, set workspace clangd options

T3
    visto: wait response, start, on transport reader eof
    extra: send request, contains

entre abiertas:
  T1=>T2  mismo objeto: on lsp notification
  T1=>T3  spawn language server → initialize → send lsp request → wait response
  T2=>T3  extra toca visto: on lsp notification
hacia el resto:
  T1 → contains
  T1 → is lsp trackable path
  T2 → on workspace opened
  T2 → set workspace clangd options
  T3 → send request
  T3 → contains
