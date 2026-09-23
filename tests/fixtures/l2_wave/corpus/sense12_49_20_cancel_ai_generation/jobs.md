### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura la tecla Escape para cancelar la generación de la IA
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de la tecla Escape para cancelar la generación de IA; lo leído es ai trace escape, que escapa caracteres en strings JSON, no eventos de teclado
Abierto:
- handle ai console keys
- handle key event
- cancel generation

### Trabajo 2
consulta: dónde se cancela la generación de la IA al hacer clic fuera del editor
keep: M7 M8
leído: cancel current, cancel level1
Cerrado:
leído: cancel current, cancel level1
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se maneja la entrada del usuario para cancelar la generación de IA
keep: M5 M8
leído: handle ai console keys, cancel level1, cancel current, clear pending insert, handle user input
Cerrado:
leído: handle ai console keys, cancel level1, cancel current, clear pending insert, handle user input
Abierto:
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: cancel current, cancel level1
    extra: handle user input, handle route

T3
    visto: handle ai console keys, cancel level1, cancel current, clear pending insert
    extra: has pending insert, ai tab active

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: cancel current
hacia el resto:
  T2 → handle route
  T2 → cancel all
  T3 → has pending insert
  T3 → ai tab active
