# Pruebas del stream de video

No abren VLC ni conectan a MiSTer. Compilan los mismos helpers CPU utilizados por el plugin.
Requieren CMake >= 3.22 y Visual Studio 2022 con herramientas C++.

Desde la raiz del repositorio, en PowerShell:

```powershell
cmake -S tests -B x64/video-tests -G "Visual Studio 17 2022" -A x64
cmake --build x64/video-tests --config Release
ctest --test-dir x64/video-tests -C Release --output-on-failure
```

Comprobacion adicional de accesos a memoria (componente MSVC AddressSanitizer instalado):

```powershell
cmake -S tests -B x64/video-tests-asan -G "Visual Studio 17 2022" -A x64 -DVIDEO_TEST_ASAN=ON
cmake --build x64/video-tests-asan --config RelWithDebInfo
ctest --test-dir x64/video-tests-asan -C RelWithDebInfo --output-on-failure
```

Cobertura: primarios/negro/blanco, BT.601/709/2020, rango completo/limitado,
padding, recorte impar, escalado fraccionario, bordes, destino insuficiente,
campos pares/impares, limites de modo, saturacion de cola, cambio de generacion,
conversion tardia y acceso concurrente de productor/consumidor/cambio de modo.
Las comprobaciones permanecen activas en Release. Se verifican 10.000 ciclos de
reutilizacion sin cambiar las direcciones de los buffers.

Regresiones de aspecto/modo: CRT fisico 4:3 en distintos raster, SAR PAL/NTSC,
video vertical, bandas y alternancia de estirado sin pixeles residuales. Una API
simulada verifica 1.000 cambios de contenido sin reconfigurar hardware y 1.000
cambios de timings, incluidos fallos y reintentos. Se prueban tambien limites
de lectura del buffer de audio ante saltos de PTS y muestras insuficientes.

La prueba concurrente comprueba propiedad y contenido; AddressSanitizer no es
un detector de carreras. No sustituye a validar negociacion de formatos en VLC,
transporte de red, cadencia de campos, tiempos del CRT o sincronizacion A/V.

## Ampliacion 2026-09-17

La suite de video tambien comprueba takeDue (atrasos y frames futuros), seleccion
PAL/NTSC con FPS fraccionarios, ultimo preset, filtro horizontal real de 15 kHz
y reduccion de un patron alterno por promedio frente a vecino cercano.

Regresion Automatic/15 kHz del 20 de septiembre: se incluye la tabla de presets
de produccion (video_presets.h). El clip 1920x1034 a 24 fps debe elegir 720x480i
~60 Hz con limite y 720x480p ~60 Hz sin limite. Se prueban ocho FPS por seis
tamaños por dos estados del limite, 100 transiciones de modo efectivas, ignorar
el switch en Manual/preset fijo, conservar un modo de 15 kHz cuando es adecuado
sin limite, llegada tardia de FPS y equivalencia de fracciones. Los tests no
automatizan la ventana de preferencias de VLC ni sustituyen la prueba fisica.

Tanda de cola posterior: pendientes caducados a mas de 250 ms, limite inclusivo,
retencion de la ultima imagen, fechas futuras, retrocesos/duplicados, no liberar
buffers del conversor/emisor y cambio de generacion. Simulacion de 600 ticks por
cadencia con 24000/1001, 30000/1001, 25 y 50 fps. Agregados de tiempo con casos
vacios, intervalos negativos y overflow. Los mismos tests se ejecutan en Release
y AddressSanitizer; no simulan la sincronizacion de audio ni el orden fisico de campos.

La suite incluye `audio_timeline_tests`: reajuste
de fechas de audio al pausar/reanudar, conservacion de 2 segundos precargados,
pausas cortas/largas, 1.000 ciclos, duplicados de evento, lectura del ultimo
bloque, llegada tardia de audio, flush durante pausa y overflow. Se comprueba
la logica pura compartida con captura; no se ejecuta VLC ni se valida el orden
real de sus callbacks/driver. El usuario confirma superadas las pruebas fisicas
de reanudacion el 21 de septiembre; no sustituye las mediciones A/V prolongadas.

La suite incluye tambien `stream_startup_tests`:
receptor simulado disponible en el intento 12, espera creciente acotada a 2 s,
cancelacion antes/durante/despues de un intento, inicio inmediato sin espera,
10.000 fallos, limite de logs, overflow y cancelacion de una espera real con
condition_variable. Se usa la misma logica de reintento que en captura con
callbacks de API simulados; no se abre ningun socket en este test. Una prueba
fisica aparte debe comprobar arranque tardio y cierre con CMD_INIT en curso.

