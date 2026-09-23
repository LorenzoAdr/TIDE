### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
keep: M2
leído: handle ai console keys, clear pending insert, has pending insert, cancel current
Cerrado:
leído: handle ai console keys, clear pending insert, has pending insert, cancel current
Abierto:
- handle route
- handle user input

### Trabajo 2
consulta: cómo se revierten los cambios en el archivo al cancelar la generación de código
keep: M8
leído: clear pending insert, cancel current, cancel all, busy, cancel level1, rollback pending
Cerrado:
encontré el mecanismo de reversión: al cancelar, si hay un insert pendiente y no hay actividad de fondo, se limpia el estado clear pending insert si hay actividad de fondo busy se cancelan las tareas cancel all . La reversión física de los cambios en el archivo se realiza mediante rollback pending en level2 session que restaura el contenido previo p before de los archivos afectados. El flujo completo es: cancel current decide limpiar estado pendiente o cancelar tareas; rollback pending es la función que efectivamente escribe el contenido original en los archivos.
Abierto:
- handle route
- handle user input
- load state
- save state

### Trabajo 3
consulta: dónde se decide si hay actividad de fondo busy para cancelar las tareas en lugar de limpiar el estado pendiente
keep: M8
leído: cancel current, cancel all, busy, clear pending insert
Cerrado:
leído: cancel current, cancel all, busy, clear pending insert
Abierto:
- handle user input
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, clear pending insert, has pending insert, cancel current
    extra: ai tab active, event is ctrl c

T2
    visto: clear pending insert, cancel current, cancel all, busy
    extra: handle user input, handle route

T3
    visto: cancel current, cancel all, busy, clear pending insert
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: clear pending insert
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all
  T1=>T3  mismo objeto: clear pending insert
  T1=>T3  handle ai console keys → handle user input → handle route → cancel current → cancel all
  T2=>T3  mismo objeto: clear pending insert
hacia el resto:
  T1 → ai tab active
  T1 → event is ctrl c
  T2 → handle user input
  T2 → handle route
  T3 → handle user input
  T3 → handle route
