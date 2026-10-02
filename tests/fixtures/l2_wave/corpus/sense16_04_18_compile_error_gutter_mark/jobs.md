### Trabajo 1
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M1 M2
leído: diagnostics panel cpp, dap backend cpp, diagnostics cpp, symbol provider hpp, diagnostics for translation unit, workspace diagnostics
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de lectura de errores de compilación de la consola inferior; lo leído es el panel de diagnósticos LSP diagnostics panel que consume workspace diagnostics del servidor de lenguaje, no la salida del compilador build output .
Abierto:
- build output parser
- read build errors
- parse compile errors

### Trabajo 2
consulta: dónde se parsea la salida del compilador en la consola inferior
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsea la salida del compilador en la consola inferior
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se extraen las líneas de error de la salida del compilador en la consola inferior
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen las líneas de error de la salida del compilador en la consola inferior
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics panel cpp, dap backend cpp, diagnostics cpp, symbol provider hpp
    extra: Make Main Layout, build include tree

T2
    visto: (nada)

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → build include tree
