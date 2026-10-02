# Guia de configuracion del modulo Groovy MiSTer

Esta guia resume como configurar el plugin en VLC para enviar video/audio a MiSTer.

## Donde colocar la DLL

En Windows, copia la DLL compilada del plugin dentro de la carpeta `plugins` de tu instalacion de VLC.

Ruta recomendada:

- `C:\Program Files\VideoLAN\VLC\plugins\video_output\libgroovy_mister64_plugin.dll` (VLC x64)

Alternativa (si tu VLC no carga desde subcarpetas de categoria):

- `C:\Program Files\VideoLAN\VLC\plugins\libgroovy_mister64_plugin.dll`

Notas importantes:

- Usa DLL x64 con VLC x64.
- Usa DLL x86 con VLC x86.
- No dejes dos copias activas del plugin a la vez (por ejemplo en `video_filter` y `video_output`), porque VLC puede cargar ambos modulos y mezclar configuraciones.
- Si reemplazas una DLL existente, cierra VLC antes de copiar.
- Si VLC no detecta el plugin, elimina `plugins.dat` para forzar reescaneo y vuelve a abrir VLC.

## Como activar el plugin en VLC

Sin activar el modulo, las opciones `mister-groovy-*` no se aplican.

1. Abre VLC y ve a Herramientas -> Preferencias.
2. Abajo a la izquierda, selecciona "Mostrar ajustes: Todo".
3. Selecciona la salida de video Groovy Mister en:
  - Video -> Salida
  - Modulo de salida de video: "Groovy Mister" (o nombre equivalente).
4. Guarda cambios y reinicia VLC.

Alternativa por linea de comandos:

```bash
--vout=vlc_groovy_mister
```

Nota: en modo video_out puro no necesitas activar filtro de video ni interfaz de control.

## Donde se configura

Las opciones del plugin usan el prefijo:

- `mister-groovy-`

Puedes configurarlas de dos formas:

1. Desde la UI de VLC (Preferencias del modulo).
2. Por linea de comandos pasando `--<opcion>=<valor>`.

Ejemplos CLI:

```bash
--mister-groovy-host=192.168.2.11
--mister-groovy-streamlogcadence=300
```

## Parametros disponibles

### General

- `mister-groovy-host`:
  - IP o host del MiSTer.
  - Valor recomendado: IP fija de tu MiSTer en LAN.
- `mister-groovy-compress`:
  - `0` desactivado, `1` activado.
  - Define si envia paquetes comprimidos.
- `mister-groovy-streamlogcadence`:
  - Rango: `0..10000`.
  - Cada cuantos frames se imprimen estadisticas (`0` desactiva logs periodicos).
  - Recomendado: `300` para debug moderado.
- `mister-groovy-runtimepollcadence`:
  - Rango: `0..10000`.
  - Relee cambios de `modeline` y `aspectratio` cada N frames.
  - `0` desactiva cambios en caliente (solo aplica al abrir vout).
  - Recomendado:
    - `30` equilibrio entre reactividad y coste.
    - `120` si priorizas reducir overhead de sondeo.
    - `0` para comportamiento fijo sin runtime updates.

### Video y audio

- `mister-groovy-modeline`:
  - Selecciona modeline predefinida.
  - `0` manual, `1` automatico, resto: presets NTSC/PAL.
  - Recomendado: `1` (automatico), salvo ajuste fino.
- `mister-groovy-aspectratio`:
  - `0` off, `1` on.
  - Activado: conserva la proporcion visible del video (incluido SAR/anamorfismo) en una pantalla fisica 4:3, independientemente del raster enviado.
  - Un video 16:9 ocupa el 75% de la altura; el resto son bandas. Desactivado: estira a toda la pantalla.
  - Automatic respeta esta casilla. Cambiarla no cambia por si solo los timings de MiSTer.
- `mister-groovy-only15khz`:
  - `0` off, `1` on.
  - Limita autoseleccion a modos 15 kHz.
  - Recomendado: `1` en setups CRT 15 kHz.
