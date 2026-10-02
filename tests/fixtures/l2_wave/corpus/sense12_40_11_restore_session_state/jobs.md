### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al abrir un proyecto
keep: M1
leído: editor state cpp, editor state hpp
Cerrado:
leído: editor state cpp, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M1
leído: editor state cpp, editor state hpp, set primary, reset to single cursor
Cerrado:
leído: editor state cpp, editor state hpp, set primary, reset to single cursor
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: editor state hpp, set primary, reset to single cursor
Cerrado:
leído: editor state hpp, set primary, reset to single cursor
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M2 M5
leído: set primary, reset to single cursor
Cerrado:
leído: set primary, reset to single cursor
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: editor state cpp, editor state hpp

T2
    visto: editor state cpp, editor state hpp, set primary, reset to single cursor
    extra: ensure cursors, set pos

T3
    visto: editor state hpp, set primary, reset to single cursor
    extra: ensure cursors, set pos

T4
    visto: set primary, reset to single cursor
    extra: ensure cursors, set pos

entre abiertas:
  T1=>T2  mismo objeto: editor state cpp
  T1=>T3  mismo objeto: editor state hpp
  T1=>T4  sin camino
  T2=>T3  mismo objeto: editor state hpp
  T2=>T4  mismo objeto: set primary
  T3=>T4  mismo objeto: set primary
hacia el resto:
  T2 → ensure cursors
  T2 → set pos
  T3 → ensure cursors
  T3 → set pos
  T4 → ensure cursors
  T4 → set pos
