# Diagnóstico VLC / GroovyMiSTer — 2026-09-13

## Resultado

Hay problemas independientes en la integración binaria y en el transporte. La conexión inicial del log funciona: `Reliable mode acknowledged by server` y ACK en 32,7234 ms. Por tanto, esa sesión sí alcanzó un HPS que aceptaba el protocolo nuevo. No es un simple fallo de IP o de apertura del socket.

No se ha reproducido la pantalla negra con VLC y la FPGA durante este análisis. Sí se han reproducido dos defectos de la API en pruebas locales con la librería compilada, sin depender del Wi-Fi. No se han cambiado fuentes, DLL instaladas ni configuración de la MiSTer.

## 1. Cabecera antigua + librería nueva: escritura fuera del objeto

`vlc-groovy-mister-out/src/groovymister_wrapper.cpp:26` hace `new GroovyMister`. Release x64 incluye `groovymister/x64/groovymister.h` (vcxproj:173), que no contiene los tres nuevos campos RIO ni `m_reliableMode`.

Prueba con MSVC x64 y el constructor real de `Groovy_MiSTer/api/libgroovymister.lib`, usando placement-new sobre memoria sobredimensionada y bytes centinela:

- Cabecera del plugin: sizeof(GroovyMister) = 600. El constructor modifica 19 bytes fuera de esos 600, hasta el offset 619.
- Cabecera actual de la API: sizeof(GroovyMister) = 624. Cero bytes modificados fuera del objeto.
- Como control, la librería antigua que ahora está en `groovymister/x64` no sobrescribe el centinela con su cabecera antigua.

La combinación cabecera antigua/librería nueva corrompe memoria sin necesidad de conectar a la MiSTer. Windows registra un APPCRASH de vlc.exe el 13/09 a las 01:22:36 con excepción 0xc0000374. Es coherente con esta incompatibilidad; no se dispone de la pila del fallo para atribuir ese incidente histórico de manera exclusiva a ella.

Corrección: distribuir cabecera y librería juntas, desde la misma compilación, y recompilar completamente el wrapper/plugin. Mejor aún, encapsular la creación/destrucción del objeto dentro de la librería mediante una API C opaca, para que el plugin no dependa del tamaño de una clase C++ privada.

## 2. La API nunca vacía la cola de completaciones de envío RIO

`api/groovymister.cpp:1050,1082,1086` usa RIOSend con RIO_MSG_DONT_NOTIFY. Solo hay llamadas a RIODequeueCompletion para `m_receiveQueue` (líneas 766 y 805); ninguna para `m_sendQueue`.

RIO_MSG_DONT_NOTIFY evita la notificación, pero las completaciones siguen entrando en la cola. Además, se reutilizan los buffers de comandos y el scratch compartido de audio/vídeo sin esperar a la finalización del envío, y se ignora el retorno de RIOSend.

Prueba real con la nueva .lib, un receptor UDP en 127.0.0.1:43210 y 1.000 comandos separados por Sleep(1):

- Sin drenar: los 1.000 envíos retornan éxito y el receptor recibe los 1.001 datagramas contando INIT; al consultar la cola de envío, devuelve 4294967295 (RIO_CORRUPT_CQ).
- Drenando la cola después de cada comando: misma entrega de datagramas, y la consulta final devuelve 0, sin corrupción de la cola.

Esto confirma el defecto de gestión de la cola. No demuestra que los ACK históricos se cortaran exactamente por él: en esta prueba los datagramas se entregaron pese a que la cola terminó corrupta.

Corrección: contabilizar operaciones pendientes, consumir y comprobar las completaciones de envío, respetar la vida de sus buffers y comprobar los errores de RIOSend. También corregir `idx <= numResults` en getACK (línea 804), que procesa una entrada adicional no válida.

Referencia oficial: https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_riosend

## 3. El receptor HPS nuevo no tolera correctamente pérdidas/reordenación

Revisión de `hps_linux/src/support/groovy/groovy.cpp:2554-2590`:

