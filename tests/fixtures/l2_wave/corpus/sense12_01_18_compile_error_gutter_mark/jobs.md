### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen del editor para marcar un error
keep: M1
leído: diagnostics for editor line
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se dibuja la línea roja en el margen del editor para marcar un error
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
leído: diagnostics for editor line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se extraen los errores de compilación del texto de la consola
keep: M11
leído: scan shell output for linker errors
Cerrado:
encontré el objeto de la consulta: la extracción de errores de la consola ocurre en scan shell output for linker errors que parsea referencias indefinidas del linker línea por línea
Abierto:
- parse linker undefined reference


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

T2
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → tick binary symbols panel
  T2 → has value
