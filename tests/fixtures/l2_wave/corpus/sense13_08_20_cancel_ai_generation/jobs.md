### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M1 M4
leído: ai trace escape, cancel all, cancel level1
Cerrado:
leído: ai trace escape, cancel all, cancel level1
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se limpia el archivo parcialmente escrito al cancelar la generación
keep: M2
leído: clear pending insert, cancel level1, cancel current, cancel all, begin insert at
Cerrado:
leído: clear pending insert, cancel level1, cancel current, cancel all, begin insert at
Abierto:
- handle route
- handle user input


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: ai trace escape, cancel all, cancel level1
    extra: Ai Controller

T2
    visto: clear pending insert, cancel level1, cancel current, cancel all
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: cancel all
  T1=>T2  clear pending insert → pending insert → ai trace escape
hacia el resto:
  T1 → Ai Controller
  T2 → handle user input
  T2 → handle route
