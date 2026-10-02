### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: handle ai console keys, ai controller cpp, cancel current, clear pending insert, handle user input, handle route, is cancel input
Cerrado:
encontré el objeto de la consulta: la pulsación de Escape se captura en handle ai console keys línea 3045 del peek, bloque if que llama a ai clear pending insert para cancelar una inserción pendiente, o bien a clear terminal selection si hay selección. La cancelación de generación activa busy se hace vía texto cancel en handle user input - is cancel input - cancel current . No hay evidencia de clic fuera en el código leído; el sistema usa teclado y comandos de texto.
Abierto:
- clic fuera cancel

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
leído: clear pending insert, cancel current, cancel all, cancel level1
Abierto:
- handle route
- handle user input

### Trabajo 3
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: clear pending insert, cancel current, cancel all, cancel level1
Cerrado:
leído: clear pending insert, cancel current, cancel all, cancel level1
Abierto:
- handle route
- handle user input

### Trabajo 4
consulta: dónde se revierte el contenido del archivo al cancelar la generación de la IA
keep: M8
leído: clear pending insert, cancel current, cancel all, busy, cancel level1, rollback pending, snapshot lines
Cerrado:
encontré el mecanismo de reversión: al cancelar, si hay un insert pendiente y no hay busy, se llama a clear pending insert que solo limpia estado interno; si hay busy, se llama a cancel all - cancel level1 La reversión real del contenido del archivo ocurre en rollback pending, que itera sobre st.pending y escribe p.before en p abs path usando write text file Sin embargo, no hay un camino directo leído desde cancel all hacia rollback pending en el cuaderno actual; rollback pending parece ser invocado por otra rama posiblemente al finalizar la tarea cancelada o por el loop autónomo L2 .
Abierto:
- handle route
- handle user input
- llamador de rollback pending desde la ruta de cancelación


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, ai controller cpp, cancel current, clear pending insert
    extra: has pending insert, ai tab active

T2
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

T3
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

T4
    visto: clear pending insert, cancel current, cancel all, busy
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all
  T1=>T3  mismo objeto: cancel current
  T1=>T3  handle ai console keys → handle user input → handle route → cancel current → cancel all
  T1=>T4  mismo objeto: cancel current
  T1=>T4  handle ai console keys → handle user input → handle route → cancel current → cancel all
  T2=>T3  mismo objeto: clear pending insert
  T2=>T4  mismo objeto: clear pending insert
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → Ai Controller
  T3 → Ai Controller
  T3 → Symbol Filter Runner
  T4 → Ai Controller
