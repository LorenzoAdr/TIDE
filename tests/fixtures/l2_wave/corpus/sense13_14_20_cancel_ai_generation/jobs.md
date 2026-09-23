### Trabajo 1
consulta: cómo se cancela la generación de código al pulsar Escape
keep: M4
leído: cancel current, handle user input, handle route, is cancel input
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de cancelación al pulsar Escape; lo leído muestra que is cancel input solo detecta comandos de texto cancel cancel cancelar y no la tecla Escape, y handle user input no contiene lógica para Escape ni llama a cancel current en ese caso
Abierto:
- handle escape key
- handle key

### Trabajo 2
consulta: dónde se captura la tecla Escape durante la generación de código
keep: M1 M2
leído: skip escape, ai trace escape, handle editor escape, handle editor keys, Event
Cerrado:
leído: skip escape, ai trace escape, handle editor escape, handle editor keys, Event
Abierto:
- end mouse selection

### Trabajo 3
consulta: dónde se captura el clic fuera del editor para cancelar la generación
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se captura el clic fuera del editor para cancelar la generación
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: cancel current, handle user input, handle route, is cancel input
    extra: clear pending insert, cancel all

T2
    visto: skip escape, ai trace escape, handle editor escape, handle editor keys
    extra: handle csi, clear primary selection

T3
    visto: (nada)

entre abiertas:
  T1=>T2  cancel current → ai trace escape
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → clear pending insert
  T1 → cancel all
  T2 → handle csi
  T2 → clear primary selection