- Coloca cada paquete por índice, pero después incrementa PoC_bytes_recv por el tamaño recibido (setBlitRaw/setBlitLZ4). No distingue bytes consecutivos disponibles de bytes recibidos fuera de orden ni descuenta duplicados.
- Ejemplo: llega el chunk 1 antes del 0. Se escribe en offset 1470, pero se anuncia a la FPGA que los primeros 1470 bytes están disponibles. El descompresor LZ4 puede leer bytes viejos o incompletos.
- Los chunks solo llevan índice, sin identificador de frame/transferencia. Un paquete retrasado de un frame puede colocarse en el siguiente, y faltan límites relativos al tamaño de la transferencia; el límite actual es el de todo el espacio DDR.
- Mientras isBlitting está activo, los comandos siguientes entran en el camino de chunks. Se ha omitido la recuperación de comandos cortos que tenía process_packet.
- El timeout solo se comprueba si recvfrom devuelve <= 0. Con tráfico continuo puede no activarse. El inicio de una transferencia de audio tampoco reinicia blitStart.
- groovy_force_flush_blit (línea 2175) entrega a la FPGA el tamaño completo de LZ4 aunque falten datos. No recupera correctamente un flujo comprimido.

Estos defectos son deducidos del código, no de un log HPS del fallo. Explican mecanismos concretos capaces de romper la imagen bajo pérdida/reordenación. La corrección necesita identificar las transferencias, distinguir comandos y datos sin ambigüedad, registrar chunks únicos y solo publicar un prefijo contiguo válido (o el frame completo), con descarte/recuperación de frames incompletos.

## 4. El fallback al protocolo antiguo cambia el tamaño de los paquetes

`api/groovymister.cpp:216` resta dos bytes a m_mtu antes de negociar y no los restaura al volver al protocolo antiguo. Con MTU 1500 acaba enviando payloads legacy de 1470 bytes, mientras el HPS legacy usa 1472 como criterio de fragmento completo en process_packet (línea 1955).

Un paquete de 1470 bytes puede disparar la detección de pérdida y el cierre prematuro del frame. La compatibilidad prometida en el comentario de CmdInit no se mantiene. Hay que negociar antes de construir los slices o reconstruirlos con el MTU correcto al cambiar de protocolo.

## 5. Los artefactos actuales no corresponden a los logs aportados

Estado inspeccionado:

- API nueva: api/libgroovymister.lib, 321900 bytes, 13/09/2026 00:48:52; contiene mensajes Reliable.
- .lib Release x64 del plugin: 990244 bytes, 29/03/2025; sin mensajes Reliable.
- DLL instalada en VLC: video_output/libgroovy_mister64_plugin.dll, 105472 bytes, 13/09/2026 01:24:13; sin mensajes Reliable.
- Log vlc-groovy-run.log: última modificación 13/09/2026 01:06:12. stdout sí anuncia Reliable.

La DLL instalada ahora es posterior a esos logs y contiene otra implementación. No se puede asumir que repetir Play reproduzca exactamente aquella sesión.

MiSTer accesible por SSH en 192.168.10.211:2222:

- Proceso activo /media/fat/MiSTer; /tmp/CORENAME = MENU; sin listener UDP 32100.
- /media/fat/MiSTer_groovy_wifi existe, 973100 bytes, y contiene el código Reliable.
- /media/fat/MiSTer.ini:343-345 selecciona [Groovy] main=MiSTer_groovy, no MiSTer_groovy_wifi.
- El código crea /tmp/groovy.log en modo wt (línea 398). Ese fichero no existe en el arranque actual, que llevaba unos 12 minutos al inspeccionarlo. Activar verbose no conserva el log después de reiniciar.

## Orden propuesto para corregir y validar

1. Unificar .h/.lib y reconstruir el plugin para eliminar la corrupción de memoria reproducida.
2. Corregir RIO y el fallback de MTU.
3. Corregir el receptor/protocolo Wi-Fi antes de dar por fiable la transmisión de LZ4.
4. Seleccionar explícitamente el HPS correspondiente y capturar simultáneamente logs nuevos de VLC y /tmp/groovy.log, conservándolos antes de reiniciar.
5. Verificar reproducción, parada/cierre y pérdida/reordenación controlada, separando protocolo antiguo y nuevo.

Pruebas aisladas y scripts: `%TEMP%\groovy-diagnosis-20260913` (abi_probe.cpp, probe_newlib.cmd, rio_probe.cpp, rio_probe.cmd, rio_probe_drain.cmd). Solo se creó documentación en el repositorio; las pruebas y sus binarios están en TEMP.
