### Trabajo 1
consulta: dónde se crean los indicadores visuales de error en el margen del editor
keep: M3
leído: diagnostics for editor line, diagnostic severity color, diagnostics on line, Make Editor Panel
Cerrado:
encontré el objeto de la consulta: los indicadores visuales de error en el margen gutter markers W se crean en el bucle de renderizado de Make Editor Panel editor panel cpp, donde se itera sobre viewport lines se obtienen los diagnostics vía diagnostics for editor line y se asigna el carácter del marcador gutter marker basado en la severidad. El color se deriva de diagnostic severity color .
Abierto:
- handle editor mouse
- show diagnostic suffix on line

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la consola
keep: M11
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
no encontré el parsing de errores de compilación de la consola; lo leído es el parsing de errores de linker undefined references en scan shell output for linker errors, que es un subconjunto específico y no el sistema general de diagnóstico de compilación
Abierto:
- parse linker undefined reference
- diagnostics for editor line
- diagnostic severity color
- diagnostics on line

### Trabajo 3
consulta: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
keep: M11
leído: parse linker undefined reference
Cerrado:
leído: parse linker undefined reference
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se crean los diagnósticos de compilación a partir del texto de la consola
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se crean los diagnósticos de compilación a partir del texto de la consola
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, diagnostic severity color, diagnostics on line, Make Editor Panel
    extra: Make Main Layout, make diagnostic modal

T2
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, parse linker undefined reference

T3
    visto: parse linker undefined reference
    extra: scan shell output for linker errors, tick binary symbols panel

T4
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  extra toca visto: parse linker undefined reference
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → make diagnostic modal
  T2 → tick binary symbols panel
  T2 → has value
  T3 → tick binary symbols panel
  T3 → compute scrollbar layout
