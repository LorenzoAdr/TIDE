### Trabajo 1
consulta: qué hace el controlador de IA al cancelar el nivel 1 y cómo limpia el archivo
keep: M1 M6
leído: cancel level1, cancel all, cancel, append
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de cancelación del nivel 1 en el controlador de IA, pero no encontré ninguna acción de limpieza de archivo; lo leído muestra que la cancelación solo establece un flag atómico y registra un log en consola, sin tocar el sistema de archivos.
Abierto:
- limpieza de archivo

### Trabajo 2
consulta: dónde se limpia el texto del editor al cancelar la generación de código
keep: M2 M4
leído: cancel live lsp on cursor move
Cerrado:
leído: cancel live lsp on cursor move
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se revierte el contenido del archivo al cancelar la generación de código
keep: M1 M3
leído: cancel current, clear pending insert, rollback pending, undo, handle user input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reversión del contenido del archivo al cancelar la generación de código en el controlador de IA; lo leído muestra que la cancelación solo limpia flags atómicos pending insert y llama a cancel all que detiene la generación, pero no restaura el buffer del editor ni el archivo en disco. El candidato rollback pending pertenece a level2 session otro sistema y no es invocado por ai controller al cancelar.
Abierto:
- handle route
- handle editor keys
- run insert async
- handle virtual edit event
- cancel all


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, cancel all, cancel, append
    extra: Ai Controller, fetch add

T2
    visto: cancel live lsp on cursor move
    extra: completion lsp tick, ISymbol Provider

T3
    visto: cancel current, clear pending insert, rollback pending, undo
    extra: handle route, cancel all

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  cancel current → cancel all → cancel level1
  T2=>T3  sin camino
hacia el resto:
  T1 → Ai Controller
  T1 → fetch add
  T2 → completion lsp tick
  T2 → ISymbol Provider
  T3 → handle route
  T3 → write text file
