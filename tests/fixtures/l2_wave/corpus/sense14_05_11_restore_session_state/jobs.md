### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el proyecto
keep: M2 M4
leído: save, remember
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que guarda la posición del cursor al cerrar el proyecto; lo leído confirma que save persiste configuración de entorno temas ai pero NO estado del editor, y remember solo guarda la ruta del workspace. El sistema cursor history solo gestiona navegación back forward en memoria, sin persistencia a disco.
Abierto:
- save state
- serialize cursor
- persist position

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M8 M11
leído: restore workspace session, open file
Cerrado:
leído: restore workspace session, open file
Abierto:
- handle navigation

### Trabajo 3
consulta: dónde se guarda el estado del editor al cerrar el proyecto
keep: M1 M12
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda el estado del editor al cerrar el proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save, remember
    extra: make intercept tool name, theme name

T2
    visto: restore workspace session, open file
    extra: Application, set workspace

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → make intercept tool name
  T1 → theme name
  T2 → Application
  T2 → set workspace
