### Trabajo 1
consulta: dónde se limpia el archivo o se revierte la escritura cuando se cancela la generación de código
keep: M8
leído: clear pending insert, cancel current, cancel all, cancel level1, begin insert at, tasks, agent cancel
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se limpia el archivo o se revierte la escritura cuando se cancela la generación de código
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
no encontré el mecanismo que limpia el archivo o revierte la escritura en disco al cancelar; lo leído muestra que la cancelación solo limpia estado en memoria pending insert, agent cancel y solicita cancelación a tareas agentes, sin lógica de revertir el buffer del editor ni restaurar el contenido del archivo.
Abierto:
- handle route
- handle user input
- tasks cancel
- agent cancel effect
- editor buffer revert

### Trabajo 2
consulta: dónde se detiene la escritura en el buffer del editor cuando se pulsa Escape durante la generación
keep: M8
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se detiene la escritura en el buffer del editor cuando se pulsa Escape durante la generación
Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla era más, otra pregunta distinta.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: clear pending insert, cancel current, cancel all, cancel level1
    extra: handle user input, handle route

T2
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → handle user input
  T1 → handle route
