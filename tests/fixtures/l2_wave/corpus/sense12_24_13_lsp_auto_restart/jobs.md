### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M5
leído: send request, wait response, write request
Cerrado:
leído: send request, wait response, write request
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje LSP tras detectar que se ha caído
keep: M5
leído: restart lsp for workspace, send request, wait response
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré el mecanismo de detección de caída del servidor LSP; lo leído es el efecto del reinicio restart lsp for workspace y la capa de transporte wait response que retorna false en timeout error, pero no hay código que observe ese fallo y dispare el reinicio
Abierto:
- on child exit
- poll loop


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: send request, wait response, write request
    extra: TUIDE MON, request scope

T2
    visto: restart lsp for workspace, send request, wait response
    extra: on lsp missing install, on workspace opened
    entre interno: restart lsp for workspace → send request

entre abiertas:
  T1=>T2  mismo objeto: send request
hacia el resto:
  T1 → TUIDE MON
  T1 → request scope
  T2 → on lsp missing install
  T2 → on workspace opened
