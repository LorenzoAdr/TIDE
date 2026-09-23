### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
keep: M2
leído: rebuild diagnostics by line
Cerrado:
leído: rebuild diagnostics by line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la salida de la consola y se convierten en diagnósticos del editor
keep: M8
leído: Make Diagnostics Panel, build rows
Cerrado:
leído: Make Diagnostics Panel, build rows
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se aplican los diagnósticos de la consola al panel del editor para que se dibujen
keep: M1
leído: apply lsp diagnostics to panel, sync diagnostic cache, push active file diagnostics from cache, cached file diagnostics
Cerrado:
encontré el objeto de la consulta: los diagnósticos se aplican al estado del panel en apply lsp diagnostics to panel que llena diagnostics by line vía push active file diagnostics from cache y se dibujan en el render del editor que lee esos mapas y invalida la vista vía invalidate editor view
Abierto:
- render loop
- draw line decorations


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: rebuild diagnostics by line
    extra: rebuild diagnostics by line if changed, apply lsp diagnostics to panel

T2
    visto: Make Diagnostics Panel, build rows
    extra: Make Console Panel, Make Main Layout

T3
    visto: apply lsp diagnostics to panel, sync diagnostic cache, push active file diagnostics from cache, cached file diagnostics
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  apply lsp diagnostics to panel → push active file diagnostics from cache → rebuild diagnostics by line if changed → rebuild diagnostics by line
  T2=>T3  extra toca visto: build rows
hacia el resto:
  T1 → rebuild diagnostics by line if changed
  T1 → ISymbol Provider
  T2 → Make Console Panel
  T2 → Make Main Layout
  T3 → ISymbol Provider
  T3 → allows lsp ui
