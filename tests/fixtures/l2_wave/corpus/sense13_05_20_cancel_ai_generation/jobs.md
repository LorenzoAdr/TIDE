### Trabajo 1
consulta: dónde se captura la pulsación de Escape o el clic fuera para interrumpir la generación de la IA
keep: M1 M2
leído: skip escape, ai trace escape
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la captura de Escape clic para interrumpir la IA; lo leído son parsers de secuencias de escape del terminal skip escape y escapado de strings para logs ai trace escape, sin lógica de interrupción
Abierto:
- handle csi
- clear hover if

### Trabajo 2
consulta: dónde se limpia el archivo parcialmente escrito al cancelar la generación de la IA
keep: M1
leído: cancel current, clear pending insert, cancel all, busy, begin insert at
Cerrado:
leído: cancel current, clear pending insert, cancel all, busy, begin insert at
Abierto:
- handle user input
- handle route


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: skip escape, ai trace escape
    extra: handle csi

T2
    visto: cancel current, clear pending insert, cancel all, busy
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  cancel current → ai trace escape
hacia el resto:
  T1 → handle csi
  T2 → handle user input
  T2 → handle route
