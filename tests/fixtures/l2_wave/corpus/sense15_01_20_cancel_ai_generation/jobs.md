### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M1 M3
leído: cancel level1, ai trace escape, clear hover if
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de captura de Escape o clic fuera para cancelar la IA; lo leído son utilidades de escape de strings ai trace escape y gestión de hover UI clear hover if, que no invocan cancel level1 ni gestionan eventos de teclado ratón globales para cancelación
Abierto:
- handle problems scrollbar mouse
- Event handler global
- key escape handler
- click outside handler

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación de la IA
keep: M3 M6
leído: cancel level1, clear pending insert, cancel current, begin insert at
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de limpieza de archivo a medias al cancelar la IA; lo leído son utilidades de cancelación lógica cancel level1, cancel current, clear pending insert que solo limpian flags en memoria pending insert, insert anchor y no invocan ninguna operación de sistema de archivos remove, unlink, delete file ni gestionan archivos temporales.
Abierto:
- handle route
- handle user input
- write response to file
- save partial response


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: cancel level1, ai trace escape, clear hover if
    extra: cancel all, Ai Controller

T2
    visto: cancel level1, clear pending insert, cancel current, begin insert at
    extra: cancel all, Ai Controller

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T2  clear pending insert → pending insert → ai trace escape
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
  T2 → cancel all
  T2 → Ai Controller
