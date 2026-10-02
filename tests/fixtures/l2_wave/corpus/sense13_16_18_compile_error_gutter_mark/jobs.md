### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor para marcar un error
keep: M1
leído: line gutter marker, line diagnostic marker from map
Cerrado:
encontré el mecanismo: la línea roja marcador se decide en line diagnostic marker from map 978 que retorna si hay un diagnóstico con k Error este carácter es devuelto por line gutter marker 1005 al UI para pintarlo en el margen.
Abierto:
- diagnostics for editor line

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la consola para crear diagnósticos en el editor
keep: M3 M8
leído: parse obj command output
Cerrado:
leído: parse obj command output
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se crean los diagnósticos de error de compilación a partir de la salida de la consola
keep: M2 M3
leído: diagnostics cpp, diagnostics panel cpp
Cerrado:
leído: diagnostics cpp, diagnostics panel cpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, line diagnostic marker from map
    extra: Make Editor Panel, Make Main Layout

T2
    visto: parse obj command output
    extra: apply core analyzer search result, apply event

T3
    visto: diagnostics cpp, diagnostics panel cpp

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → apply core analyzer search result
  T2 → apply event
