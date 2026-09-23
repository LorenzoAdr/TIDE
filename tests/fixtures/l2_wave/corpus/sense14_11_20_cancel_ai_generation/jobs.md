### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
keep: M7 M11
leído: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert
Cerrado:
encontré el objeto de la consulta: la captura de Escape y clic fuera para cancelar la generación de código está en handle ai console keys tecla Escape y handle console panel mouse clic fuera . Ambos invocan a clear pending insert para limpiar el estado de inserción pendiente, que es el efecto de cancelación.
Abierto:
- handle route
- cancel current

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se limpia el archivo a medias al cancelar la generación de la IA
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: qué hace clear pending insert para evitar que el archivo quede a medias
keep: M1 M2
leído: clear pending insert, begin insert at, handle user input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo: clear pending insert evita que el archivo quede a medias o en un estado inconsistente limpiando el estado de intención pending insert e insert anchor y notificando a la UI wake, lo que impide que se ejecute una inserción fantasma o duplicada si el usuario cancela o si el flujo se interrumpe. La protección no es a nivel de escritura de archivo no hay rollback de disco, sino a nivel de estado del controlador: al resetear pending insert a false y vaciar insert anchor, se asegura que handle user input no en la rama de ejecución de inserción run insert async con datos obsoletos o nulos.
Abierto:
- handle route
- run insert async


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, handle console panel mouse, clear pending insert, has pending insert
    extra: ai tab active, event is ctrl c

T2
    visto: (nada)

T3
    visto: clear pending insert, begin insert at, handle user input
    extra: cancel current, handle route

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: clear pending insert
  T1=>T3  handle ai console keys → handle user input → handle route → run level1 async → begin insert at
  T2=>T3  sin camino
hacia el resto:
  T1 → ai tab active
  T1 → event is ctrl c
  T3 → cancel current
  T3 → handle route
