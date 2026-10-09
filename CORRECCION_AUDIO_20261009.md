# Corrección del formato de audio — 2026-10-09

Build local de prueba sobre RC2. No cambia receptores, RTL, API de transporte,
conversión PCM16 para MiSTer, temporización, colas ni opciones del plugin.

## Defecto corregido

El callback del filtro devuelve el bloque original a VLC sin convertirlo.
Sin embargo, su apertura conservaba el formato de salida propuesto por VLC,
que podía ser distinto del de entrada (por ejemplo 48000 frente a 44100 Hz).
Esto ocultaba a VLC la necesidad de remuestrear después del filtro.

Ahora se preservan codec y todos los campos del formato de audio de entrada
en la salida, sin copiar los punteros propiedad de `es_format_t`. El formato
se modifica solo después de completar la inicialización que puede fallar.
El remuestreador de VLC sigue siendo responsable de adaptar esa salida al
dispositivo Windows. MiSTer sigue recibiendo su copia independiente PCM16 estéreo.

## Instalación de prueba

1. En el PC de reproducción, cerrar VLC por completo.
2. Guardar la DLL RC2 anterior fuera de la carpeta de plugins.
3. Elegir `windows/x64/libgroovy_mister64_plugin.dll` para VLC x64 o
   `windows/x86/libgroovy_mister_plugin.dll` para VLC x86, y copiarla a la
   ubicación actual del plugin (`plugins/video_output/`). No instalar ambas.
4. Mantener receptores, RBF, modeline, conexión y configuración anteriores.
5. Si VLC conserva la versión anterior, regenerar su caché con
   `--reset-plugins-cache`; no borrar preferencias.

## Resultado de la comprobación — 2026-10-09

El usuario informa de mejora del sonido en MiSTer. El nuevo log de VLC confirma
`in_rate=44100 out_rate=44100` con salida Windows a 48000 Hz. En el tramo
registrado desaparecen las correcciones repetidas: 0 avisos de audio adelantado,
0 `timing screwed` y 0 silencios correctivos durante la reproducción. Hay una
inserción inicial de 1548 muestras (unos 32 ms a 48000 Hz) y dos vaciados al parar.
El emisor termina con 0 descartes de vídeo, 0 errores de envío, 0 reconexiones y
0 reconfiguraciones de audio. No se publican los logs originales del usuario.

Esto confirma la corrección del defecto observado en esa prueba; no certifica
la ausencia de cualquier microcorte físico ni mide todos los buffers de MiSTer.
El log aún muestra intentos DX11/DXA9 al arrancar: desactivar explícitamente la
decodificación por hardware. No se atribuyen a ellos cortes durante el stream.

## Cómo repetir la comprobación

Repetir aproximadamente 90 segundos del mismo vídeo, sin pausas ni saltos,
con Mensajes de VLC en detalle 2 desde antes de abrirlo. Pulsar Parar y guardar
el log completo, anotando si se siguen oyendo cortes. La compilación no inicia
VLC local ni accede a MiSTer.

Para entrada 44100 Hz y dispositivo Windows 48000 Hz, debe aparecer:

```text
passthrough=1 ... in_rate=44100 out_rate=44100 ... requested_out_rate=48000 mister_rate=44100 mister_channels=2
```

El `out_rate=44100` es la salida **del filtro**, no la del dispositivo Windows.
Después VLC debe poder configurar su conversión 44100 → 48000. En mono o
multicanal, `in_channels` y `out_channels` deben coincidir; `mister_channels=2`
describe únicamente la copia destinada a MiSTer.

Comparar si desaparece la secuencia repetitiva `playback too early`,
`timing screwed` y `playback way too early ... playing silence`. Una corrección
aislada de arranque no equivale a que continúe el problema. El cambio 48000 →
44100 del INIT inicial puede seguir ocurriendo al llegar tarde el primer audio;
esta corrección no cambia ese comportamiento.

Si persisten microcortes, conservar ambos logs: corregir la negociación del
formato no demuestra que no haya además otro problema de entrega/sincronización.

## Pruebas automatizadas

La nueva prueba usa los tipos reales del SDK y el mismo helper que producción:
27 combinaciones de 22050/44100/48000 Hz y 1/2/6 canales, codec, disposición,
duración y propiedad de buffers. El control `--simulate-old` debe fallar al
aceptar 44100 Hz como 48000 Hz. La suite raíz incorpora esa prueba como número
adicional (24 en total). Release x64: 24/24 superadas; Release x86: 24/24
superadas. El control negativo falla como se espera en ambas arquitecturas.
No se simula el driver de sonido; el resultado del log real se documenta arriba
con sus límites, sin dar por validadas todas las arquitecturas en hardware.

## Vuelta atrás

Cerrar VLC y restaurar la DLL RC2 respaldada, sin dejar duplicados en plugins.
No es necesario revertir ningún archivo de MiSTer.