- `mister-groovy-mixaudio`:
  - Rango: `0..100`.
  - Mezcla front/rear.
  - Recomendado inicial: `60`.

### Modeline manual (solo si `mister-groovy-modeline=0`)

- `mister-groovy-pClock`
- `mister-groovy-hActive`
- `mister-groovy-hBegin`
- `mister-groovy-hEnd`
- `mister-groovy-hTotal`
- `mister-groovy-vActive`
- `mister-groovy-vBegin`
- `mister-groovy-vEnd`
- `mister-groovy-vTotal`
- `mister-groovy-interlace`

Regla practica: usa manual solo si necesitas timings exactos de un display/cadena concreta.

## Perfiles recomendados

### Perfil estable (uso diario)

```bash
--mister-groovy-host=192.168.2.11
--mister-groovy-modeline=1
--mister-groovy-only15khz=1
--mister-groovy-aspectratio=1
--mister-groovy-mixaudio=60
--mister-groovy-streamlogcadence=0
--mister-groovy-runtimepollcadence=30
```

### Perfil diagnostico A/V

```bash
--mister-groovy-streamlogcadence=120
--mister-groovy-runtimepollcadence=30
```

## Como validar que quedo bien

Busca estos mensajes en log del plugin:

- apertura de audio con codec/rate/channels
- inicio de API con frecuencia/canales negociados, independiente del primer bloque de audio
- resumen de vout (`queued`): frames aceptados por la cola
- resumen `video sender summary`: `received`, `queued`, `rejected`, `dropped_full`, `dropped_mode`, `submitted`, `repeated`, `send_errors`, `queue_peak`

`submitted` cuenta llamadas a la API de envio, no confirmaciones de recepcion del FPGA.
En entrelazado cuenta campos enviados. `repeated` indica reutilizacion del mismo frame fuente
(incluye campos sucesivos del mismo cuadro), no necesariamente un fallo.

El modulo solicita I420/J420 a VLC y convierte esos planos al RGB888 de MiSTer.
Los formatos RGB/YUV empaquetados requieren el conversor correspondiente de VLC instalado.
Las superficies GPU opacas siguen rechazadas: para esta ruta utiliza decodificacion por software.

La cola reserva seis buffers al iniciar (aproximadamente 19 MiB en total) y los reutiliza.
Los cambios de modo invalidan los frames pendientes del modo anterior. Si la cola se llena,
se descarta el pendiente mas antiguo conservando el frame que se esta enviando.

Plan detallado y pruebas pendientes: [Guia de mejoras del stream](GUIA_MEJORAS_STREAM_VIDEO.md).

Desde la DLL video-only (2026-09-29), el video no espera al audio. Sin filtro se
negocia 48 kHz estereo, pero no se envian muestras. Audio tardio a la misma
frecuencia se incorpora sin nuevo INIT. Una frecuencia distinta requiere nueva
sesion y reaplicar el modo, con posible corte breve. Quitar audio no detiene video.
El resumen de arranque distingue `reconnects` de `audio_reconfigurations`.

El conversor actual acepta float32 nativo a 22050/44100/48000 Hz y lo envia como
PCM16 estereo. Otros formatos/frecuencias se rechazan explicitamente: no se
remuestrean ni se interpretan como float32. Revisar el aviso `Unsupported audio`
si hay imagen sin sonido; la revision general de conversion/downmix sigue pendiente.

## Referencias tecnicas

### Conversion exacta optimizada (2026-09-30)

No añade ajustes. Mantiene los pixels y filtros anteriores y muestra
`converter=exact-tiled-v1` al arrancar el emisor. Comparar los campos `conversion`
de los resumenes con los mismos clips/ajustes; guardar un log por archivo.
La mejora del benchmark local no garantiza el mismo porcentaje en el PC real.
No requiere cambiar audio, receptores, RBF ni decodificacion. Los intentos
DX11/DXA9 siguen rechazados; esta optimizacion trabaja sobre los planos I420/J420.

