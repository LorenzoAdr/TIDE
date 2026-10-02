### Trabajo 1
consulta: dónde se calcula la trayectoria del cursor en el editor
keep: M1 M2
leído: mouse to cursor, handle editor mouse
Cerrado:
encontré el cálculo de la trayectoria del cursor: está en mouse to cursor que convierte coordenadas de ratón Mouse a posición lógica Cursor Pos usando el viewport y el ancho de tabs; es invocada por handle editor mouse para actualizar el cursor.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=1  (visto+hops extra+circuito; sin código)

T1
    visto: mouse to cursor, handle editor mouse
    extra: byte index to visual column, tab display width
hacia el resto:
  T1 → byte index to visual column
  T1 → tab display width
