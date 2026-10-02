### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M9
leído: close, reset to single cursor, save state
Cerrado:
leído: close, reset to single cursor, save state
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al abrir el proyecto
keep: M5 M4
leído: reopen workspace documents, run, load
Cerrado:
encontré el mecanismo de restauración: reopen workspace documents itera sobre workspace tabs y llama a symbols document opened para cada archivo; se invoca desde run tras la inicialización de la UI y el backend, y los datos de qué archivos restaurar provienen del estado persistente del workspace no de recent projects que solo gestiona la lista de proyectos recientes .
Abierto:
- restart lsp for workspace

### Trabajo 3
consulta: dónde se controla la visibilidad de los paneles laterales
keep: M5 M11
leído: handle source panel event, move right, secondary visible
Cerrado:
leído: handle source panel event, move right, secondary visible
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: close, reset to single cursor, save state
    extra: http exchange unlocked, http close unlocked

T2
    visto: reopen workspace documents, run, load
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: handle source panel event, move right, secondary visible
    extra: Character, handle source scrollbar mouse

entre abiertas:
  T1=>T2  Make Main Layout → Make Editor Panel → handle editor keys → reset to single cursor
  T1=>T3  sin camino
  T2=>T3  Make Main Layout → Make Source Panel → handle source panel event
hacia el resto:
  T1 → http exchange unlocked
  T1 → http close unlocked
  T2 → restart lsp for workspace
  T2 → on lsp missing install
  T3 → Character
  T3 → handle source scrollbar mouse
