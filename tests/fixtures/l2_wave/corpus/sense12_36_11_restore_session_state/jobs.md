### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al abrir un proyecto
keep: M1
leído: editor state cpp
Cerrado:
leído: editor state cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M1
leído: editor state cpp, editor state hpp
Cerrado:
leído: editor state cpp, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: editor state hpp, editor state cpp
Cerrado:
leído: editor state hpp, editor state cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M2 M5
leído: editor state hpp, editor state cpp
Cerrado:
leído: editor state hpp, editor state cpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: editor state cpp

T2
    visto: editor state cpp, editor state hpp

T3
    visto: editor state hpp, editor state cpp

T4
    visto: editor state hpp, editor state cpp

entre abiertas:
  T1=>T2  mismo objeto: editor state cpp
  T1=>T3  mismo objeto: editor state cpp
  T1=>T4  mismo objeto: editor state cpp
  T2=>T3  mismo objeto: editor state cpp
  T2=>T4  mismo objeto: editor state cpp
  T3=>T4  mismo objeto: editor state hpp
