# VLC → Groovy MiSTer v1.0.0-rc2

Actualización de documentación y homepage del plugin. Se conserva la RC1 para
poder volver atrás; esta release tiene su propio tag, fuentes y hashes.

## Cambios respecto a RC1

- **Homepage de la pantalla de parámetros de vídeo** actualizada a
  <https://github.com/neofreno/vlc-groovy-mister>.
- DLL **x64 y x86 recompiladas** con ese cambio. No se cambian la autoría ni las
  licencias existentes, ni el funcionamiento del stream, audio o mando.
- README y guías indican activar Groovy en **Vídeo → Módulos de salida**,
  **Audio → Filtros** e **Interfaz → Interfaces de control**. Los ejemplos incluyen
  `--extraintf=vlc_groovy_mister`.
- Se explica la función de la interfaz auxiliar: sincroniza y guarda los campos
  de timings de presets fijos, incluso sin reproducir; no sustituye la interfaz
  principal ni realiza el streaming. No modifica Manual ni rellena Automatic.
- Los **tres receptores son idénticos a RC1**: se vuelven a empaquetar con la
  documentación actualizada. No requieren reinstalación si ya se utiliza RC1.
  Conservan las mejoras de estabilidad de streaming y compatibilidad legacy/v2.

## Descargas e instalación

Elegir `vlc-groovy-mister-v1.0.0-rc2-windows-x64.zip` o `windows-x86.zip` según
la arquitectura de **VLC**, no la de Windows. Con VLC cerrado, sustituir su DLL
en `plugins/video_output/`, conservando una copia anterior fuera de `plugins`.
No instalar las dos arquitecturas en el mismo VLC.

En Preferencias → Mostrar ajustes: Todo, activar también **Interfaz → Interfaces
de control → Groovy Mister**, guardar y reiniciar VLC. Mantener salida de vídeo,
filtro de audio, IP y ajustes de pantalla. Si la DLL no se actualiza en VLC,
regenerar la caché con `--reset-plugins-cache`, sin borrar las preferencias.

El paquete `receivers.zip` incluye `MiSTer_groovy`, `MiSTer_groovy_XDP` y
`MiSTer_groovy_wifi`; seleccionar solo uno en `[Groovy] main=` de `MiSTer.ini`.
XDP requiere kernel/driver AF_XDP, BPF y libelf previamente preparados; no funciona
por Wi-Fi. No se incluye ni cambia RBF, kernel o rootfs.

Leer [instalación y uso](https://github.com/neofreno/vlc-groovy-mister/blob/v1.0.0-rc2/INSTALACION_Y_USO.md)
y [configuración, parámetros y mando](https://github.com/neofreno/vlc-groovy-mister/blob/v1.0.0-rc2/README.md).
La guía también se adjunta a esta release y a cada paquete binario.

## Validación y límites

- MSVC v143 14.42, Release x64 y x86; 23/23 pruebas locales por arquitectura.
- Arquitecturas PE, exportaciones de VLC 3 y presencia de la nueva homepage
  comprobadas en ambas DLL sin cargar VLC.
- Receptores ARM hard-float y perfiles verificados; hashes iguales a RC1.
- No se ha ejecutado VLC local ni accedido a MiSTer. x86 sigue pendiente de
  validación física; por ello esta publicación continúa como pre-release.
- Usar decodificación por software. `15 kHz only` solo limita Automatic y no
  protege frente a presets/manuales incompatibles ni al arranque del RBF.
- La versión interna sigue siendo `1.0.0-dev`; el tag identifica el empaquetado.
- Las limitaciones de licencias/procedencia de la auditoría siguen vigentes.

`sources.zip` contiene las fuentes del mismo commit y las dependencias/avisos.
Cada paquete binario incluye `BUILD_MANIFEST.json`; `SHA256SUMS.txt` permite
verificar las descargas. Los ZIP de receptores pueden cambiar de hash por la
documentación aunque los tres ejecutables internos sean idénticos a RC1.
