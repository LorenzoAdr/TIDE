### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: close, close tab, flush active tab
Cerrado:
leído: close, close tab, flush active tab
Abierto:
- run job

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: restore workspace session, reopen workspace documents, open file, open file impl, open new tab from disk
Cerrado:
encontré el mecanismo: restore workspace session carga Workspace Session y llama a workspace open file para cada pestaña y la activa; open file delega en open file impl y open new tab from disk para cargar el contenido y crear la pestaña en el modelo
Abierto:
- restart lsp for workspace
- handle navigation

### Trabajo 3
consulta: dónde se controla la visibilidad de los paneles laterales
keep: M5
leído: handle source panel event, secondary visible, On Event, Main Layout State
Cerrado:
leído: handle source panel event, secondary visible, On Event, Main Layout State
Abierto:
- handle gdb console keys


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: close, close tab, flush active tab
    extra: request start, run job

T2
    visto: restore workspace session, reopen workspace documents, open file, open file impl
    extra: Application, set workspace

T3
    visto: handle source panel event, secondary visible, On Event, Main Layout State
    extra: Character, handle source scrollbar mouse

entre abiertas:
  T1=>T2  restore workspace session → last launch program → open launch wizard → prepare connection wizard → exit debug mode
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → request start
  T1 → run job
  T2 → Application
  T2 → set workspace
  T3 → Character
  T3 → handle source scrollbar mouse
