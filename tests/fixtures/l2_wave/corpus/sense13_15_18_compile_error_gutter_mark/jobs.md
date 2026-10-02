### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
keep: M2
leído: rebuild diagnostics by line if changed, line gutter marker, line diagnostic marker from map, rebuild diagnostics by line, diagnostics for editor line
Cerrado:
encontré el mecanismo: la línea roja marcador se determina en line gutter marker que delega en line diagnostic marker from map para devolver si hay un diagnóstico de severidad k Error en esa línea. El dibujo visual propiamente dicho ocurre en el renderizado del gutter que consume este carácter, pero la lógica de decisión está aquí.
Abierto:
- render gutter line

### Trabajo 2
consulta: dónde se insertan los diagnósticos de error en el mapa que consulta el marcador del margen
keep: M9
leído: rebuild diagnostics by line, line gutter marker, line diagnostic marker from map
Cerrado:
encontré el objeto de la consulta: la inserción ocurre en rebuild diagnostics by line línea 833: panel diagnostics by line item line push back que es llamado por rebuild diagnostics by line if changed . El marcador del margen line gutter marker consulta ese mapa vía line diagnostic marker from map - diagnostics for editor line .
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se aplican los diagnósticos de LSP al panel del editor para que aparezcan en el margen
keep: M1
leído: apply lsp diagnostics to panel, push active file diagnostics from cache, line diagnostic marker from map, line gutter marker, diagnostics for editor line
Cerrado:
encontré el mecanismo: apply lsp diagnostics to panel llena el estado diagnostics by line y line gutter marker consulta ese estado para pintar el margen
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild diagnostics by line if changed, line gutter marker, line diagnostic marker from map, rebuild diagnostics by line
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

T2
    visto: rebuild diagnostics by line, line gutter marker, line diagnostic marker from map
    extra: rebuild diagnostics by line if changed, apply lsp diagnostics to panel

T3
    visto: apply lsp diagnostics to panel, push active file diagnostics from cache, line diagnostic marker from map, line gutter marker
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  mismo objeto: line gutter marker
  T1=>T3  mismo objeto: line gutter marker
  T1=>T3  apply lsp diagnostics to panel → push active file diagnostics from cache → rebuild diagnostics by line if changed
  T2=>T3  mismo objeto: line gutter marker
  T2=>T3  apply lsp diagnostics to panel → push active file diagnostics from cache → rebuild diagnostics by line if changed → rebuild diagnostics by line
hacia el resto:
  T1 → ISymbol Provider
  T1 → panel diagnostics match doc
  T2 → ISymbol Provider
  T2 → panel diagnostics match doc
  T3 → ISymbol Provider
  T3 → allows lsp ui
