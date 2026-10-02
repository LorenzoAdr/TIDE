### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M4
leído: ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape para cancelar la generación; lo leído es una función de escape de caracteres en strings ai trace escape, no un handler de teclado
Abierto:
- cancel all
- cancel level1
- key handler

### Trabajo 2
consulta: dónde se cancela la generación de la IA cuando el usuario hace clic fuera del editor
keep: M6 M1
leído: handle problems button click
Cerrado:
leído: handle problems button click
Abierto:
- handle editor chrome mouse

### Trabajo 3
consulta: dónde se invoca la cancelación del agente de nivel 1
keep: M1 M10
leído: cancel level1, cancel all, cancel, ai controller hpp, cancel current, handle user input, handle route
Cerrado:
leído: cancel level1, cancel all, cancel, ai controller hpp, cancel current, handle user input, handle route
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape

T2
    visto: handle problems button click
    extra: handle editor chrome mouse, mark dirty

T3
    visto: cancel level1, cancel all, cancel, ai controller hpp
    extra: Ai Controller, fetch add

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  cancel level1 → ai trace escape
  T2=>T3  sin camino
hacia el resto:
  T2 → handle editor chrome mouse
  T2 → mark dirty
  T3 → Ai Controller
  T3 → fetch add
