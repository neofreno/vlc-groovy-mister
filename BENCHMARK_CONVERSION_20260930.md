# Conversion exacta: medidas del 2026-09-30

## Evidencia del equipo de reproduccion (antes del cambio)

Dos logs proporcionados por el usuario con el mismo nombre modelines.txt, leidos
sucesivamente; el segundo sustituyo al primero. No se han ejecutado VLC ni MiSTer
desde el equipo de desarrollo.

| Entrada | Conversion media/maxima | Envio medio por campo | WaitSync medio | Descartes / reconexiones |
|---|---:|---:|---:|---:|
| 1280x720, 24000/1001 fps | 14.517 / 36 ms | 2.442 ms | 14.216 ms | 0 / 0 |
| 1920x1034, 24 fps | 15.738 / 23 ms | 1.953 ms | 14.704 ms | 0 / 0 |

Ambos salen a 720x480i, 59.939 Hz, cola pico 1. El segundo contiene 2343
imagenes aceptadas, 5853 campos y 5849 ACKs observados; los cuatro no observados
no prueban perdida. El usuario comunica audio correcto. Se prioriza conversion
y escalado sin modificar audio, sincronizacion ni receptores.

## Comparacion sintetica local

Equipo: Intel Core i7-1360P, Windows, MSVC Visual Studio 2022, Release x64.
Compara la referencia congelada tests/reference_frame_20260929.h con la nueva
funcion de produccion src/video_frame.h dentro del mismo proceso, mismas
opciones de compilacion y datos I420 pseudoaleatorios deterministas con padding.
No usa videos reales, decodificacion, VLC, red, audio ni CRT.

Cada cifra es la mediana de cinco lotes de diez conversiones, alternando el
orden referencia/candidato tras calentamiento. Destino 720x480, aspecto activado,
BT.709 limitado; SAR 3979:3976 para 1920x1034 y 1:1 para el resto. El suavizado
usa exactamente las mismas cajas enteras del conversor anterior.

Antes de cambiar produccion, ambos conversores identicos dieron aproximadamente
1.00x: suavizado 1280x720 3.841/3.847 ms y 1920x1034 4.683/4.605 ms. La diferencia
pequeña muestra variabilidad del ensayo, no una mejora del codigo.

Ultima de dos ejecuciones finales consecutivas:

| Entrada | Suavizado | Referencia ms | Nuevo ms | Aceleracion |
|---|---|---:|---:|---:|
| 1280x720 | No | 1.053 | 0.742 | 1.42x |
| 1280x720 | Si | 3.938 | 3.026 | 1.30x |
| 1920x1034 | No | 1.015 | 0.753 | 1.35x |
| 1920x1034 | Si | 4.732 | 3.854 | 1.23x |
| 1920x1080 | No | 1.055 | 0.759 | 1.39x |
| 1920x1080 | Si | 4.906 | 3.986 | 1.23x |
| 3840x2160 | No | 1.071 | 0.790 | 1.36x |
| 3840x2160 | Si | 7.109 | 6.430 | 1.11x |

La ejecucion final precedente dio, con suavizado, 3.858 -> 3.055 ms en 720p y
4.650 -> 3.882 ms en 1920x1034. En estas dos ejecuciones, reduccion de tiempo
aproximada del 17-23% para los tamaños de los clips del usuario. No extrapolar
estos milisegundos ni porcentajes al PC de reproduccion. No se mide aqui CPU
total, decodificacion, espera activa ni latencia fisica.

## Cambios y equivalencia

- Coordenadas de columnas precalculadas por bloques de 256 y limites verticales
  calculados una vez por fila/bloque, no por pixel.
- Scratch de 4096 bytes en pila; sin heap, cache compartida ni limite nuevo
  sobre anchos de salida validos. No se requieren AVX/SIMD adicionales.
- Media de cajas hasta 8x8 con reciproco entero y correccion del resto: division
  exacta, no aproximacion visual. Cajas mayores conservan el recorrido general.
- Aritmetica RGB de 32 bits dentro de limites demostrables y mismo redondeo y
  saturacion; constantes/matrices, SAR, bandas, BGR y filtros sin cambiar.
- La referencia original queda independiente y no se enlaza en la DLL.

Tests: igualdad completa incluyendo guardas, 1200 casos pequeños aleatorios,
720p/1034p/1080p/4K/vertical, tres matrices, ambos rangos, ambos filtros,
aspecto/SAR, crop impar, padding, rasters anchos y limites 255/256/257/511/512/513.
Dos conversiones concurrentes usan estados separados. Comprobacion exhaustiva
de todas las sumas posibles de 1..64 bytes y de 24 millones de valores para el
redondeo/saturacion RGB. Release y ASan superados (suite completa 8/8).

## Reproducir

```powershell
cmake --build x64/video-tests --config Release
ctest --test-dir x64/video-tests -C Release --output-on-failure
& x64/video-tests/Release/video_conversion_compare.exe --benchmark
```

No se establecen umbrales de velocidad en CTest: dependen del equipo/carga.
La comprobacion de pixels si falla ante cualquier diferencia.

## Pendiente en el equipo real

Repetir ambos clips 60-90 segundos con mismos ajustes y guardar cada log con
nombre distinto. Confirmar converter=exact-tiled-v1 y comparar conversion
media/maxima, descartes, cola y CPU. Comprobar tambien aspecto/suavizado y cambios
de resolucion. Mantener los receptores/RBF. El nuevo paquete conserva las
mejoras de la DLL anterior, incluida copia directa de audio, sin cambiarlas.

## Resultado real recibido despues (2026-09-30)

Ambos logs incluyen converter=exact-tiled-v1. El usuario confirma video y audio
correctos. No se ha ejecutado VLC en el equipo de desarrollo.

| Entrada / log | Media anterior -> nueva | Maximo anterior -> nuevo | Reduccion media observada |
|---|---:|---:|---:|
| 1280x720 / modelines2.txt | 14.517 -> 5.647 ms | 36 -> 28 ms | 61.1% |
| 1920x1034 / modelines.txt | 15.738 -> 8.067 ms | 23 -> 13 ms | 48.7% |

1533 y 1837 imagenes aceptadas (~64/~77 s), cero rechazos, descartes, errores
de envio o reconexiones; cola pico 1. Mismo destino 720x480i, 59.939 Hz.
Son comparaciones entre reproducciones, no una medida controlada de CPU total.
No sustituyen la validacion extensa de colores, modos o orden de campos.
