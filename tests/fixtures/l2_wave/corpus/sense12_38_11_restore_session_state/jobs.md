### Trabajo 1
consulta: dónde se guarda el estado del editor al cerrar el proyecto
keep: M1
leído: editor state cpp, editor state hpp
Cerrado:
leído: editor state cpp, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura el estado del editor al abrir el proyecto
keep: M1
leído: editor state hpp, editor state cpp, restore workspace session, open file
Cerrado:
leído: editor state hpp, editor state cpp, restore workspace session, open file
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: hay camino guardar y restaurar el estado del editor
keep: M1
leído: editor state hpp, editor state cpp
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar estado del editor; lo leído son funciones de gestión de cursores reset to single cursor, set primary y la estructura Editor Buffer, sin serialización ni persistencia
Abierto:
- save editor state
- restore editor state
- serialize editor state


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: editor state cpp, editor state hpp

T2
    visto: editor state hpp, editor state cpp, restore workspace session, open file
    extra: Application, set workspace

T3
    visto: editor state hpp, editor state cpp

entre abiertas:
  T1=>T2  mismo objeto: editor state cpp
  T1=>T3  mismo objeto: editor state cpp
  T2=>T3  mismo objeto: editor state hpp
hacia el resto:
  T2 → Application
  T2 → set workspace
