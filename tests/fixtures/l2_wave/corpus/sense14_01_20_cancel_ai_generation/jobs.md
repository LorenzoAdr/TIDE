### Trabajo 1
consulta: dónde se cancela la generación de código al pulsar Escape
keep: M4
leído: handle editor escape, cancel current, cancel inflight completion, handle editor keys, handle user input, handle route
Cerrado:
leído: handle editor escape, cancel current, cancel inflight completion, handle editor keys, handle user input, handle route
Abierto:
- send completion request
- end mouse selection
- send cancel

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de código
keep: M6
leído: cancel current, clear pending insert, cancel all, cancel level1, cancel, begin insert at
Cerrado:
leído: cancel current, clear pending insert, cancel all, cancel level1, cancel, begin insert at
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: dónde se cancela la generación de código al hacer clic fuera del editor
keep: M3
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se cancela la generación de código al hacer clic fuera del editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle editor escape, cancel current, cancel inflight completion, handle editor keys
    extra: clear primary selection, clear snippet session

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle user input, handle route

T3
    visto: cancel all

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T3  cancel current → cancel all
  T2=>T3  mismo objeto: cancel all
hacia el resto:
  T1 → clear primary selection
  T1 → clear snippet session
  T2 → Ai Controller
  T2 → Symbol Filter Runner