`stream_health_tests` eleva la suite a 4 tests: perdida de ACK, duplicados,
reordenacion, wrap, reinicio remoto, progresion sostenida sin reconexiones,
invalidacion de conversiones pendientes y reenvio del mismo modo tras perder
la sesion. La suite Release y ASan usa el mismo helper que el emisor.

## Repositorio unificado (2026-10-01)

La entrada recomendada para las 23 pruebas es el CMake de la raíz:
`cmake -S . -B build/tests`, build Release y `ctest --test-dir build/tests -C Release`.
No necesita las carpetas hermanas Groovy_MiSTer/Main_MiSTer. Las secciones
fechadas siguientes conservan la evolución histórica; para compilación ARM
actual usar los comandos del README de la raíz.

## Cold reset: reconexion de la API real sobre UDP local

```powershell
cmake -S tests/reconnect -B x64/reconnect-tests -G "Visual Studio 17 2022" -A x64
cmake --build x64/reconnect-tests --config Release
ctest --test-dir x64/reconnect-tests -C Release --output-on-failure
```

Requiere la carpeta integrada Groovy_MiSTer y Python 3. Compila la API real y
usa el watchdog/reintento de produccion con un receptor Python exclusivamente
en 127.0.0.1. Comprueba cuatro casos legacy/v2 y raw/LZ4: perdida de estado del
receptor durante 2.5 s, ACK duplicados que no evitan timeout, nueva sesion/puerto,
modo reaplicado antes del video y pixels exactos distintos antes/despues.
No ejecuta VLC, AF_XDP ni RTL; no demuestra ausencia de cuelgues en el core.

El guard de comandos legacy del receptor esta cubierto adicionalmente por
transport_safety_tests en Groovy_MiSTer: comandos 0..255 antes/despues de INIT
y con/sin modo. Se ejecuta tambien con ASan+UBSan Linux.

API real en loopback (Python 3; no abre VLC ni conecta con MiSTer):

```powershell
cmake -S Groovy_MiSTer/tests -B build/api-tests -G "Visual Studio 17 2022" -A x64
cmake --build build/api-tests --config Release
ctest --test-dir build/api-tests -C Release --output-on-failure
```

Cuatro tests: protocolo/reensamblado, seguridad de transporte, ciclo de vida API
y UDP loopback. Este ultimo comprueba ocho combinaciones de legacy/v2, raw/LZ4,
MTU 1500/9000, bytes exactos de 12 frames + 12 audios y reintentos de control.
El cliente de prueba espera el ACK real del frame antes de comprobar audio; no
supone que el servidor Python complete descompresion en 10 ms.

Ampliacion de cierre del 28 de septiembre: la suite de API tiene ahora 6 tests.
`resource_lifecycle_tests` comprueba el helper de orden de liberacion XDP,
10.000 cierres repetidos, inicializaciones parciales y reintentos sin desmapear
UMEM en uso. `core_cleanup_hooks` es una comprobacion de FUENTE: exige que
Main_MiSTer/fpga_io.cpp cierre Groovy antes de carga/reset de FPGA y exec.
No ejecuta AF_XDP ni simula un reinicio real del core.

En WSL/Linux se puede ejecutar ademas `Groovy_MiSTer/tests/posix_fd_exec_tests.cpp`
(g++ -std=c++14): usa el mismo helper FD_CLOEXEC del receptor y fork/exec real
para comprobar que un socket no se hereda. `resource_lifecycle_tests.cpp` se
ha comprobado tambien con -fsanitize=address,undefined.

`transport_safety_tests` cubre completaciones TX parciales, headroom RX de 256,
limites de chunk, double-free, agotamiento del pool, cabeceras truncadas,
fragmentacion IP rechazada, perdida del ultimo fragmento legacy y timeout FPGA
con reloj simulado. Los helpers se ejecutaron ademas con ASan+UBSan en WSL.
No es una prueba de integracion del kernel AF_XDP ni de la interfaz HPS/FPGA.

Build ARM sin despliegue: `../Groovy_MiSTer/hps_linux/build-xdp.sh` recibe el
arbol completo `Main_MiSTer` y una carpeta aislada de compilacion. Base usada:
`0b2e6eb2bdbcc0a73cb53e9a581b8a485c9a3292`, toolchain ARM GNU 10.2-2020.11,
`_AF_XDP=1`, `_WIFI_MODE=0`. No usar `src/build.sh` para esta verificacion:
contiene pasos de despliegue ajenos a la compilacion aislada.

Desde la tanda de cleanup, build-xdp.sh actualiza tambien fpga_io.cpp desde
Main_MiSTer y exige el hook de groovy_stop. El arbol completo Main_MiSTer es una
base local modificada, no basta su hash de commit: conservar estas modificaciones
y los overlays de Groovy_MiSTer para reproducir el receptor. No compilar una copia
antigua de fpga_io.cpp reutilizada dentro del directorio aislado.

