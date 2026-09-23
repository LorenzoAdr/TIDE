### Trabajo 1
consulta: dónde se parsean los errores de compilación del texto de la consola inferior
keep: M7
leído: scan shell output for linker errors
Cerrado:
leído: scan shell output for linker errors
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja una línea roja en el margen izquierdo del editor
keep: M1
leído: clear editor line paint caches
Cerrado:
leído: clear editor line paint caches
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, parse linker undefined reference

T2
    visto: clear editor line paint caches

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → tick binary symbols panel
  T1 → parse linker undefined reference
