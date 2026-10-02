# Instalación y uso — VLC → Groovy MiSTer

## 1. Qué descargar

En [Releases](https://github.com/neofreno/vlc-groovy-mister/releases), descargar
el paquete Windows que coincida con **VLC**, y el paquete de receptores:

| Paquete | Contenido principal |
|---|---|
| `vlc-groovy-mister-v1.0.0-rc2-windows-x64.zip` | `libgroovy_mister64_plugin.dll`, para VLC de 64 bits |
| `vlc-groovy-mister-v1.0.0-rc2-windows-x86.zip` | `libgroovy_mister_plugin.dll`, para VLC de 32 bits |
| `vlc-groovy-mister-v1.0.0-rc2-receivers.zip` | `MiSTer_groovy`, `MiSTer_groovy_XDP`, `MiSTer_groovy_wifi` |
| `vlc-groovy-mister-v1.0.0-rc2-sources.zip` | Fuentes del proyecto, scripts, SDK, dependencias y documentación de licencias |

Los ZIP binarios incluyen instrucciones, avisos y manifiesto de compilación.
`SHA256SUMS.txt` identifica los archivos descargables; se puede comprobar un ZIP
con `Get-FileHash .\nombre-del-paquete.zip -Algorithm SHA256` en PowerShell.
No hace falta compilar las fuentes para instalar los binarios.

Esta entrega es una **release candidata**: DLL x64 y x86 compiladas,
23/23 pruebas locales por arquitectura y comprobaciones estáticas ARM. x86
todavía requiere prueba real en VLC/MiSTer. No se ha ejecutado VLC ni conectado
una MiSTer en el equipo de compilación. No se incluye un RBF ni un kernel nuevo.

## 2. Requisitos y precauciones

- Windows y **VLC 3 de escritorio**, instalado desde VideoLAN, de la misma
  arquitectura que la DLL. No es una DLL para VLC 4. La ABI objetivo es VLC 3;
  no se garantiza cada distribución/versionado de terceros.
- Microsoft Visual C++ Redistributable v14, de la arquitectura de VLC, igual o
  posterior al toolchain de compilación. Descargar desde
  [Microsoft](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
  Las DLL requieren MSVCP140/VCRUNTIME140; x64 también VCRUNTIME140_1.
- MiSTer con Groovy instalado y un `Groovy.rbf` compatible y previamente
  validado con la pantalla. Para una instalación inicial del core, consultar
  [Groovy MiSTer upstream](https://github.com/psakhis/Groovy_MiSTer).
- Ethernet Gigabit directa es la configuración preferida para el stream.
  Wi-Fi tiene menos caudal y mayor variabilidad. No abrir estos puertos a Internet.
- **CRT:** verificar los modos admitidos antes de reproducir. `15 kHz only`
  limita solo el modeline automático del plugin; no protege frente a un preset
  manual incompatible ni controla el modo de arranque del RBF. No sustituir el
  RBF probado por uno generado del RTL de este repositorio sin validación.
- Hacer copia de la DLL, receptores y `MiSTer.ini` actuales antes de actualizar.

## 3. Instalar la DLL en el PC de reproducción

1. Cerrar todas las ventanas y procesos de VLC.
2. Identificar si **VLC** es x64 o x86; Windows x64 también puede ejecutar VLC
   x86. La arquitectura del sistema operativo no basta para elegir la DLL.
3. Extraer el ZIP y copiar únicamente su DLL a `plugins\video_output\` dentro
   de la instalación de VLC. Rutas habituales:

   - x64: `C:\Program Files\VideoLAN\VLC\plugins\video_output\libgroovy_mister64_plugin.dll`
   - x86: `C:\Program Files (x86)\VideoLAN\VLC\plugins\video_output\libgroovy_mister_plugin.dll`

   Una instalación portátil o Windows de 32 bits puede usar otra ruta.
4. Guardar las DLL Groovy antiguas **fuera de la carpeta `plugins`**, incluidas
   las antiguas copias en `video_filter`. No mantener ambas arquitecturas ni
   duplicados activos en una instalación de VLC.
5. Si no aparece el módulo, abrir una vez VLC con `--reset-plugins-cache`, por
   ejemplo desde PowerShell en el PC de reproducción:

   ```powershell
   & 'C:\Program Files\VideoLAN\VLC\vlc.exe' --reset-plugins-cache vlc://quit
   ```

   Adaptar la ruta para x86. No usar `--reset-config`: no hace falta borrar las
   preferencias personales. Si la caché sigue obsoleta, cerrar VLC y guardar
   una copia de `plugins\plugins.dat` fuera de esa carpeta antes de regenerarla.

La API Groovy y LZ4 están enlazadas en el plugin; **no copiar otra DLL Groovy,
archivos `.lib` ni cabeceras del SDK**. `libvlc.dll` y `libvlccore.dll` deben ser
los de la instalación normal de VLC, no copias descargadas por separado.

## 4. Instalar y seleccionar un receptor en MiSTer

Detener primero la reproducción. Copiar los archivos del directorio `mister/`
del ZIP a la raíz de la SD (`/media/fat/`), en modo binario si se usa FTP.
No renombrar ni reemplazar el ejecutable principal `MiSTer` de la SD.

En `MiSTer.ini`, localizar la sección `[Groovy]` existente y cambiar **su única
línea `main=`**, sin crear secciones o selecciones duplicadas:

```ini
[Groovy]
main=MiSTer_groovy
```

Elegir un receptor:

| Valor de `main=` | Cuándo usarlo |
|---|---|
| `MiSTer_groovy` | UDP normal: punto de partida si no se dispone de una instalación XDP preparada |
| `MiSTer_groovy_XDP` | Ethernet `eth0` con kernel/driver AF_XDP y dependencias ya instaladas |
| `MiSTer_groovy_wifi` | UDP con perfil Wi-Fi; también acepta Ethernet, sin ajustes específicos de MTU de `eth0` |

Mantener `Groovy.rbf` validado en `_Utility` y realizar un apagado/encendido
normal después de sustituir el receptor, sin interrumpir escrituras en la SD.
Entrar en el core Groovy desde el menú. Los receptores son variantes del programa
Main de MiSTer; no se ejecutan a la vez como tres servidores independientes.

**XDP no funciona por Wi-Fi.** Necesita kernel con AF_XDP/driver apropiado,
`/usr/lib/arm-linux-gnueabihf/bpf/groovy_xdp_kern.o` compatible y `libelf.so.1`,
además de las bibliotecas normales del sistema MiSTer. El paquete no instala
kernel, BPF ni rootfs y no debe usarse para reemplazarlos a ciegas. Si no se
dispone de ese entorno, empezar por UDP normal. El detalle upstream está en su
[apartado de instalación](https://github.com/psakhis/Groovy_MiSTer#installation-transfers-in-binary-mode).

## 5. Conexión de red

Usar la **IP de la interfaz que transporta el vídeo**, que puede ser distinta de
la IP Wi-Fi usada para administrar MiSTer. Ejemplo de enlace directo aislado:
PC Ethernet `192.168.2.10/24`, MiSTer Ethernet `192.168.2.11/24`, sin necesidad
de puerta de enlace en ese enlace. Son ejemplos: comprobar que no colisionan
con las redes existentes y configurar las interfaces de forma persistente.

Permitir VLC en el firewall para esa red privada, con tráfico UDP de stream y
respuestas (`32100`) y del mando (`32101`), restringido a la MiSTer. No desactivar
el firewall completo ni configurar redirecciones de puertos del router.
Empezar con MTU 1500 y Jumbo Frames desactivado. En Wi-Fi, probar primero una
resolución baja y compresión; no se promete el mismo comportamiento que por cable.

## 6. Activar vídeo y audio en VLC

En Herramientas → Preferencias, seleccionar **Mostrar ajustes: Todo**:

1. Vídeo → Módulos de salida: seleccionar **Groovy Mister**.
2. En las opciones de esa salida, poner la IP MiSTer en **Mister Host**.
3. Seleccionar **Video Mode: Automatic**. Para un CRT de 15 kHz, mantener
   **15 kHz only (Automatic)** activado.
4. Activar **Aspect Ratio** para conservar la proporción en una pantalla física
   **4:3**. Un vídeo panorámico tendrá bandas; desactivarlo estira la imagen.
5. **Audio → Filtros: activar Groovy Mister**. Aunque todo esté en una sola DLL,
   el filtro de audio se activa por separado. Sin él puede haber imagen sin sonido.
6. **Interfaz → Interfaces de control: activar Groovy Mister**. La interfaz
   auxiliar rellena y guarda los campos de la modeline al elegir un preset fijo,
   incluso sin reproducción. Se añade a la interfaz habitual de VLC; no la sustituye.
7. Entrada/Códecs: desactivar decodificación acelerada por hardware. También se
   puede usar `--avcodec-hw=none`. Las superficies opacas D3D9/D3D11 no están
   admitidas por esta ruta; no seleccionar otra salida de vídeo como OpenGL.
8. Guardar y reiniciar VLC. No activar el antiguo filtro de vídeo Groovy.

La configuración completa utiliza **salida de vídeo + filtro de audio + interfaz
de control Groovy**. Esta última es el asistente de presets: no rellena Automatic
ni cambia Manual. El envío de vídeo y el mando dependen de la salida de vídeo,
no de esa interfaz auxiliar. El log `groovy mister config helper interface opened`
confirma que se ha abierto; `modeline preset ... synced to fields` indica una
sincronización. Si la ventana de preferencias conserva valores anteriores,
cerrarla y abrirla de nuevo para consultar los campos guardados.

Mantener el audio de VLC habilitado: no usar `--no-audio`. El filtro copia el
audio hacia MiSTer y conserva el bloque para la salida local, por lo que puede
sonar también en el PC. Si no se desea sonido local, silenciar los altavoces del
PC sin desactivar la decodificación de audio de VLC.

Ejemplo PowerShell completo para un CRT 4:3 de 15 kHz, en el PC de reproducción:

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

Adaptar IP, ruta VLC y fichero; en x86 normalmente cambia `Program Files` por
`Program Files (x86)`. Ejecutarlo con VLC cerrado evita que otra instancia ignore
opciones nuevas. Para uso diario también se puede abrir el vídeo desde la UI.
El comando define el filtro de audio de esa ejecución; si se necesitan otros
filtros, conservarlos en la configuración correspondiente.
Igualmente, conservar otras interfaces adicionales que ya se utilicen.
`--extraintf` añade Groovy; no sustituirlo por `--intf`, que reemplaza la interfaz
principal de VLC.

## 7. Ajustes durante la reproducción y mando

Los cambios de modeline, aspecto, suavizado y límite automático se detectan
al guardar las preferencias con `Runtime config poll cadence` distinto de cero
(30 frames por defecto). Durante una pausa se aplican cuando vuelven a llegar
frames. Cambiar la IP o activar/desactivar filtros requiere reabrir la reproducción.

- **15 kHz only** solo afecta a Automatic. Desactivado permite todos los modos,
  no obliga a 31 kHz; no desactivarlo en una pantalla que no los admita.
- **Smooth video reduction** suaviza reducciones; desactivarlo conserva vecino cercano.
- **Compress** activa LZ4. No altera la resolución; puede reducir el caudal necesario.
- **PAL/NTSC** describe el modo elegido, no la nacionalidad del vídeo. Automatic
  considera los FPS; 24 fps prioriza aproximadamente 60 Hz con y sin límite de 15 kHz.
- Subtítulos: seleccionar o desactivar la pista en VLC. Un subtítulo incrustado
  en los píxeles del vídeo original no se puede quitar como una pista separada.

En el OSD de Groovy, poner **Joysticks: Digital o Analog**, no Off. Las acciones
usan botones digitales; las letras dependen del mapeo del mando en MiSTer:

| Botón lógico | Acción |
|---|---|
| B2 / A | Pausa / reanuda |
| B3 / Y | Para |
| B4 / X | Elemento anterior |
| B1 / B | Elemento siguiente |
| Izquierda / derecha | Retrocede / avanza 1 segundo; repetición al mantener |
| Arriba / abajo | Capítulo siguiente / anterior; sin capítulos, secciones del 10% |

El mando requiere una salida de vídeo activa. Puede reanudar una pausa, pero
tras **Parar** puede ser necesario iniciar otra vez desde VLC. No se ofrecen
controles analógicos ni compatibilidad de mando con LibVLC embebido sin playlist.

## 8. Problemas, logs y vuelta atrás

| Síntoma | Comprobar primero |
|---|---|
| El módulo no aparece | Arquitectura VLC/DLL, carpeta, duplicados, runtime Microsoft y caché |
| No llega imagen | IP de stream, core cargado, receptor elegido, firewall, MTU 1500 y decodificación por software |
| XDP no arranca | Kernel/driver AF_XDP, programa BPF y libelf del entorno existente; probar UDP normal |
| Imagen sin audio | Filtro de audio Groovy activado, audio habilitado y pista seleccionada; buscar `Unsupported audio` |
| Vídeo deformado | Aspect Ratio activado y pantalla física 4:3; revisar el modo del propio monitor |
| El mando no responde | Joysticks Digital/Analog, salida activa y UDP 32101 |

El audio admitido por el filtro es float32 a 22050, 44100 o 48000 Hz; se envía
PCM16 estéreo. No se garantiza la conversión de todos los formatos multicanal.
En Herramientas → Mensajes, usar detalle **2** y guardar el log antes de cerrar.
En MiSTer, activar el log Groovy del OSD y guardar `/tmp/groovy.log` y
`/tmp/groovy.log.1` antes de reiniciar: son temporales.

Para volver atrás: detener VLC, restaurar su DLL anterior fuera de cualquier
duplicado, restaurar receptores y `MiSTer.ini` respaldados, y reiniciar MiSTer
normalmente. Conservar el RBF conocido; no sustituirlo por el de otra revisión
como intento de resolver un problema de red o de DLL.

## Licencias y fuentes

Los ZIP incluyen `LICENSE.md`, `THIRD_PARTY_NOTICES.md`, `LICENSES/` y la auditoría.
Las fuentes y avisos completos están en el ZIP `sources` **de esta misma release**,
no únicamente en la rama principal, que puede cambiar. Conservarlos al redistribuir.
Persisten las limitaciones de procedencia/licencia documentadas: publicar esta
release no convierte su auditoría en una certificación jurídica cerrada.
