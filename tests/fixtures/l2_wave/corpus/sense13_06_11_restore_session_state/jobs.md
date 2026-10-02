### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M8
leído: editor text hpp, workspace model cpp, editor panel cpp
Cerrado:
leído: editor text hpp, workspace model cpp, editor panel cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la lista de archivos abiertos al iniciar la sesión
keep: M4
leído: reopen workspace documents, run, load, restore workspace session
Cerrado:
encontré el objeto de la consulta: la restauración de la lista de archivos abiertos ocurre en restore workspace session que carga load y itera sobre session open tabs llamando a workspace open file para cada uno.
Abierto:
- restart lsp for workspace
- ensure backend started

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al iniciar la sesión
keep: M8
leído: restore workspace session, load, editor panel cpp, workspace model cpp
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que restaura la visibilidad de los paneles laterales al iniciar la sesión; lo leído restore workspace session, load, editor panel, workspace model solo gestiona la apertura de archivos y el estado del editor, sin tocar la visibilidad de paneles laterales diagnostics, search, etc.
Abierto:
- restore ui layout
- panel visibility state
- workspace layout restore


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: editor text hpp, workspace model cpp, editor panel cpp

T2
    visto: reopen workspace documents, run, load, restore workspace session
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: restore workspace session, load, editor panel cpp, workspace model cpp
    extra: set workspace, Application

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: workspace model cpp
  T2=>T3  mismo objeto: load
hacia el resto:
  T2 → restart lsp for workspace
  T2 → on lsp missing install
  T3 → set workspace
  T3 → Application
