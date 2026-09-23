### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M6 M7
leído: visual highlight cpp, clear editor line paint caches
Cerrado:
leído: visual highlight cpp, clear editor line paint caches
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M1 M2
leído: refresh terminal view, terminal viewport lines, consume output pending, drain output bytes
Cerrado:
leído: refresh terminal view, terminal viewport lines, consume output pending, drain output bytes
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: visual highlight cpp, clear editor line paint caches

T2
    visto: refresh terminal view, terminal viewport lines, consume output pending, drain output bytes
    extra: build console panel view, render shell terminal

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T2 → build console panel view
  T2 → render shell terminal
