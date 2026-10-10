# VLC → Groovy MiSTer v1.0.0-rc2 — actualización de audio 2026-10-10

**Los archivos descargables de esta release se han actualizado.** Incluyen la
corrección de audio del commit
[`70ef036`](https://github.com/neofreno/vlc-groovy-mister/commit/70ef0365ed614246022ec3c9ae58be9e64a8ec10),
la homepage del repositorio y la configuración completa de la interfaz Groovy.
RC1 y RC2 actualizadas contienen las mismas DLL corregidas x64/x86.

## Qué cambia

- El filtro preserva el formato de audio que devuelve a VLC. Una entrada de
  44100 Hz ya no se declara erróneamente como 48000 Hz; VLC puede realizar el
  remuestreo posterior para Windows sin alterar la copia PCM16 enviada a MiSTer.
- El log distingue `in_rate`, `out_rate`, `requested_out_rate` y `mister_rate`.
- 24/24 pruebas superadas en x64 y x86; regresión para 27 combinaciones de
  frecuencia/canales. En el log real aportado desaparecen los silencios
  correctivos repetitivos y los avisos de deriva. No se certifica todo el hardware.
- Los tres receptores son **idénticos a los originales de estas releases**.
  No cambia protocolo, RTL, RBF ni kernel. Los ZIP de receptores se actualizan
  solo para incorporar documentación y manifiestos coherentes.

## Qué instalar

Si ya tienes RC1/RC2, cerrar VLC, respaldar la DLL y sustituir únicamente el
plugin de su arquitectura dentro de `plugins/video_output/`. No instalar x86 y
x64 juntas. **No hace falta actualizar los receptores ni el RBF.**

Configurar Groovy en **Vídeo → Módulos de salida**, **Audio → Filtros** e
**Interfaz → Interfaces de control**. Esta última sincroniza los campos de los
presets y se añade con `--extraintf=vlc_groovy_mister`. Desactivar la decodificación
por hardware. Para un CRT de 15 kHz, usar Automatic y su límite de 15 kHz;
ese límite no protege frente a modos manuales ni al arranque del RBF.

Los paquetes llevan el prefijo `vlc-groovy-mister-v1.0.0-rc2-`:

- `windows-x64.zip` / `windows-x86.zip`: DLL e instrucciones para VLC 3.
- `receivers.zip`: `MiSTer_groovy`, `MiSTer_groovy_XDP` y `MiSTer_groovy_wifi`.
- `sources.zip`: fuentes del árbol de esta actualización y dependencias.
- `INSTALACION_Y_USO.md`: guía completa, también dentro de los paquetes.
- `SHA256SUMS.txt`: hashes actualizados; no reutilizar los de la descarga anterior.

XDP requiere Ethernet y un entorno AF_XDP/BPF/libelf ya preparado; no funciona
por Wi-Fi. Se conserva la compatibilidad legacy/v2 y las mejoras de estabilidad.

## Fuentes y trazabilidad: importante

**El tag histórico `v1.0.0-rc2` NO se ha movido.** Los enlaces automáticos de
GitHub **«Source code (zip/tar.gz)» no contienen esta corrección**. Para recompilar
las DLL actualizadas, descargar **`vlc-groovy-mister-v1.0.0-rc2-sources.zip`**
de los assets. Cada ZIP binario identifica su commit en `BUILD_MANIFEST.json`.
La actualización conserva copias locales de los assets anteriores; RC1 ya no
es una descarga alternativa del plugin anterior al arreglo de audio.

Se mantiene como pre-release: x86 sigue pendiente de validación física específica.
No se ha iniciado VLC local ni accedido a MiSTer para compilar. La versión interna
permanece `1.0.0-dev`. Se conservan licencias y límites de procedencia de la
auditoría, incluida en los paquetes; esta actualización no los declara resueltos.
