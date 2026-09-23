### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la IA
keep: M1 M7
leído: handle ai console keys
Cerrado:
leído: handle ai console keys
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se limpia el archivo cuando se cancela la generación de la IA
keep: M2 M10
leído: clear pending insert, cancel level1, cancel current, cancel all, begin insert at
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la limpieza del archivo al cancelar la generación de IA; lo leído clear pending insert, cancel current, cancel all solo limpia estado en memoria pending insert, agent cancel y cancela tareas en cola, pero no hay escritura ni truncado de archivo en disco en la rama de cancelación
Abierto:
- handle route
- handle user input
- tasks cancel


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys
    extra: clear pending insert, has pending insert

T2
    visto: clear pending insert, cancel level1, cancel current, cancel all
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  handle ai console keys → clear pending insert
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → handle user input
  T2 → handle route
