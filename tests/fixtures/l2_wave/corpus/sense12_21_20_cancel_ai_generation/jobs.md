### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para cancelar la generación de la IA
keep: M2
leído: handle ai console keys, cancel current, clear pending insert
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
encontré el objeto de la consulta: la pulsación de Escape se captura en handle ai console keys línea 3040 del peek, bloque if tras la validación de selección y antes de ai console keys active . El efecto de cancelación se ejecuta vía cancel current llamado desde handle user input o handle route que invoca clear pending insert para limpiar el estado pendiente. No se encontró captura de clic fuera en los loci leídos; el mecanismo de cancelación por UI parece estar acoplado a la tecla Escape en el panel de consola.
Abierto:
- handle user input
- handle route

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M8
leído: clear pending insert, cancel current, cancel all, wake, cancel level1, begin insert at
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré dónde se limpia el archivo a medias; lo leído muestra que cancel current solo limpia estado interno pending insert, agent cancel y despierta la UI, sin truncar ni restaurar el buffer del editor
Abierto:
- handle route
- handle user input
- truncate buffer
- restore snapshot
- apply insert

### Trabajo 3
consulta: dónde se restaura el contenido del archivo a su estado anterior al iniciar la generación de la IA
keep: M7
leído: snapshot lines, rollback pending
Cerrado:
encontré el objeto de la consulta: la restauración del contenido del archivo a su estado anterior ocurre en rollback pending línea 8119 de level2 session que itera sobre st pending y escribe p before en p abs path mediante write text file .
Abierto:
- load state
- save state

### Trabajo 4
consulta: dónde se invoca la restauración del archivo al cancelar la generación de la IA
keep: M8
leído: clear pending insert, rollback pending, cancel current, cancel level1, cancel all
Cerrado:
leído: clear pending insert, rollback pending, cancel current, cancel level1, cancel all
Abierto:
- handle route
- handle user input


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, cancel current, clear pending insert
    extra: has pending insert, ai tab active

T2
    visto: clear pending insert, cancel current, cancel all, wake
    extra: handle user input, handle route

T3
    visto: snapshot lines, rollback pending
    extra: write text file, load state

T4
    visto: clear pending insert, rollback pending, cancel current, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all
  T1=>T3  handle ai console keys → snapshot lines
  T1=>T4  mismo objeto: cancel current
  T1=>T4  handle ai console keys → handle user input → handle route → cancel current → cancel all → cancel level1
  T2=>T3  sin camino
  T2=>T4  mismo objeto: clear pending insert
  T2=>T4  rollback pending → pack span hint
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → handle user input
  T2 → handle route
  T3 → write text file
  T3 → load state
