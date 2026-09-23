### Trabajo 1
consulta: dónde se captura la tecla Escape o el clic fuera para cancelar la generación de la IA
keep: M2 M8
leído: ai trace escape, cancel level1, cancel current, handle user input, handle route, is cancel input, handle ai console keys
Cerrado:
encontré el objeto de la consulta: la captura de Escape para cancelar la generación ocurre en handle ai console keys líneas 3048-3055 del peek, donde Escape invoca a ai clear pending insert si hay una inserción pendiente, o ai cancel current si el agente está ocupado. La consulta también menciona clic fuera, pero no hay evidencia de captura de clics para cancelar en el código leído; solo se maneja la tecla Escape y comandos de texto como cancel en is cancel input .
Abierto:
- clic fuera cancelacion

### Trabajo 2
consulta: dónde se limpia el archivo o se revierte la inserción pendiente al cancelar la generación
keep: M8
leído: clear pending insert, cancel current
Cerrado:
encontré el mecanismo de limpieza: al cancelar una inserción pendiente pending insert, se invoca clear pending insert desde cancel current, lo cual resetea el flag y el anchor, pero NO borra el archivo ni revierte texto en el buffer; la reversión de inserción es lógica de estado, no de contenido.
Abierto:
- handle route
- handle user input
- cancel all

### Trabajo 3
consulta: dónde se revierte el contenido del buffer de texto al cancelar la inserción pendiente
keep: M8
leído: clear pending insert, cancel current
Cerrado:
encontré el mecanismo de reversión: en clear pending insert se resetea el estado pending insert false insert anchor y se despierta el sistema wake sin manipulación directa del buffer de texto visible en el cuerpo leído
Abierto:
- handle route
- handle user input
- insert anchor


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel level1, cancel current, handle user input
    extra: cancel all, Ai Controller

T2
    visto: clear pending insert, cancel current
    extra: handle user input, handle route

T3
    visto: clear pending insert, cancel current
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel current
  T1=>T2  clear pending insert → pending insert → ai trace escape
  T1=>T3  mismo objeto: cancel current
  T1=>T3  clear pending insert → pending insert → ai trace escape
  T2=>T3  mismo objeto: clear pending insert
hacia el resto:
  T1 → cancel all
  T1 → Ai Controller
  T2 → cancel all
  T3 → cancel all
