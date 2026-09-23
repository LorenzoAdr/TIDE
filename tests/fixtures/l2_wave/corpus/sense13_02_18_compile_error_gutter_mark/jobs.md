### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: diagnostic severity tr, diagnostics on line, line diagnostic marker, line diagnostic marker from map, line gutter marker
Cerrado:
encontré el objeto de la consulta: los errores se marcan en el margen gutter mediante la función line gutter marker en editor panel que delega en line diagnostic marker from map para obtener el carácter error o W warning y lo retorna para su renderizado en el gutter de la línea.
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la salida del shell para crear diagnósticos
keep: M5
leído: goto helix diagnostic, count workspace diagnostics
Cerrado:
leído: goto helix diagnostic, count workspace diagnostics
Abierto:
- handle editor keys

### Trabajo 3
consulta: dónde se crean los diagnósticos del workspace a partir de la salida de la consola
keep: M3 M1
leído: diagnostics panel cpp, diagnostics cpp
Cerrado:
leído: diagnostics panel cpp, diagnostics cpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostic severity tr, diagnostics on line, line diagnostic marker, line diagnostic marker from map
    extra: Make Editor Panel, make diagnostic modal

T2
    visto: goto helix diagnostic, count workspace diagnostics
    extra: build helix dispatch context, handle editor keys

T3
    visto: diagnostics panel cpp, diagnostics cpp

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → make diagnostic modal
  T2 → build helix dispatch context
  T2 → handle editor keys
