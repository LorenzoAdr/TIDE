### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input, handle ai console keys
Cerrado:
encontré la captura de Escape para cancelar la generación: en handle ai console keys se intercepta Escape y llama a ai clear pending insert y wake console cuando hay una inserción pendiente; la cancelación de generación activa busy se gestiona vía comando cancel en is cancel input y handle user input que invoca cancel current . No hay evidencia de captura de clic fuera para cancelar en los loci leídos.
Abierto:
- clic fuera para cancelar

### Trabajo 2
consulta: cómo se limpia el archivo cuando se cancela la inserción pendiente
keep: M8
leído: clear pending insert
Cerrado:
encontré el mecanismo de limpieza: clear pending insert resetea el flag y el anchor, pero NO limpia el archivo buffer de texto; la consulta asume una limpieza de archivo que no existe en este código solo estado interno
Abierto:
- handle route
- clear pending insert

### Trabajo 3
consulta: qué hace cancel current con el buffer de texto del archivo cuando se cancela la generación activa
keep: M8
leído: cancel current, clear pending insert
Cerrado:
encontré el efecto sobre el buffer de texto: cancel current llama a clear pending insert, que resetea pending insert a false y vacía insert anchor
Abierto:
- handle user input
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cancel all, Ai Controller

T2
    visto: clear pending insert
    extra: cancel current, handle user input

T3
    visto: cancel current, clear pending insert
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  mismo objeto: cancel current
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T2=>T3  mismo objeto: clear pending insert
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
  T3 → cancel all
