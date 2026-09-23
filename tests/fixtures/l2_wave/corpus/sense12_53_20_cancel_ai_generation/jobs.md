### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M4
leído: handle editor escape, handle editor keys, cancel all, cancel level1
Cerrado:
leído: handle editor escape, handle editor keys, cancel all, cancel level1
Abierto:
- end mouse selection

### Trabajo 2
consulta: dónde se limpia el archivo al cancelar la generación de la IA
keep: M1
leído: clear pending insert, cancel all, cancel current, cancel level1
Cerrado:
leído: clear pending insert, cancel all, cancel current, cancel level1
Abierto:
- handle route
- handle user input

### Trabajo 3
consulta: dónde se cancela la generación cuando el usuario hace clic fuera del editor
keep: M2
leído: cancel level1, handle editor mouse, end mouse selection, cancel all, Event
Cerrado:
leído: cancel level1, handle editor mouse, end mouse selection, cancel all, Event
Abierto:
- handle editor keys
- handle editor escape


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: handle editor escape, handle editor keys, cancel all, cancel level1
    extra: clear primary selection, clear snippet session

T2
    visto: clear pending insert, cancel all, cancel current, cancel level1
    extra: handle user input, handle route

T3
    visto: cancel level1, handle editor mouse, end mouse selection, cancel all
    extra: Ai Controller, active tab git diff view

entre abiertas:
  T1=>T2  mismo objeto: cancel all
  T1=>T3  mismo objeto: cancel all
  T1=>T3  handle editor escape → end mouse selection
  T2=>T3  mismo objeto: cancel all
hacia el resto:
  T1 → clear primary selection
  T1 → clear snippet session
  T2 → handle user input
  T2 → handle route
  T3 → Ai Controller
  T3 → active tab git diff view
