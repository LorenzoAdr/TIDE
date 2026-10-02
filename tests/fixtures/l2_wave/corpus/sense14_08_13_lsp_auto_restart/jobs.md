### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde y se dispara el reinicio automático
keep: M3 M4
leído: is running, ensure completion server, status text, start completion server, health ok, stop owned unlocked
Cerrado:
encontré el mecanismo: la detección de caída no-respuesta es health ok probe HTTP GET health y el disparo de reinicio es start completion server que, si health ok falla o el stamp cambia, llama a stop owned unlocked y relanza el proceso. El reinicio automático se dispara en cada llamada a ensure completion server ej. al enviar una petición, no por un watchdog temporal independiente.
Abierto:
- start server
- send lsp request

### Trabajo 2
consulta: dónde se programa la verificación periódica del estado del servidor LSP para detectar caídas o falta de respuesta
keep: M4 M7
leído: start, on transport reader eof, schedule debounced lsp restart, is running, health ok
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré un mecanismo de verificación periódica watchdog timer para el servidor LSP; lo leído muestra que la detección de caídas es reactiva vía transport reader eof EOF en el pipe y el reinicio es manual debounced tras cambios de configuración schedule debounced lsp restart sin polling activo de salud.
Abierto:
- start server
- tick lsp


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: is running, ensure completion server, status text, start completion server
    extra: initialize, send lsp request
    entre interno: ensure completion server → stop owned unlocked

T2
    visto: start, on transport reader eof, schedule debounced lsp restart, is running
    extra: set notification handler, set reader eof handler

entre abiertas:
  T1=>T2  mismo objeto: is running
hacia el resto:
  T1 → initialize
  T1 → send lsp request
  T2 → set notification handler
  T2 → set reader eof handler