## Tres receptores coherentes (2026-09-29)

La suite API tiene ahora **9 tests**: a los seis anteriores se suman
profile_standard_tests, profile_xdp_tests y profile_wifi_tests. Compilan el
mismo helper de perfiles usado por el receptor y comprueban requisitos de red,
aislamiento del ajuste de Ethernet, buffer/TOS y presupuesto de 256 paquetes.
No miden el rendimiento fisico ni ejecutan los sockets del receptor ARM.

Los cuatro tests del plugin pasan en Release y ASan; el loopback de reconexion
pasa sus cuatro casos. No se modifica la DLL en esta tanda.

Build conjunto, con objetos separados por variante (WSL/Linux, toolchain ARM
en PATH; no despliega ni ejecuta los receptores):

```sh
bash Groovy_MiSTer/hps_linux/build-all.sh /var/tmp/groovy-receivers-build
python3 Groovy_MiSTer/tests/check_receiver_artifacts.py /var/tmp/groovy-receivers-build Groovy_MiSTer/hps_linux/src
```

La segunda orden compara fuentes, cabeceras y hooks de las tres copias con los
origenes, verifica ELF ARM hard-float, identificadores de perfil/revision,
limpieza compartida, aislamiento de XDP, dependencias dinamicas y SHA-256 de los
binarios publicados. Requiere readelf. XDP conserva libelf y la dependencia
directa del cargador ARM presentes en el ejecutable previamente validado.

La reconstruccion final uso /var/tmp/groovy-receivers-release-20260929, sin
reutilizar objetos de tandas anteriores. Detalles e instalacion en
../Groovy_MiSTer/hps_linux/VARIANTES.md. La validacion fisica previa de cold reset
corresponde al XDP anterior; no valida automaticamente los tres nuevos builds.

## Video sin audio y audio tardio (2026-09-29)

Confirmacion posterior: el usuario comunica que los tres receptores funcionan
correctamente. La tanda siguiente cambia solo la DLL y requiere nueva prueba.

La suite del plugin contiene ahora **5 tests**, tambien bajo ASan.
`stream_audio_format_tests` usa la politica de formato de produccion: inicio sin
filtro, llegada a igual/distinta frecuencia, cierre/reapertura, formatos
incompatibles, no confirmar formato al fallar handshake, 10.000 transiciones y
primitivas de invalidacion de cola/reaplicacion del modo. No ejecuta captureloop.

La suite tests/reconnect contiene **2 tests**. El nuevo
`audio_session_udp_loopback` usa la API real y politica compartida contra UDP
127.0.0.1: seis frames y cuatro bloques PCM exactos, cuatro pares INIT/MODE con
frecuencias 48/44.1/22.05/48 kHz. Comprueba que iniciar sin audio, incorporar 48k
y retirar audio no generan INIT adicionales. Se repite con legacy/v2 y raw/LZ4.
Los comandos CMake/CTest de reconexion anteriores ejecutan ambas pruebas.

No sustituye la verificacion de callbacks VLC, buffers fisicos o interrupciones
al cambiar frecuencia. No se ejecuta VLC instalado ni se conecta con MiSTer.

## Metricas ACK y resumen periodico (2026-09-29)

El usuario confirma la DLL anterior de video sin audio. La nueva tanda no
modifica API ni receptores: instrumenta el emisor y necesita recoger logs reales.

Tests plugin: **6/6 Release y 6/6 ASan**. `video_ack_metrics_tests` cubre
coincidencias exactas, estados duplicados/desconocidos, salto y reordenacion,
wrap, nueva sesion, anillo limitado a 256, contabilidad de pendientes y 100.000
envios. Comprueba reloj regresivo/extremo, intervalo de 30 segundos y ausencia
de rafagas al recuperar de una pausa larga.

El cliente real `audio_session_client` usa ademas el tracker para comprobar
seis ACKs con su secuencia y que consultar otra vez el mismo estado no los
duplica. Los cuatro casos legacy/v2, raw/LZ4 del test UDP pasan. El timestamp
en ese fixture es sintetico: valida correlacion, no latencia de red real.
Regresion completa de red: 2/2; API: 9/9.

La API publica el ultimo estado consumido: ACKs anteriores pueden no observarse.
No interpretar `ack_unobserved` como perdida, ni `ack_observation` como RTT puro
o latencia fisica. No se cambian el consumo de ACKs ni el algoritmo WaitSync.

## Copia directa de audio (2026-09-29, posterior a metricas)