### Metricas del stream (2026-09-29)

La tanda posterior audio-direct conserva estas metricas y no requiere nuevas
opciones. El envio copia audio directamente del anillo al buffer API sin
temporal por lectura; no modifica formatos, reloj, pausa ni tamano de bloque.

Con detalle de log 2 aparecen `stream metrics periodic` y `stream timings
periodic` cada 30 segundos de funcionamiento del trabajador, y `final` al
cerrar. Son acumulados de toda la reproduccion, no promedios de cada ventana.
Las medias/maximos de tiempo se expresan en microsegundos. `queue` es ocupacion
actual/pico, no ocupacion/capacidad.

- `submitted`: solicitudes a la API; no implica recepcion.
- `ack_observed`: echo exacto observado y vinculado a un envio; los estados
  cacheados no cuentan de nuevo. No demuestra presentacion fisica en CRT.
- `ack_unobserved`: envios anteriores a un echo observado. La API pudo consumir
  sus ACKs sin que el plugin viera el estado intermedio; NO es perdida medida.
- `ack_overwritten`: correlaciones retiradas al llenarse los 256 registros.
- `ack_abandoned`: correlaciones pendientes al reiniciar sesion.
- `ack_pending`: registros todavia esperando coincidencia.
- `ack_progressive`/`ack_fields`: coincidencias de cuadros progresivos/campos.
- `ack_observation`: tiempo desde comienzo del envio hasta observar el echo;
  incluye sondeo y planificacion. No es RTT puro ni latencia de pantalla.

Para evaluar el siguiente ajuste, guardar 60-90 segundos del mismo clip con
modo/compresion/receptor fijos y anotar CPU del PC de reproduccion. Mantener los
receptores y RBF actuales. Esta DLL no cambia WaitSync ni promete menos CPU.

### Actualizacion de video, 2026-09-17

- `mister-groovy-smoothvideo` (booleano, activado por defecto): promedio de cajas al reducir ambas dimensiones; reduce aliasing al pasar de HD a 240p. Desactivarlo conserva vecino cercano. Ampliaciones o escalados mixtos siguen usando vecino cercano.
- Automatic considera FPS y refresco real; 25/50 priorizan PAL y 23.976/29.97/59.94 NTSC entre los presets disponibles. No altera Aspect Ratio.
- `15 kHz only (Automatic)` solo limita la seleccion automatica. Activado restringe a ~15 kHz; desactivado permite todos los presets, sin obligar a elegir ~31 kHz. No afecta a Manual ni a un preset fijo. Al guardar se detecta el cambio en el siguiente sondeo durante reproduccion (por defecto cada 30 frames; 0 desactiva el sondeo). En pausa se aplicara al reanudar la entrega de frames. Desactivar solo si la pantalla admite los modos adicionales.
- Los FPS se revisan tambien durante reproduccion: si no estaban disponibles al abrir, se recalcula cuando llegan. El log `Automatic: source=... fps=... only15khz=... -> preset=... h=... refresh=...` permite distinguir la frecuencia del origen, la horizontal de salida y el refresco vertical. A 24 fps la politica es ~60 Hz tanto con limite de 15 kHz como sin el.
- CRT fisico: 4:3. Aspect Ratio activado conserva la geometria del contenido, no asume pixeles cuadrados del raster enviado.
- `dropped_late` cuenta frames pendientes descartados cuando existe una imagen mas reciente cuya fecha de presentacion ya ha llegado.
- `dropped_expired` cuenta pendientes que superan 250 ms de retraso; no se caduca la imagen retenida durante pausa. `pts_regressions` cuenta retrocesos de fecha de presentacion y `dropped_discontinuity` los pendientes retirados por ello. Estos contadores no identifican necesariamente un seek de VLC.
- `progressive_submitted` y `fields_submitted` separan cuadros progresivos y campos entrelazados solicitados. No son ACK ni equivalen a FPS medidos en pantalla.
- El resumen `video timings us (average/max)` al cerrar muestra conversion, copia/envio API, espera WaitSync y edad hasta primera solicitud de cada imagen. Usar log VLC de detalle 2. `first_submit_age` excluye repeticiones del mismo frame y no mide la latencia fisica del CRT.
- MTU del plugin: 1500. Para la primera prueba dejar Jumbo Frames desactivado en Groovy. El loopback de API con MTU 9000 no valida jumbo en XDP (chunks de 4096 bytes).
- Usar la nueva DLL junto al receptor XDP incluido. La API intenta protocolo v2 y conserva fallback legacy; el texto historico `Wi-Fi v2` tambien puede aparecer sobre Ethernet, no indica que se este usando Wi-Fi.

