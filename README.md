# VLC → Groovy MiSTer

Plugin de salida de vídeo para VLC 3 en Windows x64 y x86, con audio y control desde
los mandos de MiSTer. Este repositorio reúne el emisor, la API de transporte,
los tres receptores ARM y las fuentes del core FPGA.

## Descargar e instalar

Las [releases](https://github.com/neofreno/vlc-groovy-mister/releases) incluyen
los tres receptores y plugins Windows para VLC x64 y x86. Consulta la
[guía de instalación y uso](INSTALACION_Y_USO.md) antes de copiarlos: la
arquitectura debe coincidir con VLC y XDP requiere un entorno MiSTer preparado.

## Configurar VLC

1. Cerrar VLC y copiar la DLL de su arquitectura a `plugins/video_output/`:
   `libgroovy_mister64_plugin.dll` para VLC x64 o `libgroovy_mister_plugin.dll`
   para VLC x86. Retirar las copias antiguas de otras carpetas de plugins.
2. Abrir **Herramientas → Preferencias → Mostrar ajustes: Todo**.
3. En **Vídeo → Módulos de salida**, seleccionar **Groovy Mister**. En las
   opciones de esa salida, configurar **Mister Host** con la IPv4 de MiSTer
   usada para el stream, no necesariamente la IP Wi-Fi de administración.
4. En **Audio → Filtros**, activar **Groovy Mister**. Seleccionar solo la salida
   de vídeo no envía audio: el filtro de audio se activa por separado, aunque
   ambos estén en la misma DLL. Mantener el audio de VLC habilitado.
5. En **Interfaz → Interfaces de control**, activar **Groovy Mister**. Esta
   interfaz auxiliar sincroniza y guarda los campos de timings cuando se elige
   un preset fijo, incluso sin reproducir vídeo. Mantener la interfaz habitual
   de VLC: Groovy se añade como interfaz adicional, no la sustituye.
6. En **Entrada/Códecs**, desactivar la **decodificación acelerada por hardware**.
   Esta ruta trabaja con imágenes en memoria; no admite superficies opacas
   D3D9/D3D11. No activar el antiguo filtro de vídeo Groovy.
7. Para empezar en un CRT 4:3 de 15 kHz: **Video Mode = Automatic**, **15 kHz
   only (Automatic) activado** y **Aspect Ratio activado**. Guardar y reiniciar VLC.
8. Cargar el core Groovy en MiSTer y abrir un vídeo en VLC. Empezar con Ethernet
   directa, MTU 1500 y Jumbo Frames desactivado.

**Importante para CRT:** el límite de 15 kHz solo afecta al modo automático
del plugin. No limita los presets fijos, el modo manual ni el modo de arranque
del RBF. Usar únicamente modos admitidos por la pantalla y conservar el RBF
validado. La corrección de aspecto está diseñada para una pantalla física 4:3.

Ejemplo desde PowerShell en el PC de reproducción, con VLC cerrado previamente:

```powershell
& 'C:\Program Files\VideoLAN\VLC\vlc.exe' `
  --vout=vlc_groovy_mister `
  --audio-filter=vlc_groovy_mister `
  --extraintf=vlc_groovy_mister `
  --avcodec-hw=none `
  --mister-groovy-host=192.168.2.11 `
  --mister-groovy-modeline=1 `
  --mister-groovy-only15khz `
  --mister-groovy-aspectratio `
  --mister-groovy-runtimepollcadence=30 `
  'D:\Videos\ejemplo.mkv'
```

Adaptar la IP y las rutas. Para VLC x86, la instalación suele estar en
`C:\Program Files (x86)\VideoLAN\VLC`. El filtro conserva también la salida de
audio local: si no se desea escuchar el PC, silenciar sus altavoces sin desactivar
la decodificación de audio en VLC. Para rutas de instalación, runtime Microsoft,
caché del plugin, red y solución de problemas, ver [la guía completa](INSTALACION_Y_USO.md).

La configuración completa activa Groovy en **salida de vídeo, filtro de audio
e interfaces de control**. Cada componente cumple una función distinta: la
interfaz auxiliar completa los campos de los presets; el vídeo y el mando
funcionan desde la salida activa y el audio requiere su filtro. La interfaz
auxiliar no calcula los campos de Automatic ni modifica los timings de Manual.
Si ya se usan otras interfaces adicionales, conservarlas al configurar
`extraintf`. No usar `--intf=vlc_groovy_mister`, que reemplazaría la interfaz principal.

## Parámetros del plugin

Todos usan el prefijo **`mister-groovy-`**. La tabla muestra los nombres completos
para línea de comandos y los valores registrados por el módulo, no un perfil
ya guardado por el usuario. En VLC se configuran desde las preferencias de Groovy.

| Parámetro | Valor inicial | Función |
|---|---|---|
| `mister-groovy-host` | Vacío | IPv4 de MiSTer para el stream; hay que configurarla |
| `mister-groovy-modeline` | `1` | Modo de vídeo: `0` Manual, `1` Automatic, `2..15` presets de la tabla siguiente |
| `mister-groovy-only15khz` | Activado | En Automatic, restringe a modos de unos 15 kHz. Desactivado permite todos los presets; no obliga a elegir 31 kHz |
| `mister-groovy-aspectratio` | Desactivado | Activarlo conserva la proporción del contenido, incluido SAR/anamorfismo, en una pantalla física 4:3. Desactivado estira a pantalla completa |
| `mister-groovy-smoothvideo` | Activado | Promedia píxeles al reducir ambas dimensiones para reducir aliasing. Desactivado usa vecino cercano; ampliaciones/escalados mixtos también usan vecino cercano |
| `mister-groovy-compress` | Desactivado | Activa compresión LZ4 del stream; puede reducir el caudal de red sin cambiar la resolución |
| `mister-groovy-mixaudio` | `60` | Mezcla frontal/trasera para audio multicanal, rango `0..100`; no es un control de volumen general |
| `mister-groovy-runtimepollcadence` | `30` | Revisa los cambios de configuración cada N frames, rango `0..10000`; `0` desactiva el sondeo durante la reproducción |
| `mister-groovy-streamlogcadence` | `300` | Cadencia de estadísticas de vídeo, en frames, rango `0..10000`; `0` desactiva esos mensajes periódicos, no todos los logs del módulo |

En CLI, los enteros y cadenas usan `--opcion=valor`. Para booleanos, por ejemplo,
`--mister-groovy-aspectratio` activa y `--no-mister-groovy-aspectratio` desactiva.
En la interfaz de VLC se usan las casillas correspondientes.

Al guardar cambios, el sondeo aplica modeline, aspecto, suavizado y límite de
15 kHz durante la reproducción; en pausa, cuando vuelven a llegar frames.
Para cambiar host o compresión, cerrar y reabrir la reproducción. Con cadencia
`0`, reabrir también para aplicar los demás cambios. El ajuste de mezcla de
audio se consulta desde el filtro de audio.

### Modos predefinidos

Las frecuencias de la tabla son nominales; el refresco real depende del timing.
`i` indica entrelazado y `p` progresivo. En modos entrelazados, 50/60 Hz es la
frecuencia de campos, no la de cuadros completos.

| Valor | Modo | Familia nominal |
|---|---|---|
| `0` | Manual | Timings introducidos por el usuario |
| `1` | Automatic | Selección según dimensiones, FPS y límite de 15 kHz |
| `2` | 256×240p | NTSC, 60 Hz |
| `3` | 320×240p | NTSC, 60 Hz |
| `4` | 320×480i | NTSC, 60 Hz |
| `5` | 640×480i | NTSC, 60 Hz |
| `6` | 720×480i | NTSC, 60 Hz |
| `7` | 640×480p | NTSC, 60 Hz; unos 31 kHz |
| `8` | 720×480p | NTSC, 60 Hz; unos 31 kHz |
| `9` | 256×240p | PAL, 50 Hz |
| `10` | 320×240p | PAL, 50 Hz |
| `11` | 320×480i | PAL, 50 Hz |
| `12` | 640×480i | PAL, 50 Hz |
| `13` | 720×576i | PAL, 50 Hz |
| `14` | 640×480p | PAL, 50 Hz; unos 31 kHz |
| `15` | 720×576p | PAL, 50 Hz; unos 31 kHz |

Automatic busca un modo adecuado al vídeo entre estos presets; no genera una
modeline arbitraria. A 24 fps prioriza aproximadamente 60 Hz tanto con el límite
de 15 kHz como sin él. El log `Automatic: source=... fps=... -> preset=...`
permite comprobar la elección. El aspecto del contenido se controla por separado.

### Timings manuales

Solo se utilizan con `mister-groovy-modeline=0`. Su valor inicial es cero y no
forma una modeline válida: hay que completar timings compatibles con la pantalla.

| Parámetro | Significado |
|---|---|
| `mister-groovy-pClock` | Reloj de píxel en MHz |
| `mister-groovy-hActive` | Píxeles visibles horizontales |
| `mister-groovy-hBegin`, `mister-groovy-hEnd` | Inicio y final del pulso de sincronía horizontal |
| `mister-groovy-hTotal` | Total de píxeles por línea, incluidos intervalos no visibles |
| `mister-groovy-vActive` | Líneas visibles |
| `mister-groovy-vBegin`, `mister-groovy-vEnd` | Inicio y final del pulso de sincronía vertical |
| `mister-groovy-vTotal` | Total de líneas, incluidos intervalos no visibles |
| `mister-groovy-interlace` | Entrelazado; desactivado por defecto |

No usar Manual como primera prueba ni copiar timings de una pantalla distinta.
El módulo valida el orden y los límites de los campos, pero eso no demuestra que
el monitor soporte la señal.

## Mando conectado a MiSTer

En el OSD del core Groovy, seleccionar **Joysticks: Digital** o **Analog**, no
Off. Ambos permiten los botones digitales; este plugin no asigna acciones a los
ejes analógicos. Se aceptan los mandos 1 y 2. Las letras dependen del mapeo en
MiSTer: la referencia son los botones lógicos B1/B2/B3/B4.

| Botón lógico | Acción en VLC |
|---|---|
| B2 / A | Pausar o reanudar |
| B3 / Y | Parar la reproducción |
| B4 / X | Elemento anterior de la playlist |
| B1 / B | Elemento siguiente de la playlist; también admite B10+B1 |
| Derecha | Avanzar 1 segundo |
| Izquierda | Retroceder 1 segundo |
| Arriba | Capítulo siguiente; sin capítulos, avanzar a la siguiente sección del 10% |
| Abajo | Capítulo anterior; sin capítulos, retroceder por secciones del 10% |

Izquierda/derecha repiten tras 400 ms, cada 150 ms; los demás controles no se
repiten al mantenerlos. Se ignoran combinaciones ambiguas. El mando requiere
una salida de vídeo activa: puede reanudar una pausa, pero después de **Parar**
puede ser necesario iniciar otra vez desde VLC. No se admite el control de una
instancia LibVLC embebida sin playlist. Si no llegan botones, revisar UDP 32101
y buscar `JoyPad first packet` en el log de VLC con detalle 2.

## Receptores mejorados y retrocompatibilidad

Los tres receptores incluidos **se han mejorado para aumentar la estabilidad
del streaming de vídeo**, manteniendo sus diferencias de transporte. No son
solo copias de los ejecutables originales con otro nombre:

- Validan tamaños, comandos y modelines y controlan los buffers antes de
  entregar datos a la FPGA; las transferencias incompletas se descartan en las
  rutas con reensamblado, evitando publicar imágenes parciales.
- La negociación v2 añade identificación de sesión y transferencia, reensamblado,
  deduplicación y paridad. Los datos atrasados de otra sesión no se tratan como
  imágenes de la reproducción actual.
- Hay límites de espera en los intercambios con la FPGA y limpieza de recursos
  al salir, cambiar de core o reiniciar, incluyendo las correcciones de cold reset.
- El procesamiento UDP trabaja por lotes, con presupuesto acotado para atender
  el stream sin monopolizar el bucle de MiSTer; cada variante conserva sus buffers
  y ajustes de transporte. Los logs están limitados y rotados.

| Receptor | Uso |
|---|---|
| `MiSTer_groovy` | UDP normal, por Ethernet o Wi-Fi; opción inicial sin un entorno XDP preparado |
| `MiSTer_groovy_XDP` | AF_XDP sobre Ethernet `eth0`; requiere kernel/driver, BPF y bibliotecas compatibles. No funciona sobre Wi-Fi |
| `MiSTer_groovy_wifi` | UDP con perfil y buffers orientados a Wi-Fi; también acepta Ethernet |

**Se conserva la retrocompatibilidad a nivel de protocolo:** los receptores
aceptan el protocolo legacy de los emisores anteriores y negocian v2 cuando
el emisor lo admite. A la inversa, la API del plugin intenta v2 y, si no recibe
su confirmación, vuelve al INIT y al formato de paquetes legacy para conectar
con receptores antiguos. En legacy no están disponibles las garantías propias
de v2; usar un receptor antiguo tampoco incorpora las correcciones de los nuevos.

Esto no equivale a haber probado todas las versiones de todos los clientes ni
convierte un kernel/RBF incompatible en compatible. Las mejoras no requieren
recompilar el RTL: mantener el RBF previamente validado y los requisitos del
transporte elegido. No se promete una reducción universal de latencia ni el
mismo caudal por Wi-Fi que por Ethernet.

Para instalarlos, copiar los receptores a `/media/fat/` y seleccionar **uno**
en la sección existente de `MiSTer.ini`, por ejemplo:

```ini
[Groovy]
main=MiSTer_groovy
```

Cambiar ese valor por `MiSTer_groovy_XDP` o `MiSTer_groovy_wifi` según el entorno.
No sobrescribir el ejecutable principal `MiSTer` ni arrancar las tres variantes
a la vez. Ver [instalación y rollback](INSTALACION_Y_USO.md) y
[detalles de los perfiles](Groovy_MiSTer/hps_linux/VARIANTES.md).

## Organización

| Carpeta | Contenido |
|---|---|
| `src/` | Plugin VLC: vídeo, audio, modos, reconexión y mandos |
| `Groovy_MiSTer/api/` | API, wrapper C compartido y LZ4 compilado desde fuentes |
| `Groovy_MiSTer/protocol/` | Protocolo, reensamblado, límites y perfiles compartidos |
| `Groovy_MiSTer/hps_linux/src/` | Main MiSTer completo integrado con Groovy y correcciones de cold reset |
| `Groovy_MiSTer/rtl/`, `sys/` | RTL, soporte MiSTer y proyecto Quartus |
| `tests/`, `Groovy_MiSTer/tests/` | Pruebas unitarias y transporte UDP local |
| `sdk/` | Cabeceras y bibliotecas de importación de VLC incluidas en el proyecto |
| `LICENSES/`, `third_party/` | Licencias, atribuciones, fuentes originales de dependencias y manifiestos SHA-256 |

No hace falta clonar `Groovy_MiSTer` ni `Main_MiSTer` en carpetas hermanas.
No se requiere vcpkg. No hay submódulos ni descargas durante la compilación.

Verificado el 2026-10-01: DLL Release x64, 23/23 pruebas y los tres receptores
ARM, también desde exports limpios sin las carpetas originales.

## Compilar en Windows

Requisitos: Visual Studio 2022, herramientas C++ v143 y Windows SDK.
Para las pruebas: CMake ≥ 3.22 y Python 3 en PATH.

```powershell
.\scripts\build-windows.ps1 -Test
```

Resultado Release x64: `x64/Release/libgroovy_mister64_plugin.dll`.
También se puede abrir `vlc-groovy-mister.sln`. La API y LZ4 se recompilan como
dependencia de la DLL; las carpetas antiguas `groovymister/` y `lz4/` ya no se usan.
Para VLC de 32 bits: `./scripts/build-windows.ps1 -Platform x86 -Test`.
Resultado: `Release/libgroovy_mister_plugin.dll`. Ejecutar las suites x64/x86
una después de otra, no simultáneamente, porque comparten puertos de loopback.
Ambas arquitecturas compilan en Release y superan 23/23 pruebas (2026-10-02).
La DLL x86 no se ha probado físicamente en VLC/MiSTer; las pruebas locales no
equivalen a esa validación. Ver las notas de la release.

Para ejecutar solamente la suite conjunta:

```powershell
cmake -S . -B build/tests -G "Visual Studio 17 2022" -A x64
cmake --build build/tests --config Release
ctest --test-dir build/tests -C Release --output-on-failure
```

Estas pruebas no abren VLC ni contactan con MiSTer: los fixtures de red usan
loopback. No ejecutar las suites UDP simultáneamente sobre los mismos puertos;
la prueba del mando utiliza 32100 y 32101.

## Compilar los tres receptores

En Linux/WSL, instalar bash, rsync, make y Python 3, y poner ARM GNU
10.2-2020.11 (`arm-none-linux-gnueabihf-gcc`) en PATH. Desde esta raíz:

```sh
bash Groovy_MiSTer/hps_linux/build-all.sh /var/tmp/groovy-receivers-build
python3 Groovy_MiSTer/tests/check_receiver_artifacts.py \
    /var/tmp/groovy-receivers-build Groovy_MiSTer/hps_linux/src
```

La carpeta de trabajo debe estar fuera de `Groovy_MiSTer` y ser exclusiva de
esta compilación. Para una reconstrucción limpia, elegir una carpeta nueva.
Los resultados, excluidos de Git, aparecen en `Groovy_MiSTer/hps_linux/`:
`MiSTer_groovy`, `MiSTer_groovy_XDP` y `MiSTer_groovy_wifi`.
Se pueden construir por separado con `build-standard.sh`, `build-xdp.sh` y
`build-wifi.sh`, pasando la misma clase de argumento.

Los scripts no despliegan ni ejecutan los receptores. Las bibliotecas ARM de
terceros incluidas son entradas de enlace, no fuentes reconstruidas por estos
scripts. XDP sigue requiriendo el kernel, programa BPF y bibliotecas adecuados
en MiSTer; no se sustituyen automáticamente.
Ver [diferencias e instalación](Groovy_MiSTer/hps_linux/VARIANTES.md).

## FPGA y validación física

Se conserva el proyecto `Groovy_MiSTer/Groovy.qpf` y el RTL local importado.
**Esta unificación no reconstruye ni valida un RBF nuevo.** Los cambios locales
del RTL no deben confundirse con el RBF que pasó las pruebas físicas. Mantener
el RBF validado; en particular, no asumir que el modo de arranque del RTL es
seguro para un CRT de 15 kHz. Ver el inventario de integración.

## Documentación

- [Inventario, procedencia y checklist de unificación](UNIFICACION.md).
- [Componentes y licencias conservadas](THIRD_PARTY_NOTICES.md).
- [Alcance de las licencias](LICENSE.md) y [estado de la revisión](AUDITORIA_LICENCIAS.md).
- [Procedencia de bibliotecas y fuentes adjuntas](third_party/README.md).
- [Guía de configuración](GUIA_CONFIGURACION_MODULO.md).
- [Guía de mejoras y pruebas físicas históricas](GUIA_MEJORAS_STREAM_VIDEO.md).
- [Detalle de pruebas](tests/README.md).

Los informes históricos conservan las rutas y los resultados de sus respectivas
tandas; los comandos de este README son los del repositorio unificado.
No se ha publicado ningún commit ni release desde los scripts de compilación.

Para exportar los archivos de trabajo que Git recogería (incluidos los nuevos,
sin hacer commit) a un ZIP nuevo:

```powershell
python scripts/export-sources.py dist/groovy-unified-sources.zip
```

El export no es una auditoría de secretos ni de licencias. Revisar el contenido
antes de publicarlo. Los informes históricos pueden contener rutas locales.
Incluye las bibliotecas precompiladas y 14 archivos de fuentes originales de
terceros (aproximadamente 49 MiB adicionales); no es un paquete exclusivamente
de código fuente ni se licencia todo como LGPL. Las identificaciones inferidas
de bibliotecas antiguas están señaladas en la documentación de procedencia.
