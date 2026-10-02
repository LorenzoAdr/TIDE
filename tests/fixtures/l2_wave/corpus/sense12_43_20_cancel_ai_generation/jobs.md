### Trabajo 1
consulta: dónde se captura la tecla Escape para cancelar la generación de la IA
keep: M2
leído: ai controller cpp, ai trace cpp, clear, handle ai console keys
Cerrado:
leído: ai controller cpp, ai trace cpp, clear, handle ai console keys
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se limpia el archivo al cancelar la generación de la IA
keep: M8
leído: clear, cancel current, clear pending insert, cancel all, cancel level1
Cerrado:
leído: clear, cancel current, clear pending insert, cancel all, cancel level1
Abierto:
- handle user input
- handle route

### Trabajo 3
consulta: cómo se decide si limpiar el archivo pendiente al cancelar la generación
keep: M8
leído: cancel current, clear pending insert
Cerrado:
encontré el mecanismo de decisión: en cancel current, se limpia el archivo pendiente clear pending insert si y solo si pending insert es true Y busy es false; si busy es true, se ignora el pendiente y se llama a cancel all
Abierto:
- handle user input
- handle route


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: ai controller cpp, ai trace cpp, clear, handle ai console keys
    extra: clear pending insert, has pending insert

T2
    visto: clear, cancel current, clear pending insert, cancel all
    extra: handle user input, handle route

T3
    visto: cancel current, clear pending insert
    extra: handle user input, handle route

entre abiertas:
  T1=>T2  mismo objeto: clear
  T1=>T3  cancel current → cancel all → migrate legacy config
  T2=>T3  mismo objeto: cancel current
hacia el resto:
  T1 → has pending insert
  T1 → ai tab active
  T2 → handle user input
  T2 → handle route
  T3 → handle user input
  T3 → handle route
