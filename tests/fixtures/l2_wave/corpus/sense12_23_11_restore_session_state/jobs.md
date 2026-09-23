### Trabajo 1
consulta: dónde se guarda el estado del editor al cerrar el proyecto para restaurarlo al abrirlo
keep: M1
leído: workspace config cpp
Cerrado:
leído: workspace config cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M1
leído: workspace config cpp, Cursor Pos
Cerrado:
leído: workspace config cpp, Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la selección del cursor al abrir el proyecto
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restauran los archivos abiertos al abrir el proyecto
keep: M1
leído: workspace config cpp, editor state cpp
Cerrado:
leído: workspace config cpp, editor state cpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: workspace config cpp

T2
    visto: workspace config cpp, Cursor Pos
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T4
    visto: workspace config cpp, editor state cpp

entre abiertas:
  T1=>T2  mismo objeto: workspace config cpp
  T1=>T3  sin camino
  T1=>T4  mismo objeto: workspace config cpp
  T2=>T3  mismo objeto: Cursor Pos
  T2=>T4  mismo objeto: workspace config cpp
  T3=>T4  sin camino
hacia el resto:
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → cursor selection span on line
  T3 → ensure cursors
