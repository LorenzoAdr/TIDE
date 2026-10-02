### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde y se dispara el reinicio
keep: M4 M2
leído: reader loop, start, on transport reader eof, stop, wait response, restart lsp for workspace, run, on lsp missing install
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de no responde timeout que dispare el reinicio; lo leído muestra que la detección de caída EOF en reader loop solo limpia estado transport reader eof sin reiniciar, y el único reinicio restart lsp for workspace se dispara manualmente o tras instalación, no tras fallo de comunicación.
Abierto:
- send request
- on lsp status changed
- notify lsp status


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: reader loop, start, on transport reader eof, stop
    extra: open host pty, contains
hacia el resto:
  T1 → open host pty
  T1 → contains
