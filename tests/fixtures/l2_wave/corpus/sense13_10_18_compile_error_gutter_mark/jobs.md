### Trabajo 1
consulta: dónde se aplican las decoraciones de margen en el editor para marcar errores
keep: M1
leído: apply visual highlight fold regions, push active file diagnostics from cache, apply lsp diagnostics to panel, line gutter marker, gutter buffer line at row, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo de decoración de margen para errores: se aplica en line gutter marker que delega en line diagnostic marker from map para obtener el carácter para error, W para warning basado en la severidad de los diagnósticos LSP mapeados por línea en diagnostics for editor line . El flujo de datos es: apply lsp diagnostics to panel → push active file diagnostics from cache población de panel diagnostics by line → renderizado de línea → line gutter marker lectura del mapa para el carácter del margen .
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se escanea la salida de la consola inferior para extraer errores de compilación
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se escanea la salida de la consola inferior para extraer errores de compilación
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: apply visual highlight fold regions, push active file diagnostics from cache, apply lsp diagnostics to panel, line gutter marker
    extra: Make Editor Panel, Make Main Layout

T2
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
