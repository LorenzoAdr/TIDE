### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de código
keep: M7 M8
leído: handle ai console keys, handle console panel mouse, clear pending insert, cancel current, Event
Cerrado:
encontré el objeto de la consulta: la captura de Escape y clic fuera para cancelar la generación de código está en handle ai console keys y handle console panel mouse
Abierto:
- handle route
- handle user input
- cancel all

### Trabajo 2
consulta: dónde se limpia el archivo a medias al cancelar la generación
keep: M6 M10
leído: cancel current, clear pending insert, cancel all, cancel level1
Cerrado:
leído: cancel current, clear pending insert, cancel all, cancel level1
Abierto:
- handle user input
- handle route


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: handle ai console keys, handle console panel mouse, clear pending insert, cancel current
    extra: has pending insert, ai tab active

T2
    visto: cancel current, clear pending insert, cancel all, cancel level1
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: clear pending insert
  T1=>T2  handle ai console keys → handle user input → handle route → cancel current → cancel all
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → handle user input
  T2 → handle route
