### Trabajo 1
consulta: dónde se pinta la línea roja en el margen del editor para marcar un error
keep: M1 M5
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se pinta la línea roja en el margen del editor para marcar un error
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M12
leído: problems tab active, Event, console panel hpp, Make Console Panel, Make Diagnostics Panel, build rows, diagnostics display allowed, diagnostics for translation unit
Cerrado:
encontré el mecanismo de lectura de errores: la consola inferior Make Console Panel integra Make Diagnostics Panel, que lee los diagnostics de LSP vía diagnostics for translation unit en build rows y los renderiza como filas de problemas.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: problems tab active, Event, console panel hpp, Make Console Panel
    extra: Make Busy Strip Placeholder, colors revision

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T2 → Make Busy Strip Placeholder
  T2 → colors revision
