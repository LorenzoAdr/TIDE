### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde
keep: M1 M4
leído: on transport reader eof, wait response, reader loop
Cerrado:
encontré los dos mecanismos de detección: 1 Caída del proceso: reader loop detecta EOF en la lectura y ejecuta reader eof handler que apunta a transport reader eof el cual limpia el PID con waitpid . 2 No respuesta timeout : wait response espera en pending cv hasta deadline si expira, borra la petición pendiente y retorna false propagando el fallo al cliente.
Abierto:
- send request

### Trabajo 2
consulta: dónde se reinicia el servidor LSP tras un fallo
keep: M6 M5
leído: on transport reader eof, wait response, reader loop, start, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de reinicio tras fallo: el fallo EOF se detecta en reader loop que invoca el handler transport reader eof en lsp client . Este handler limpia el estado ready false waitpid pero NO reinicia directamente. El reinicio se orquesta en application : schedule debounced lsp restart programa un reinicio diferido 2s, y restart lsp for workspace ejecuta la reapertura del workspace y documentos. El disparo del reinicio tras fallo específico no está completamente trazado en los callers de schedule debounced lsp restart solo settings build env, pero el flujo de fallo- limpieza está claro. El hueco es qué conecta exactamente el EOF del transport a la llamada de schedule debounced lsp restart o
Abierto:
- send request
- apply workspace settings

### Trabajo 3
consulta: qué hace el handler de fin de lectura del transporte para iniciar el reinicio del servidor
keep: M1 M6
leído: on transport reader eof, spawn language server, start
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio del servidor en el handler de fin de lectura; lo leído muestra que on transport reader eof solo limpia estado ready false, waitpid y NO invoca spawn ni restart. El reinicio no ocurre en este handler.
Abierto:
- reader loop


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, wait response, reader loop
    extra: send request, contains

T2
    visto: on transport reader eof, wait response, reader loop, start
    extra: send request, contains

T3
    visto: on transport reader eof, spawn language server, start
    extra: c str, initialize

entre abiertas:
  T1=>T2  mismo objeto: on transport reader eof
  T1=>T3  mismo objeto: on transport reader eof
  T1=>T3  spawn language server → initialize → send lsp request → wait response
  T2=>T3  mismo objeto: on transport reader eof
  T2=>T3  spawn language server → initialize → send lsp request → wait response
hacia el resto:
  T1 → send request
  T1 → contains
  T2 → send request
  T2 → contains
  T3 → c str
  T3 → initialize
