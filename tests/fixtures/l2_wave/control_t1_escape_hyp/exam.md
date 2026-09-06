# examen
Ancla:
cuando la IA está generando código y el usuario pulsa Escape o hace clic fuera, quiero que se cancele la generación limpiamente sin dejar el archivo a medias

T1 preguntó:
dónde se maneja la tecla Escape para cancelar la IA
Cerrado de T1:
La tecla Escape NO se maneja en el controlador de IA Ai Controller . El sistema detecta cancelación mediante comandos de texto cancel, cancel, cancelar en is cancel input, no por eventos de teclado. La captura de la tecla Escape ocurre en la capa de UI handle ai console keys, que probablemente traduce el evento de tecla en el envío de la cadena cancel al controlador.