Ver [instalacion y prueba fisica](PRUEBA_ETHERNET_20260917.md).

### Pausa/reanudacion del audio (DLL audio-resume, 2026-09-20)

No requiere nueva opcion. El filtro escucha los eventos de estado del input de
su playlist y conserva/reajusta las fechas de las muestras al reanudar. Buscar
con detalle 2 `Audio pause/resume observer attached to input` al abrir y
`Audio resumed: shifted buffered timestamps` al reanudar. Si aparece
`No owning input found`, esta correccion de pausa no se ha podido vincular al
input; guardar ese log. Los seeks/flush vacian la cola mediante pf_flush.
Al cerrar se registran pause_rebases, flushes y paused_blocks; este ultimo
cuenta bloques recibidos mientras el observador aun indica pausa.

Las opciones y rangos salen directamente del registro del modulo en:

- `src/module.cpp`

### Mando conectado a MiSTer (DLL joypad, 2026-09-30)

En el OSD de Groovy, configurar **Joysticks: Digital** o **Analog**, no Off.
Ambos transportan botones digitales; esta DLL no asigna acciones a ejes analogicos.
Las letras dependen del mapeo del mando en MiSTer; prevalecen B1/B2/B3/B4.

| Boton logico | Accion |
|---|---|
| B2 / A | Pausa o reanuda |
| B3 / Y | Para |
| B4 / X | Elemento anterior de la playlist |
| B1 / B | Elemento siguiente; tambien admite B10+B1 |
| Derecha / izquierda | Avanza / retrocede 1 segundo |
| Arriba / abajo | Capitulo siguiente / anterior; sin capitulos, saltos por secciones del 10% |

La pulsacion de transporte/capitulo no se repite al mantenerla. Izquierda/derecha
repiten tras 400 ms, cada 150 ms. Se admiten los mapas digitales de mandos 1 y 2.
El mando requiere un vout activo: puede reanudar una pausa, pero no se promete
arrancar de nuevo tras Parar y destruirse la salida. LibVLC embebido sin playlist
se deshabilita con aviso; no impide streaming.

Log detalle 2: `JoyPad controller=playlist-v1`, `input subscription requested`,
`JoyPad first packet`, `JoyPad action` y `JoyPad summary`. La suscripcion por
UDP 32101 no prueba recepcion: el primer paquete y samples del resumen si.
Las acciones se despachan fuera del hilo de video; el sondeo/API sigue en su
unico hilo propietario. No cambian el protocolo ni los binarios receptores.

### Protecciones de cierre y cambio de archivo (DLL lifecycle, 2026-09-30)

Sin nuevas opciones ni cambios de botones. Log actualizado a
`JoyPad controller=playlist-v2`. Las ordenes conservan el input de origen y
se descartan si ya no corresponde a la reproduccion o sesion actual. Al
comenzar el cierre se dejan de aceptar ordenes, incluso si falta terminar un
handshake con MiSTer. Los descartes se registran como JoyPad action ignored.

La interfaz auxiliar de presets mantiene su propio temporizador y estado por
instancia; cerrar una no deja callbacks sobre objetos liberados ni destruye
el timer de otra. No hay cambios en audio, conversion, sincronizacion o core.
