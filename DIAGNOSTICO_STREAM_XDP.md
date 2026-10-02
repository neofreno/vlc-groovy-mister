# Diagnostico del stream y del receptor XDP

Fecha: 2026-09-16. Revision sin nueva reproduccion, a peticion del usuario.

Actualizacion 2026-09-17: el diagnostico siguiente es historico. Sus defectos de fuente se han abordado en una nueva DLL/API y receptor XDP ARM. El checklist de implementacion/pruebas esta al final de [GUIA_MEJORAS_STREAM_VIDEO.md](GUIA_MEJORAS_STREAM_VIDEO.md). No se afirma que el cuelgue fisico haya quedado resuelto hasta que el usuario lo pruebe. El binario local ya NO coincide con el SHA-256 remoto antiguo indicado abajo.

## Alcance y limites

Sintomas comunicados: MiSTer se cuelga poco despues de iniciar el stream, video a saltos y mal comportamiento en resoluciones bajas. No se ha reproducido el cuelgue durante este diagnostico. No se ha ejecutado VLC local, enviado video de prueba, reiniciado MiSTer ni modificado su configuracion/binarios.

Separo defectos comprobables en los fuentes de su posible participacion en el incidente. No hay log del fallo: el usuario reinicio la maquina tras el cuelgue. Los tests anteriores de conversion/cola no validaban el receptor XDP ni la cadencia completa VLC/API/FPGA.

## Estado obtenido antes de suspender las pruebas remotas

- Groovy seleccionado en MiSTer.ini: `main=MiSTer_groovy_XDP`.
- Acceso de administracion por Wi-Fi: 192.168.1.210. Ethernet de stream: 192.168.2.11, enlace a 1 Gbps Full Duplex. Son interfaces distintas; no significa que VLC este enviando por Wi-Fi.
- Tras reiniciar: core MENU, /tmp/groovy.log ausente, /tmp al 1%, memoria disponible aproximadamente 452 MiB. No permite descartar agotamiento de memoria en la sesion anterior.
- Binario XDP remoto fechado 2025-01-03; SHA-256 `a489cee2f95722fecb8395626fb5c81296aecc035b5bc1c44a4349e439662e71`.
- Su SHA-256 coincide exactamente con `../Groovy_MiSTer/hps_linux/MiSTer_groovy_XDP` local. No se ha demostrado correspondencia reproducible entre todos los fuentes actuales modificados y ese binario.
- El receptor seleccionado no es MiSTer_groovy_wifi. Las pruebas/correcciones previas del protocolo v2 no prueban el camino XDP legacy.

## 1. Bloqueo posible del bucle principal del receptor (critico)

Fuente: `../Groovy_MiSTer/hps_linux/src/support/groovy/groovy.cpp`, setBlit/setBlitAudio y groovy_poll; aproximadamente lineas 1262, 1307 y 2657.

Al iniciar una transferencia se activa `isCorePriority`. `groovy_poll()` repite mientras siga activo. En la ruta XDP no hay expiracion de la transferencia legacy. Si faltan datos y dejan de llegar paquetes, handle_receive_packets retorna sin cambiar ese estado y el bucle no vuelve al llamador. Es una explicacion concreta de que la aplicacion MiSTer deje de responder; no demuestra un kernel panic ni que esta fuera la secuencia del incidente.

Ejemplo aritmetico con el formato real: un frame RGB888 de 320x240 ocupa 230400 bytes. Tras 156 datagramas de 1472 bytes faltan 768 bytes. Si se pierde ese ultimo fragmento y no llega otro paquete, no existe timeout XDP que abandone la espera.

Tambien hay un bucle de reserva de FILL sin limite en handle_receive_packets (aprox. 2469), y se configura XDP_USE_NEED_WAKEUP con las llamadas de despertar de RX comentadas. Son riesgos adicionales de falta de progreso, sujetos al estado del driver/anillos.

## 2. Reutilizacion prematura de memoria de envio XDP (critico)

Fuente: sendACK y otros envios seleccionan `umem_frame_addr[outstanding_tx]` (aprox. 1134); complete_tx (878) solo decrementa un contador, sin recuperar la direccion completada.

Simulacion de esas transiciones, sin hardware:

