### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: Cursor Pos, context menu open editor background, context menu open editor symbol, open file at impl, reset to single cursor
Cerrado:
encontré el mecanismo: al abrir el editor vía open file at impl, se llama a reset to single cursor que establece la posición del cursor en la línea y columna especificadas
Abierto:
- handle editor mouse

### Trabajo 3
consulta: dónde se restaura la lista de archivos abiertos al reabrir el proyecto
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la lista de archivos abiertos al reabrir el proyecto
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restaura la visibilidad de los paneles laterales al reabrir el proyecto
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la visibilidad de los paneles laterales al reabrir el proyecto
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, context menu open editor background, context menu open editor symbol, open file at impl
    extra: cursor selection span on line, ensure cursors

T3
    visto: (nada)

T4
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
