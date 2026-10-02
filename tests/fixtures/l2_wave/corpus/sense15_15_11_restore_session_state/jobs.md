### Trabajo 1
consulta: dónde se guarda y restaura el estado de la sesión al abrir un proyecto
keep: M1 M7
leído: save state, load state, state path, write file, read file, dir for
Cerrado:
encontré el mecanismo de guardado y restauración del estado de la sesión: se serializa a JSON en save state y se lee en load state . La ruta se construye en state path usando dir for que apunta a state json .
Abierto:
- handle route
- handle level2 harness
- run level1 async

### Trabajo 2
consulta: qué datos del editor y los paneles se incluyen en el estado que se guarda y restaura
keep: M1 M2 M5
leído: save state, load state
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré datos del editor ni de los paneles en el estado guardado; lo leído load state serializa exclusivamente el estado de la sesión de IA turn, phase, workflow, pending hunks, counters, etc., sin campos de UI como scroll, selección o paneles
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: save state, load state, state path, write file
    extra: contains, is array

T2
    visto: save state, load state
    extra: state path, write file

entre abiertas:
  T1=>T2  mismo objeto: save state
hacia el resto:
  T1 → contains
  T1 → is array
  T2 → contains
  T2 → is array