1. Envio A usa direccion 0; pendientes=1.
2. Envio B usa direccion 4096; pendientes=2.
3. Solo A completa; pendientes=1 y B sigue usando 4096.
4. El envio C elige otra vez direccion 4096 y sobrescribe memoria todavia en uso.

Esto confirma un defecto del algoritmo con completaciones parciales, no un fallo observado de la NIC. Debe asignarse una direccion libre por envio y recuperarse exactamente la direccion notificada en COMPLETION. La propiedad de buffers esta documentada por [Linux AF_XDP](https://docs.kernel.org/networking/af_xdp.html#umem-completion-ring).

## 3. Datos incompletos publicados a la FPGA (critico si hay perdida)

Fuente: process_packet (aprox. 2084) fuerza `groovy_FPGA_blit_lz4(tamano_completo, 65535)` al detectar un fragmento corto incompatible con lo pendiente. Esto anuncia datos comprimidos que pueden no haberse recibido. El transporte legacy tampoco identifica individualmente cada fragmento con su frame.

Ademas, process_packet_eth copia a DDR antes de validar el paquete con process_packet (aprox. 2432), sin una comprobacion explicita de longitud UDP contra descriptor recibido y capacidad restante del destino.

Estos son mecanismos de corrupcion de imagen/estado a corregir. No se ha demostrado que se estuviera usando compresion ni que hubiese perdida de paquetes durante el incidente del usuario.

## 4. Causas comprobables de mala cadencia en el plugin

- `src/module.cpp:401`: el selector automatico usa dimensiones/aspecto, no FPS. En empates entre tamaños PAL/NTSC gana el primer preset, NTSC. Evaluacion local de la formula y los presets reales: 320x240 selecciona 60.030 Hz; 640x480 y 720x480 seleccionan aproximadamente 59.942/59.939 campos/s. El resultado es el mismo para contenido de 25 o 50 fps, lo que obliga a una cadencia irregular si el automatico elige esos modos.
- `src/capture.cpp:206`: se extrae el siguiente frame por FIFO, no por una fecha de presentacion objetivo. Se repite el ultimo cuando no hay pendientes. El PTS se conserva, pero no decide el instante/seleccion del video; solo se entrega al audio.
- `src/module.cpp:201`: la division entera del frame rate pierde 23.976/29.97/59.94; afecta al parametro usado por el audio. No implica por si sola que el emisor se limite a esos enteros: el ritmo de envio depende de WaitSync y la modeline.
- `src/groovymister_wrapper.cpp:31`: nivel verbose 2 forzado, con trazas por ACK/envio. Es trabajo adicional en el camino sensible al tiempo; no se ha medido su impacto en el PC de reproduccion.

## 5. Calidad visual a baja resolucion

`src/video_frame.h:107`: reduccion por vecino cercano, sin filtro de area/antialiasing. Una entrada HD reducida a 240p descarta muchas muestras y puede producir aliasing, parpadeo en detalles y movimiento inestable. Esto es independiente del cuelgue y no explica una parada del sistema.

## Orden de correccion propuesto

- [ ] Receptor XDP: timeout de transferencias, salida garantizada del bucle prioritario y despertares/esperas de anillos acotados.
- [ ] Receptor XDP: propiedad exacta de buffers TX y completaciones; probar completaciones parciales.
- [ ] Receptor legacy: validar antes de copiar y no publicar LZ4 incompleto. Probar perdida del ultimo fragmento, duplicados, longitudes invalidas y reanudacion.
- [ ] Plugin/API: revisar sincronizacion con FPGA, seleccion de frames por PTS, limite de repeticiones y contrapresion; conservar frame rates racionales.
- [ ] Selector automatico: considerar frecuencia de cuadros/campos y PAL/NTSC.
- [ ] Escalado para video: filtro de area/bilineal configurable; mantener vecino cercano como opcion.
- [ ] Validar primero con simulacion/loopback; despues hacer prueba fisica cuando el usuario pueda. No instalar otro receptor ni cambiar MiSTer.ini sin acordarlo.

La revision inicial del 16 no modifico produccion. La tanda del 17 si genera nuevos binarios; ver guia y paquete de pruebas. Ademas se corrigieron headroom RX, checksum impar y esperas FPGA sin limite. Referencia de propiedad de buffers y offsets: [AF_XDP](https://docs.kernel.org/networking/af_xdp.html).