Suite plugin ampliada a **7/7 Release y 7/7 ASan**. El nuevo
`audio_ring_submit_tests` comprueba el helper real de copia directa: anillos
pequeños exhaustivos, wrap, pares estereo, capacidades/punteros invalidos,
guardas del destino, tres segundos maximos y 10.000 lecturas repetidas. Compara
bytes y particion de bloques con el recorrido previo y conserva el redondeo
unico de fechas, PTS duplicado, pausa y flush a las tres frecuencias admitidas.

`audio_session_client` utiliza ahora el mismo helper, con un anillo PCM de
20000 muestras y lectura de 19200 comenzando en 19996. Cada lectura se divide
en 32768+5632 bytes; el receptor Python comprueba el patron completo tras wrap.
Cuatro lecturas, ocho bloques, seis frames y cuatro INIT/MODE en cada caso
legacy/v2 y raw/LZ4. Red 2/2 y API 9/9 superadas.

La API usa su buffer de audio real; los tests no ejecutan VLC ni HPS/FPGA.
No es una medicion de CPU, latencia o continuidad de sonido fisico.

## Conversion/escalado exactos (2026-09-30)

Suite plugin **8/8 Release y 8/8 ASan**. `video_conversion_compare` compara el
conversor de produccion contra tests/reference_frame_20260929.h, copia congelada
independiente del codigo anterior. Compara todo el buffer y sus guardas en
1200 casos aleatorios, tamaños grandes, crop/pitch/SAR, matrices/rangos y ambos
filtros. Incluye conversion simultanea, limites de bloques, todas las sumas
posibles para medias de 1..64 bytes y 24 millones de valores de redondeo RGB.

Benchmark optativo, fuera de CTest y sin umbrales de velocidad:

```powershell
& x64/video-tests/Release/video_conversion_compare.exe --benchmark
```

Mismos datos/proceso/toolchain, mediana de cinco lotes alternados de diez frames.
Solo conversion sintetica local: no ejecuta VLC, decoder, audio, red ni CRT.
Resultados en ../BENCHMARK_CONVERSION_20260930.md. No utilizar ASan para comparar
rendimiento. Regresion API 9/9 y red 2/2 tambien superada.

## Mando del vout (2026-09-30)

Suite 10/10 Release y ASan. joypad_controls_tests comprueba flancos, mapeo,
repeticion acotada, destinos seguros con duracion corta/desconocida, limites
numericos y cola fija concurrente. joypad_vlc_tests compila src/joypad.cpp real
contra cabeceras/dobles de tests/joypad_stubs: vout sin intf, playlist antecesora,
acciones en hilo distinto al productor/API, mandos 1/2, pausa/reanudacion sin
frames nuevos, capitulos/secciones, ausencia de input, fallo de hilo y cierre
con referencias balanceadas. Los dobles no validan callbacks internos de VLC.

Red 3/3. joypad_session_udp_loopback usa src/groovymister_wrapper.cpp y API
reales con servidor local 127.0.0.1:32100/32101 (si estan ocupados, falla sin
interferir). Comprueba paquetes de 9/17 bytes, descarte de tamaño incorrecto,
suscripcion unica por sesion, reset de contador 100000 -> 1 y lectura segura
sin API/NULL. No ejecuta VLC instalado, HPS, FPGA ni dispositivos fisicos.

Se corrigio una carrera preexistente de test_audio_session.py: el evento de
cierre se activaba en INIT intermedios y podia detener el receptor antes de
leer el ultimo PCM. Ahora exige CLOSE final despues de todas las muestras;
conserva las comprobaciones de bytes. API 9/9 y build DLL Release superados.

## Ciclo de vida: interfaz y mando (2026-09-30)

11/11 Release y 11/11 ASan. interface_timer_tests usa el helper de produccion
con un doble de timer VLC: owners independientes, inicio duplicado, fallo de
creacion, limpieza RAII y cierre bloqueado hasta terminar callback en vuelo.
Los callbacks de cada timer son serializados conforme al contrato del SDK.

joypad_vlc_tests fuerza ventanas de carrera mediante barreras, sin sleeps:
orden extraida de cola, cambio A->B, reset, stop con init tardio, y cambio de
input entre validacion y setter. Comprueba referencias/identidad, acciones de
playlist bajo su lock, setters y releases fuera del lock, caducidad y cola
saturada. Estas pruebas compilan joypad.cpp real, pero no son VLC real ni red.

Ambas suites nuevas/modificadas superan 20 repeticiones consecutivas Release.
API 9/9 y UDP 3/3 tambien superados. No se realiza la tanda de medicion CPU/
WaitSync ni la prueba fisica prolongada, descartadas por el usuario.
