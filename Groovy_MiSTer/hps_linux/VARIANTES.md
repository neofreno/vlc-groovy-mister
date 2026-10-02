# Receptores coherentes - 2026-09-29

Los tres ejecutables se construyen desde las fuentes unificadas en
`hps_linux/src`, incluida la base Main_MiSTer modificada. No son tres copias
independientes del protocolo ni necesitan un repositorio hermano.
Identificador de esta tanda: `coherent-20260929` en la salida de arranque.

| Ejecutable | Transporte y requisitos | Ajustes que se conservan |
|---|---|---|
| `MiSTer_groovy` | UDP normal, sin AF_XDP; puede recibir por Ethernet o Wi-Fi | Buffer UDP solicitado de 2 MiB, IP_TOS=1; configura MTU/afinidad de Ethernet solo si hay Ethernet disponible |
| `MiSTer_groovy_XDP` | AF_XDP exclusivo de eth0; requiere Ethernet | UMEM 32 MiB, lotes RX de 64, propiedad RX/TX y liberacion XDP; bibliotecas libxdp/libbpf |
| `MiSTer_groovy_wifi` | UDP con perfil Wi-Fi; socket INADDR_ANY, admite tambien Ethernet | Buffer solicitado de 8 MiB, DSCP EF (0xB8), busy-poll opcional de 20 us y presupuesto 256; no cambia MTU ni afinidad de eth0 |

Los buffers solicitados y busy-poll dependen del kernel; no garantizan caudal ni
latencia. XDP NO funciona sobre Wi-Fi. Mantener MTU 1500 para la prueba inicial.
El perfil Wi-Fi no cambia el MTU de la interfaz inalambrica ni activa Jumbo Frames.

## Evolutivos compartidos

- Legacy y v2 negociado: identificadores de sesion/transferencia, validacion,
  reensamblado, deduplicacion, paridad y descarte de transferencias incompletas.
- Staging completo antes de entregar pixels/comprimidos a FPGA; timeout de 80 ms.
- Validacion de INIT/modeline/media, limites de buffers y guard de sesion/modo.
- Timeouts de handshakes FPGA, sin confirmar controles fallidos.
- Limpieza de sesion al parar/arrancar y hooks antes de cargar core, reboot y exec.
- Cierre idempotente, sockets sin herencia por exec, limpieza de fallos parciales.
- Logs acotados/rotados y limites de trabajo: hasta cuatro lotes de 64 paquetes
  por llamada de groovy_poll. UDP ya no queda limitado a cuatro datagramas.
- No cambia RBF/RTL. Las mejoras de conversion, aspecto, presets, pausa/audio y
  reconexion del emisor permanecen en la DLL VLC existente, que no se recompila
  ni modifica en esta tanda.

## Compilacion unica, sin despliegue

Requiere bash, rsync, Python 3, make y ARM GNU 10.2-2020.11 en PATH.
Las fuentes locales incluyen los hooks de groovy_stop en fpga_io.cpp y las
cabeceras AF_XDP en support/groovy/kernel/usr/include. El script valida los
hooks antes de compilar. Desde la carpeta Groovy_MiSTer:

```sh
bash hps_linux/build-all.sh /var/tmp/groovy-receivers-build
```

Alternativas: build-standard.sh, build-xdp.sh y build-wifi.sh reciben los mismos
argumentos (solo la carpeta aislada). Todos delegan en build-receiver.sh.
`src/compila.sh` es tambien un alias de build-all.sh.

El directorio elegido contiene standard/, xdp/ y wifi/, cada uno con fuentes
refrescadas y objetos propios. No se importan objetos/dependencias de la base.
`GROOVY_BUILD_JOBS` controla el paralelismo (4 por defecto); all compila en serie.
No usar simultaneamente dos compilaciones de la MISMA variante en el mismo root.
Para una reconstruccion limpia, elegir un nuevo directorio aislado.

Los resultados se copian a hps_linux/ con sus nombres historicos exactos. No hay
FTP, SSH ni instalacion en estos scripts. El viejo src/build.sh, que contenia
pasos de despliegue, no se ha importado al repositorio unificado.

## Validacion e instalacion

Actualizacion posterior (2026-09-29): el usuario confirma que los tres receptores
de esta tanda funcionan correctamente. Se conservan estos binarios; las listas
ampliadas de prueba no se dan por medidas individualmente sin resultados.

Antes de entregar esta tanda solo estaba validado fisicamente el XDP de cleanup.
La confirmacion posterior anterior cubre el funcionamiento general de los tres;
las pruebas unitarias/loopback por si mismas no ejecutan HPS/FPGA.

Detener el stream, respaldar los ejecutables y copiar los tres archivos a sus
ubicaciones habituales de la SD. Conservar la DLL y RBF actuales. MiSTer.ini
selecciona UNO mediante main=; no cambiarlo automaticamente al copiar. Realizar
un apagado/encendido inicial al sustituir el ejecutable usado y comprobar:

- Inicio normal y receptor iniciado despues del video.
- Pausa/reanudacion, saltos, aspecto y resolucion con modos seguros para el CRT.
- Apagado/encendido y cold reset mientras VLC reproduce; volver al core.
- Cierre/reapertura, acceso al OSD y audio/video sostenidos, con raw y LZ4.
- Wi-Fi: probar sin Ethernet conectado; no asumir el caudal de cable.

Si falla, guardar /tmp/groovy.log, /tmp/groovy.log.1 y log VLC antes de reiniciar.
Para rollback, detener VLC, restaurar el ejecutable elegido y apagar/encender.
