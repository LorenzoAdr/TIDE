### Trabajo 1
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: editor state cpp, restore workspace session
Cerrado:
encontré el mecanismo: restore workspace session itera session open tabs y llama a workspace open file path para cada archivo válido
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M2
leído: restore workspace session, main layout cpp, main layout hpp, set workspace
Cerrado:
leído: restore workspace session, main layout cpp, main layout hpp, set workspace
Abierto:
- begin workspace bootstrap


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T2
    visto: editor state cpp, restore workspace session
    extra: Application, set workspace

T3
    visto: restore workspace session, main layout cpp, main layout hpp, set workspace
    extra: Application, open file

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: restore workspace session
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → Application
  T2 → open file
  T3 → Application
  T3 → open file
