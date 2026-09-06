### Trabajo 1
consulta: dónde se maneja la tecla Escape para cancelar la IA
keep: M2
leído: ai controller hpp, cancel current, handle user input, is cancel input
Cerrado:
La tecla Escape NO se maneja en el controlador de IA Ai Controller . El sistema detecta cancelación mediante comandos de texto cancel, cancel, cancelar en is cancel input, no por eventos de teclado. La captura de la tecla Escape ocurre en la capa de UI handle ai console keys, que probablemente traduce el evento de tecla en el envío de la cadena cancel al controlador.
Abierto:
- handle route
- handle ai console keys
