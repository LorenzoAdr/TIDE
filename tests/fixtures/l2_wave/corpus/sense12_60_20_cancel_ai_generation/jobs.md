### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de código
keep: M6 M9
leído: handle ai console keys, cancel level1, 3020 3165, 3045 3060, 3060 3100
Cerrado:
encontré el objeto de la consulta: la captura de Escape para cancelar la generación de código está en handle ai console keys líneas 3068-3074 . Cuando console input active es true y hay una inserción pendiente ai has pending insert se invoca ai clear pending insert .
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se limpia el archivo a medias cuando se cancela la inserción pendiente
keep: M10
leído: clear pending insert
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se limpia el archivo a medias; lo leído clear pending insert solo resetea flags de estado pending insert insert anchor y despierta el ciclo, sin tocar disco ni buffers de texto
Abierto:
- handle route
- clear pending insert file
- truncate pending buffer

### Trabajo 3
consulta: dónde se limpia el archivo a medias cuando se cancela la generación de código
keep: M11 M12
leído: cancel level1
Cerrado:
leído: cancel level1
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, cancel level1, 3020 3165, 3045 3060
    extra: clear pending insert, has pending insert

T2
    visto: clear pending insert
    extra: cancel current, handle user input

T3
    visto: cancel level1
    extra: cancel all, Ai Controller

entre abiertas:
  T1=>T2  handle ai console keys → clear pending insert
  T1=>T3  mismo objeto: cancel level1
  T2=>T3  sin camino
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → cancel current
  T2 → handle user input
  T3 → cancel all
  T3 → Ai Controller
