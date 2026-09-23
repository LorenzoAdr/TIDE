### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos, save buffer, save state, save workspace session, flush active tab, save
Cerrado:
leído: Cursor Pos, save buffer, save state, save workspace session, flush active tab, save
Abierto:
- run custom event drain
- run background generation

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: Cursor Pos, set primary, set pos
Cerrado:
leído: Cursor Pos, set primary, set pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la lista de archivos abiertos al cargar la sesión del proyecto
keep: M1
leído: restore workspace session
Cerrado:
encontré el mecanismo: restore workspace session itera session open tabs y llama a workspace open file para cada archivo válido.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se guarda el estado de visibilidad de los paneles laterales al cerrar la sesión
keep: M2
leído: Main Layout State, save workspace session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré el mecanismo de persistencia del estado de visibilidad de los paneles laterales; lo leído en save workspace session solo guarda rutas de tabs abiertos y argumentos de lanzamiento, sin tocar Main Layout State ni los latches de visibilidad visible etc.
Abierto:
- run custom event drain


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, save buffer, save state, save workspace session
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, set primary, set pos
    extra: cursor selection span on line, ensure cursors

T3
    visto: restore workspace session
    extra: Application, set workspace

T4
    visto: Main Layout State, save workspace session
    extra: clear busy, on lsp missing install

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  sin camino
  T1=>T4  mismo objeto: save workspace session
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → Application
  T3 → set workspace
