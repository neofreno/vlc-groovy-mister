# Correcciones de API, HPS y VLC

Actualizacion 2026-09-29: los tres receptores se han unificado en fuente y flujo
de build, conservando sus perfiles. Ver [variantes actuales](hps_linux/VARIANTES.md).
El usuario ha validado cold reset con la ultima correccion XDP anterior a esta
reconstruccion conjunta. Las referencias a instalaciones y pruebas mas abajo
son HISTORICAS, no describen un despliegue efectuado en esta nueva tanda.

La API conserva el protocolo original para `MiSTer_groovy` y
`MiSTer_groovy_XDP`. Solo activa Wi-Fi v2 cuando recibe una confirmación explícita
de la versión y sesión. El fallback mantiene los paquetes legacy de 1472 bytes
con MTU 1500. XDP sigue requiriendo Ethernet.

## Cambios realizados

- Envío UDP síncrono con tiempo límite: se eliminan las colas RIO sin consumir y
  la reutilización de buffers todavía pendientes. Inicialización, cierre repetido
  y limpieza tras errores quedan controlados.
- La solución de VLC referencia el proyecto de la API y usa sus cabeceras.
  La creación/destrucción del objeto ocurre dentro de la librería para evitar
  reservas con un tamaño de clase antiguo.
- Protocolo Wi-Fi v2: sesión, transferencia, metadatos repetidos, deduplicación,
  separación de audio/vídeo, timeout y descarte seguro de frames incompletos.
  La paridad recupera un paquete perdido por cada grupo de ocho. No se usan deltas
  que dependan de frames perdidos. Modeline y cierre tienen ACK y reintentos.
- El plugin protege el cierre del audio frente al hilo de vídeo, limita los
  bloques de audio a la capacidad del HPS y corrige la gestión de la cola de vídeo
  y el empaquetado de filas RGB.
- El HPS limita las trazas de nivel 3 a 100 muestras/s y rota el log aproximadamente
  a 8 MiB, conservando una copia anterior. En las pruebas, las trazas anteriores
  habían llenado `/tmp` con 241 MB; después de corregirlo la prueba de 180 frames
  generó unos 85 KiB y `/tmp` volvió al 3 % de uso.

## Validación

- MSVC x64: API y plugin compilados; API también compilada con GCC Linux.
- Reconstrucción: 300 casos de pérdida, reordenación, duplicados y recuperación
  con paridad; además, expiración, frames antiguos, límites y cambios de modeline.
  Pasan en Windows y en Linux con AddressSanitizer/UndefinedBehaviorSanitizer.
- Transporte real UDP en loopback: ocho combinaciones (legacy/v2, RAW/LZ4,
  MTU 1500/9000), verificando píxeles y audio byte a byte. Se pierden
  deliberadamente las primeras confirmaciones v2 para comprobar reintentos.
  Un ACK genérico no activa v2.
- 100 ciclos de creación, inicialización fallida y cierre repetido, sin incremento
  de handles tras el calentamiento de Winsock (64 → 64).
- MiSTer por Wi-Fi: API nueva contra `MiSTer_groovy` legacy, hasta el frame 120,
  con sincronización y audio habilitado. Contra el HPS v2 corregido, hasta el
  frame 180, también sincronizado y con audio habilitado.
- VLC 3.0.21 con la DLL corregida: dos reproducciones completas del vídeo de
  30 segundos, con cierre normal del filtro de audio, del hilo de vídeo y de VLC.
  No se registró un nuevo APPCRASH de VLC durante esas pruebas.
- **XDP no se ha validado en reproducción real**: la conexión de trabajo es Wi-Fi.
  Se ha conservado y probado el formato de paquetes legacy que utiliza.

Hay frames descartados en el vídeo de mayor caudal sobre Wi-Fi. Se descartan
sin entregar LZ4 incompleto a la FPGA; la corrección no garantiza ausencia de
cortes si el enlace no sostiene el bitrate. Las pruebas confirman protocolo,
sincronización y cierre; la imagen y el sonido físicos requieren la observación
del usuario.

## Archivos y estado de instalación

- API: `api/libgroovymister.lib` y `api/groovymister.h`.
- HPS: `hps_linux/MiSTer_groovy_wifi`, ya copiado a `/media/fat/MiSTer_groovy_wifi`.
  `[Groovy] main=MiSTer_groovy_wifi` está seleccionado y el core Groovy está cargado.
- DLL: `../vlc-groovy-mister-out/x64/Release/libgroovy_mister64_plugin.dll`.
  **Su copia a Program Files la realiza el usuario**, porque Windows denegó la
  escritura sin elevación. Con VLC cerrado, sustituir la DLL de
  `C:\Program Files\VideoLAN\VLC\plugins\video_output`.
- El VLC empleado para las pruebas está en `%TEMP%\groovy-vlc-fixed`.
- Logs de las pruebas: `%TEMP%\vlc-groovy-fixed*.log` y
  `%TEMP%\vlc-groovy-fixed-repeat*.log`. El log original grande del HPS se conservó
  comprimido en `/media/fat/groovy-wifi2-tests-before-log-limit.log.gz`.
- Copias de seguridad en MiSTer: `MiSTer.ini.before-wifi2-fix` y
  `MiSTer_groovy_wifi.before-wifi2-fix`. La DLL anterior está en
  `%TEMP%\groovy-fix-backups`.

El formato de red y las instrucciones de compilación están en
[protocol/README.md](protocol/README.md). El informe `DIAGNOSTICO_VLC_WIFI.md`
describe el estado anterior a estas correcciones.
