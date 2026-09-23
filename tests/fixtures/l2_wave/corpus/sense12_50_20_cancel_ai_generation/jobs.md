### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de la tecla Escape para cancelar la generación de código; lo leído es una función de escape de strings JSON ai trace escape no un handler de eventos de teclado
Abierto:
- handle ai console keys
- cancel generation

### Trabajo 2
consulta: dónde se captura la tecla Escape en el panel de consola para cancelar la generación
keep: M5
leído: handle ai console keys, clear pending insert, has pending insert, cancel current
Cerrado:
leído: handle ai console keys, clear pending insert, has pending insert, cancel current
Abierto:
- handle route
- handle user input

### Trabajo 3
consulta: dónde se detecta la tecla Escape en el manejo de teclas de la consola para cancelar
keep: M5
leído: handle ai console keys, 3060 3100
Cerrado:
encontré el mecanismo de captura de la tecla Escape para cancelar la generación de código; lo leído es el cuerpo de handle ai console keys en console panel cpp que detecta Escape y llama a ai- clear pending insert cuando hay input activo
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: handle ai console keys, clear pending insert, has pending insert, cancel current
    extra: ai tab active, event is ctrl c

T3
    visto: handle ai console keys, 3060 3100
    extra: clear pending insert, has pending insert

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: handle ai console keys
hacia el resto:
  T2 → ai tab active
  T2 → event is ctrl c
  T3 → ai tab active
  T3 → event is ctrl c
