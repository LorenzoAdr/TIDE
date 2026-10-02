### Trabajo 1
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M1
leído: Cursor Pos, open file at impl, open file at confirmed, reset to single cursor
Cerrado:
encontré el mecanismo de restauración de la posición del cursor al abrir un proyecto: la cadena es open file at confirmed → open file at impl → reset to single cursor line, col → set pos line, col . La posición se restaura en reset to single cursor, que es llamado desde open file at impl con los argumentos line y col recibidos en la apertura.
Abierto:
- open file at confirmed caller

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: restore workspace session, open file
Cerrado:
encontré el mecanismo: restore workspace session itera session open tabs y llama a workspace open file path para cada archivo válido, y luego activa el active tab path
Abierto:
- handle navigation

### Trabajo 3
consulta: dónde se restauran los paneles laterales al abrir un proyecto
keep: M2
leído: restore workspace session, open file, Ai Controller
Cerrado:
leído: restore workspace session, open file, Ai Controller
Abierto:
- handle navigation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, open file at impl, open file at confirmed, reset to single cursor
    extra: cursor selection span on line, ensure cursors

T2
    visto: restore workspace session, open file
    extra: Application, set workspace

T3
    visto: restore workspace session, open file, Ai Controller
    extra: Application, set workspace

entre abiertas:
  T1=>T2  open file at impl → open file impl → open new tab from disk → open file
  T1=>T3  open file at impl → open file impl → open new tab from disk → open file
  T2=>T3  mismo objeto: restore workspace session
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → Application
  T2 → set workspace
  T3 → Application
  T3 → set workspace
