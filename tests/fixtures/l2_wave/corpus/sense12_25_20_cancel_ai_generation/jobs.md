### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré la captura de la tecla Escape ni del clic fuera para cancelar la generación de IA; lo leído es una función de escape de caracteres en strings ai trace escape y un método que marca un flag de cancelación cancel level1, pero no el handler de eventos de teclado ratón que dispara la cancelación
Abierto:
- handle ai console keys
- handle key event
- on mouse click
- cancel generation trigger

### Trabajo 2
consulta: dónde se limpia el archivo o se revierte la inserción a medias al cancelar la IA
keep: M8
leído: clear pending insert, cancel current, cancel all, busy, cancel level1
Cerrado:
encontré el mecanismo de limpieza y reversión al cancelar la IA: cancel current revierte la inserción a medias vía clear pending insert limpia pending insert e insert anchor, y cancel level1 marcan el flag agent cancel para abortar la generación; no se limpian archivos en disco, solo estado en memoria
Abierto:
- handle route
- handle user input
- agent cancel

### Trabajo 3
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M5 M8
leído: handle ai console keys, Event, cancel current, cancel level1, handle user input, handle route
Cerrado:
leído: handle ai console keys, Event, cancel current, cancel level1, handle user input, handle route
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1
    extra: cancel all, Ai Controller

T2
    visto: clear pending insert, cancel current, cancel all, busy
    extra: handle user input, handle route

T3
    visto: handle ai console keys, Event, cancel current, cancel level1
    extra: clear pending insert, has pending insert

entre abiertas:
  T1=>T2  mismo objeto: cancel level1
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  mismo objeto: cancel level1
  T1=>T3  handle ai console keys → handle user input → ai trace escape
  T2=>T3  mismo objeto: cancel current
  T2=>T3  handle ai console keys → clear pending insert
hacia el resto:
  T1 → Ai Controller
  T2 → Ai Controller
  T2 → is continuable
  T3 → has pending insert
  T3 → ai tab active
