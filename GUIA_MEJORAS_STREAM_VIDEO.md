# Guia de correcciones del stream de video

Fecha de inicio: 2026-09-15. Base: revision del modulo VLC 3.0 y su API Groovy MiSTer enlazada.

## Objetivo y seguimiento

Estado actualizado 2026-09-30: conversion optimizada y mando confirmados fisicamente por el usuario. Corregidos por encargo los dos riesgos de la revision de ciclo de vida: temporizador por instancia y ordenes del mando ligadas al input/sesion, con admision cerrada al comenzar el cierre. DLL lifecycle compilada y tests 11/11 Release/ASan, API 9/9 y UDP 3/3. Validacion fisica de esta ultima DLL pendiente. Por decision expresa del usuario NO se realizaran la tanda de sincronizacion prolongada/orden de campos ni la evaluacion de CPU/WaitSync propuestas como puntos 3 y 4; las casillas historicas de esas tandas no constituyen trabajo activo. Audio, conversion, WaitSync, receptores y RBF sin cambios. El diagnostico del 16 de septiembre queda como registro historico en [DIAGNOSTICO_STREAM_XDP.md](DIAGNOSTICO_STREAM_XDP.md).

Mejorar primero la correccion y estabilidad del video; despues reducir CPU, copias y latencia, y finalmente ajustar calidad, sincronizacion y diagnostico.

- `[x]` implementado y comprobado localmente.
- `[ ]` pendiente. La validacion en VLC/CRT remoto se registra por separado: compilar no demuestra que el resultado visual o el timing sean correctos.
- La maquina de reproduccion esta conectada a MiSTer por Ethernet. Las pruebas de reproduccion se realizaran alli; este equipo se utiliza para editar, compilar y ejecutar pruebas independientes.
- Conservar la composicion de subtitulos en VLC y la retirada de Black Screen.
- Preservar los cambios locales anteriores de captura, audio y API.

## Orden de ejecucion

### 1. Formatos y conversion de pixeles (critico)

Problema: RGB24 y formatos YUV empaquetados se trataban como I420; BGRA/RGBA/ARGB compartian lector; RGB ignoraba pitch y recorte.

- [x] Negociar un formato CPU explicito y retirar la lista de formatos falsamente compatibles.
- [x] Validar numero de planos, punteros, pitches, lineas, recorte y dimensiones antes de leer.
- [x] Respetar offsets y filas con padding, incluyendo recortes impares en I420.
- [x] Convertir el orden de canales al formato esperado por Groovy MiSTer.
- [x] Respetar rango completo/limitado y matrices BT.601, BT.709 y BT.2020; definir fallback para metadatos ausentes.
- [x] Optimizar conversion/escalado sin modificar pixels: coordenadas por bloques, media entera exacta y RGB de 32 bits; verificar contra referencia congelada (2026-09-30).
- [x] Comprobar errores de reserva y limites antes de escribir el destino; inicializar bordes y todos los pixeles visibles.
- [x] Pruebas sinteticas de negro, blanco y primarios; padding, recorte, ultimo pixel y entrada invalida.
- [x] Compilar Release x64.
- [ ] Validacion remota: barras de color, fuentes RGB/YUV convertidas por VLC, recorte, rango completo, SD y HD.

### 2. Cambio seguro de resolucion (critico)

Problema: puntero modeline compartido entre productor/consumidor y frames antiguos interpretados con dimensiones nuevas.

- [x] Sustituir punteros prestados por copias de configuracion protegidas.
- [x] Validar timings, dimensiones, capacidad, altura entrelazada e indice de preset.
- [x] Identificar cada frame por generacion de modo, dimensiones, bytes y PTS.
- [x] Serializar cambio de modo y envio en el hilo consumidor.
- [x] Invalidar frames antiguos sin reutilizar buffers que siguen en conversion/envio.
- [x] Rechazar conversiones terminadas para una generacion que ya no esta vigente.
- [x] Comprobar fallo de configuracion de la API antes de aceptar envios del modo nuevo.
- [x] Pruebas de cambio pequeño/grande, conversion pendiente y repeticion del ultimo frame.
- [x] Compilar Release x64.
- [ ] Validacion remota: cambiar 240p/480i/576i durante reproduccion; repetir cambios rapidos y comprobar cierre.

### 3. Copias y asignaciones por frame

- [x] Retirar picture_NewFromFormat/picture_Copy de VoutDisplay si la conversion solo lee el frame.
- [x] Reutilizar buffers con propiedad explicita (libre, conversion, cola, envio).
- [x] Reservar memoria al iniciar/cambiar modo, evitando malloc/free en el recorrido estable.
- [x] Copiar directamente las lineas pares/impares al buffer API sin temporal de campo.
- [x] Mantener RGB888 compacto, sin padding innecesario en la cola.
- [x] Audio en el hilo emisor: copiar directamente del buffer circular al buffer de la API, sin temporal/malloc/free por envio. Mantener muestras, bloques y redondeo de fechas (tanda 2026-09-29).
- [x] Comprobar que un buffer en envio no puede ser reutilizado por el productor.
- [x] Pruebas de saturacion/reutilizacion y extraccion de campos con limites de memoria.
- [x] Compilar Release x64 y medir localmente ausencia de reasignaciones estables.
- [ ] Validacion remota: CPU y estabilidad con 1080p de entrada y salidas 240p/480i.

### 4. Cola, PTS y sincronizacion

- [x] Definir politica de saturacion: descartar pendientes antiguos, conservando el frame que ya se esta enviando.
- [x] Seleccionar el frame vencido mas reciente por fecha monotona de display; repetir el ultimo al ritmo de WaitSync. Falta validar la cadencia real.
- [x] Conservar numerador/denominador en seleccion de modo y evitar division entera al entregar FPS al audio. La revision completa del reloj A/V sigue pendiente.
- [ ] Distinguir frecuencia de cuadros de frecuencia de campos; comprobar orden de campos.
- [ ] Gestionar saltos, pausas, reanudaciones, cambio de pista y discontinuidades sin arrastrar colas antiguas.
- [x] Parte video: invalidar pendientes al retroceder la fecha de presentacion; conservar propiedad del conversor y del envio. No equivale a resolver discontinuidades de audio o detectar todos los seeks.
- [x] Limitar antiguedad de pendientes a 250 ms respecto a la fecha de presentacion; contabilizar repeticiones, expiraciones y descartes. La imagen retenida para pausa no caduca.
- [x] Pruebas de saturacion, PTS repetidos/no monotonos y cadencias fraccionarias.
- [ ] Validacion remota: 23.976/25/29.97/50/59.94 fps, progresivo/entrelazado y sincronizacion A/V prolongada.

### 5. Escalado y seleccion automatica del modo

- [x] Respetar SAR (proporcion del pixel) de origen y definir proporcion fisica de salida CRT (4:3 confirmado por el usuario).
- [x] Mantener vecino cercano y añadir promedio de area rectangular seleccionable para reduccion; no se ha añadido bilineal para ampliacion.
- [x] Considerar FPS del contenido y refresco real en la eleccion del preset.
- [x] Calcular frecuencia horizontal con pClock/hTotal para el filtro 15 kHz.
- [x] Recorrer todos los presets usando el tamaño real del array.
- [x] Respetar la casilla explicita de aspecto, incluso en modo automatico; obtener SAR de vd->source e invertirlo para orientacion de 90/270 grados.
- [ ] Validar cambios de recorte/orientacion en reproduccion real.
- [x] Pruebas de anamorfismo, bandas, ultima fila/columna y eleccion PAL/NTSC.
- [ ] Validacion remota visual en CRT 4:3 y contenido 16:9.

### 6. CPU, arranque y ciclo de vida

- [x] Sustituir el bucle sin frames por espera señalizada que permita parar y cambiar modo.
- [ ] Medir WaitSync de la API antes de cambiar su espera activa; evaluar espera temporizada mas ajuste final.
- [x] Permitir video sin audio y definir incorporacion posterior de audio (frecuencia/canales de CMD_INIT). Implementado el 29 de septiembre; el usuario confirma que funciona. Casos ampliados no descritos individualmente siguen pendientes.
- [x] Registrar fallo de inicializacion y permitir reintentos controlados en el emisor, sin terminar tras cinco fallos; espera creciente cancelable al cerrar. El vout no declara preparado el stream hasta aplicar el modo.
- [ ] Revisar estados globales compartidos y orden de cierre de video/audio.
- [x] Detectar falta de avance de ACK de video durante un segundo; detener media, invalidar colas y negociar nueva sesion/modo. Proteccion del receptor frente a comandos sin INIT/modo y limpieza al detener/arrancar servidor. Cold reset validado por el usuario tras la correccion adicional de cleanup.
- [x] Evitar escritura de configuracion a disco desde el recorrido de presentacion.
- [ ] Validacion remota: archivo sin audio, arranque lento, desconexion/reconexion y cierre durante espera.

### 7. Metricas y preparacion

- [x] Separar frames recibidos por captura, conversiones aceptadas, descartes y envios solicitados en el resumen de cierre.
- [x] Vincular el frameEcho observado del receptor con su envio/imagen/campo exactos. No equivale a confirmacion de scanout FPGA/CRT ni a capturar cada ACK recibido internamente por la API.
- [x] Registrar tiempos de conversion/envio/WaitSync, ocupacion actual/pico y antiguedad a primera solicitud; publicar acumulados cada 30 segundos y al cerrar.
- [x] Desactivar trazas por ACK de la API por defecto y conservar resumen final del plugin. El nivel de log del receptor es independiente.
- [x] No contabilizar una llamada void a CmdBlit como confirmacion de recepcion.
- [ ] Evaluar mover conversion a prepare con gestion de propiedad y presentacion por PTS.
- [ ] Medir antes/despues con mismo archivo, modo, compresion y equipo.

## Validacion y decisiones

Los bloques 1-3 se implementan primero. Los cambios de reloj/WaitSync y arranque sin audio requieren pruebas especificas y no se consideraran validados por el simple enlace de la DLL.

Referencias: src/module.cpp, src/capture.cpp, src/defaults.h, src/groovymister_wrapper.cpp y ../Groovy_MiSTer/api/groovymister.cpp.

Contrato vout VLC 3.0.16: https://github.com/videolan/vlc/blob/3.0.16/include/vlc_vout_display.h

## Registro de ejecucion

- 2026-09-15: creada guia y checklist. Comienza el bloque 1.

### Primera tanda implementada (bloques 1-3)

- Entrada CPU negociada con VLC: I420 o J420; orientacion normalizada. VLC se encarga de adaptar RGB/YUV empaquetado.
- Conversor independiente con lectura por pitch/offset y matrices BT.601/709/2020 (no implementa tone mapping HDR). Fallback sin matriz: BT.709 por encima de 576 lineas visibles; BT.601 en el resto.
- La salida mantiene B,G,R en memoria: comprobado en el camino RGB888 de Groovy.sv, donde los 24 bits bajos se asignan a {r,g,b}.
- Copias de modo por valor y generacion bajo mutex. Solo el consumidor cambia la resolucion de la API. Un cambio puede dejar terminar un envio ya iniciado con el modo anterior, pero nunca lo reinterpreta con el nuevo tamaño.
- Seis buffers de capacidad maxima (19 MiB aproximadamente), reservados al arrancar y liberados al cerrar. Esta reserva evita reallocaciones tambien al cambiar de modo. Cuatro pendientes, uno en envio y uno en conversion.
- Conversion directa del frame VLC, sin picture_Copy, malloc por frame ni buffer temporal de entrelazado.
- Saturacion: descartar el pendiente mas antiguo; el frame en envio permanece protegido. Se conserva FIFO entre los pendientes restantes. La seleccion por PTS corresponde al bloque 4.
- El hilo espera una señal si no dispone de ningun frame. Con uno disponible sigue el ritmo WaitSync existente y puede repetirlo.
- Resumen al cerrar: received, queued, rejected, dropped_full, dropped_mode, submitted, repeated, send_errors y queue_peak. Submitted significa llamada a API, no ACK. En entrelazado cuenta campos; repeated incluye campos del mismo frame.
- Se valida el indice del preset y los timings manuales antes de convertirlos a uint16_t. Configuraciones invalidas se rechazan sin publicar un modo parcial.
- Los cambios anteriores de audio y wrapper se conservan; la captura sigue usando el protocolo de la API enlazada y sus reintentos de inicio.

### Evidencia local

- Build de solucion Release|x64: correcto.
- CTest Release: correcto.
- CTest RelWithDebInfo + AddressSanitizer: correcto.
- Pruebas de conversion con primarios, negro/blanco, rango, matrices, recorte impar, padding, escalado fraccionario y ultimo pixel.
- Pruebas de campos pares/impares, capacidad, timings y rechazo de un frame cuyo tamaño no coincide.
- 10.000 ciclos sin reasignar buffers y prueba concurrente con 10.000 publicaciones y 1.000 cambios de generacion.
- AddressSanitizer valida accesos a memoria; no detecta carreras. La prueba concurrente comprueba contenido y propiedad de buffers.
- Instrucciones reproducibles: [tests/README.md](tests/README.md).
- No se ha ejecutado VLC ni se ha conectado a MiSTer desde esta maquina. Sigue pendiente validar visualmente conversion negociada, campos y cambios de modo en la maquina remota.

### Siguiente tanda

Prioridad actual: validar remotamente las correcciones de aspecto y cambio de modo siguientes antes de continuar por el bloque 4 (reloj/PTS, FPS racionales, discontinuidades y cadencia de campos). La dependencia actual del audio, el escalado por vecino cercano y la espera activa interna de la API siguen pendientes. No se han medido aun ganancias de CPU o latencia en el equipo de reproduccion.

### Regresion reportada y correccion (2026-09-16)

El usuario comprobo fisicamente aspecto incorrecto y congelacion/crash al cambiar resolucion o Aspect Ratio. Pantalla fisica confirmada: 4:3. Las pruebas locales anteriores no cubrian la geometria fisica, la aplicacion de modos de hardware ni el allocator de VLC. No se considera superada la validacion remota del bloque 2.

Hallazgos y checklist de esta correccion:

- [x] Sustituir la suposicion de pixeles cuadrados de salida por geometria fisica 4:3. Un video 16:9 ocupa el 75% de la altura tanto en 320x240 como en 720x480/576.
- [x] Leer SAR desde vd->source: VLC borra el SAR en el formato de los buffers de imagen. Invertir SAR si la orientacion intercambia ejes.
- [x] El modo Automatic ya no cambia por su cuenta el valor de la casilla Aspect Ratio.
- [x] Separar cambios de generacion de contenido de cambios de timings: cambiar solo aspecto vacia pendientes, pero no envia CMD_SWITCHRES.
- [x] Reintentar un modo cuyo envio falla, con espera interrumpible de 500 ms entre intentos. No terminar silenciosamente el hilo. El protocolo fiable ya dispone de sus propios intentos/ACK; en legacy no hay confirmacion equivalente.
- [x] Cargar Host solo al abrir y liberarlo con libvlc_free, no free del plugin MSVC. Evitar tambien el free inline de var_InheritString cuando la cadena esta vacia. Es un riesgo de allocator corregido; no se ha demostrado que sea la causa del crash remoto.
- [x] Retirar config_Put*/config_SaveConfigFile del recorrido de presentacion. El helper de preferencias conserva su sincronizacion de campos de presets; el renderer no modifica la casilla de aspecto.
- [x] Rechazar un segundo vout solapado mientras siga activo el primero, porque la captura/API mantienen estado global. Evita que una nueva apertura reinicie o libere recursos del anterior; no implementa multivout.
- [x] Acotar lectura de audio a muestras disponibles/capacidad, alineadas a canales. La resta anterior mezclaba signed/unsigned y no detectaba falta de muestras tras discontinuidades. No consumir el buffer si falla malloc. No sustituye la revision de reloj A/V pendiente.
- [x] Pruebas de SAR PAL/NTSC 4:3 y 16:9, vertical, SAR ausente, estirado desactivando aspecto y regeneracion de bandas sin pixeles residuales.
- [x] Prueba de estado con API simulada: 1.000 generaciones de contenido sin nuevo cambio de hardware, 1.000 cambios efectivos de modo, fallo/reintento y vuelta al modo anterior tras fallo.
- [x] Pruebas de limites de lectura de audio ante saltos de PTS, insuficiencia y datos invalidos.
- [x] Build Release x64, CTest Release y CTest con AddressSanitizer correctos. Los tests son independientes: no ejecutan VLC ni el transporte real.

Validacion fisica necesaria, exclusivamente en la maquina de reproduccion:

- [ ] Cerrar VLC, sustituir la DLL x64 y comprobar que no quede otra copia antigua cargable del plugin.
- [ ] Mismo clip 16:9 con Aspect Ratio activado: bandas arriba/abajo y proporcion visual constante en 320x240, 720x480i y 720x576i. Usar solo modos admitidos por el CRT.
- [ ] Clip 4:3, incluido anamorfismo: llenar la pantalla sin deformacion. Clip vertical: bandas laterales.
- [ ] Alternar la casilla 20 veces, tambien en Automatic: no debe producir una nueva entrada de log "Stream mode applied" si los timings no cambian.
- [ ] Alternar 240p/480i/576i 20 veces, dejar reproducir y cerrar VLC; comprobar que no hay congelacion permanente ni crash.
- [ ] Si falla, guardar log VLC con detalle 2, modos exactos de origen/destino, version de VLC, ajustes de compresion y volcado de crash del equipo remoto. No afirmar causa definitiva solo a partir de estas correcciones.

Referencias de contrato: [VLC 3.0.16: SAR de los buffers](https://github.com/videolan/vlc/blob/3.0.16/src/video_output/display.c#L80) y [libvlc_free / runtime de C](https://github.com/videolan/vlc/blob/3.0.16/include/vlc/libvlc.h#L269).

Artefacto historico de la correccion del 16 de septiembre: SHA-256 `A061C7FF29FF992D595014C5A91BEC4D6B26829CACBA56FAF735CAEB7A7A4949`. Sustituido por el paquete siguiente.

### Tanda de estabilidad Ethernet/XDP (2026-09-17)

Estado: implementado y comprobado localmente; NO validado en MiSTer fisica. El usuario autoriza compilar ambos proyectos y realizara la prueba directa por Ethernet. No se ha ejecutado VLC local ni desplegado remotamente.

- [x] Limitar groovy_poll a cuatro iteraciones; XDP recibe hasta 64 paquetes por lote y hasta 256 por llamada si hay cola.
- [x] Eliminar la espera ilimitada de FILL y restaurar el despertar RX cuando el kernel lo solicita.
- [x] Recuperar las direcciones exactas de COMPLETION TX; no reutilizar buffers pendientes.
- [x] Aceptar el desplazamiento de datos RX dentro del chunk (headroom) y devolver su base al pool.
- [x] Reducir UMEM de 256 MiB a 32 MiB, conservando espacio para los anillos RX/TX.
- [x] Validar Ethernet/IPv4/UDP y capacidad antes de leer/copiar. Rechazar fragmentacion IP, opciones IP y VLAN en esta ruta.
- [x] Ensamblar legacy en memoria CPU; publicar a FPGA solo al completar. Expirar a 80 ms y eliminar el anuncio forzado de LZ4 incompleto.
- [x] Admitir fragmentos legacy de MTU normal y aprender un chunk mayor del primer datagrama; no confundir MTU del receptor con el del emisor.
- [x] Conectar el protocolo v2 existente al camino XDP, incluidos INIT/ACK y expiracion. Cada fragmento tiene identidad/indice y se descartan incompletos; FEC recupera una perdida por grupo.
- [x] Acotar los siete bucles de handshake FPGA y dos esperas de reset a 100 ms cada uno; detener ACK tras fallo y exigir nuevo INIT. El limite rodea los reintentos, no puede recuperar una llamada de driver bloqueada internamente.
- [x] Solicitar reset FPGA en cada nueva sesion incluso sin screensaver, y no confirmar INIT fallido.
- [x] Evitar lectura de un byte fuera de datagramas UDP de longitud impar al calcular checksum.
- [x] Calcular duracion de linea/campo API con precision submicrosegundo antes del redondeo final.
- [x] Seleccion PAL para 25/50 y NTSC para 23.976/29.97/59.94, siempre entre los presets disponibles; 24p conserva cadencia 2:3, no se convierte en 60 imagenes distintas.
- [x] Cola por fecha de display con contador dropped_late; conservar frames futuros y descartar antiguos cuando ya existe uno vencido mas reciente.
- [x] Reduccion de video por promedio rectangular, activada por defecto mediante mister-groovy-smoothvideo. Vecino cercano desactivandola; ampliaciones siguen con vecino cercano.
- [x] Compilar DLL x64/API y receptor ARM hard-float; comprobar que dependencias ELF y versiones requeridas coinciden con el binario XDP anterior.
- [x] CTest plugin Release y AddressSanitizer; cuatro pruebas API/transporte, incluido loopback con ocho combinaciones legacy/v2, raw/LZ4 y MTU 1500/9000. Se comprueban 12 frames y 12 bloques de audio exactos por combinacion.
- [x] Helpers de protocolo/seguridad bajo ASan+UBSan en Linux: perdida, duplicados, reordenacion, timeout, limites, completaciones parciales, headroom y espera agotada.
- [ ] Probar DLL + receptor juntos en Ethernet fisica, incluyendo cierre/reapertura y cambio de modo.
- [ ] Medir CPU, latencia, duracion de conversion y cadencia sostenida en el PC real.
- [ ] Validar 240p/288p, entrelazado, orden de campos, pausas/saltos y A/V prolongado.

Limites: las pruebas no ejecutan el kernel XDP ni la FPGA. Legacy no puede detectar duplicados/reordenacion de fragmentos completos porque no contienen identificadores. El filtro de area usa cajas enteras, no integracion fraccionaria exacta. No se han cambiado los RTL/RBF existentes ni completado el arranque sin audio. La anterior politica FIFO descrita en el registro historico queda sustituida por takeDue.

En VLC 3, picture->date entregado a display ya pertenece al reloj monotono de presentacion: [video_output.c](https://github.com/videolan/vlc/blob/3.0.16/src/video_output/video_output.c). Se compara con mdate(), no con el PTS de medios sin convertir.

Instalacion y checklist fisico: [PRUEBA_ETHERNET_20260917.md](PRUEBA_ETHERNET_20260917.md). Paquete final preparado el 20 de septiembre: dist/groovy-stream-fix-20260920.zip; huellas individuales dentro de SHA256SUMS.txt. Se repiten compilaciones y tests antes de empaquetar.

### Validacion del usuario y ajuste de Automatic / 15 kHz (2026-09-20)

- [x] El usuario confirma que sus pruebas de cambios de resolucion y Aspect Ratio funcionan sin cortes de audio/video ni cuelgues. Esto no implica haber ejecutado todas las pruebas prolongadas del checklist.
- [x] Etiqueta visible `15 kHz only (Automatic)` y ayuda explicando que no afecta a Manual ni a presets fijos.
- [x] Detectar el cambio de only15khz por si solo durante reproduccion en Automatic; aplicar por la cola de cambios segura existente, sin reiniciar audio/API.
- [x] Desactivado permite todos los presets: NO fuerza una frecuencia superior a 15 kHz (decision explicita del usuario).
- [x] Actualizar seleccion cuando aparecen FPS validos despues de abrir el vout; usar formato de origen, con fallback a formato de imagen. No borrar FPS conocidos si faltan temporalmente ni reconfigurar por fracciones equivalentes.
- [x] Log Automatic con dimensiones, FPS racionales, limite, indice del preset, frecuencia horizontal y refresco; aviso explicito si FPS desconocidos.
- [x] Compartir la tabla real de presets con los tests mediante video_presets.h, sin cambiar ningun timing.
- [x] Regresion exacta: 1920x1034 a 24 fps selecciona preset 6 (720x480i, 15.734 kHz, 59.939 Hz) con limite y preset 8 (720x480p, 31.469 kHz, 59.940 Hz) sin limite.
- [x] Pruebas con ocho cadencias, seis tamaños y ambos limites; 100 alternancias efectivas, no reaccion en Manual/fijo, FPS tardios y estado sin cambios.
- [x] Compilacion Release x64, CTest Release y AddressSanitizer correctos.
- [ ] Repetir el clip del usuario con esta DLL y guardar la linea Automatic si no coincide con los dos presets previstos.

Diagnostico acotado: con la tabla y selector anteriores, 24 fps conocidos YA elegian NTSC en ambos casos. Con FPS desconocidos se reproduce la seleccion 720x576i PAL por dimensiones. Leer FPS solo al abrir era una carencia corregida; sin el log del equipo remoto no se confirma que sea la causa de su observacion. 15/31 kHz son frecuencias horizontales, independientes de la familia vertical 50/60 Hz. A 24 fps se mantiene la politica 2:3 sobre ~60 Hz, sin aceleracion PAL ni interpolacion.

Entrega incremental: dist/groovy-auto15khz-20260920.zip, solo DLL y notas. Mantener el receptor XDP/RBF de la prueba estable; no se han modificado la API ni el receptor en esta tanda.

### Confirmacion de Automatic y tanda de cola/metricas (2026-09-20)

- [x] El usuario confirma que Automatic/15 kHz ya funciona correctamente con la DLL incremental anterior.
- [x] Caducar solo frames READY cuya fecha de presentacion queda mas de 250 ms atras; los futuros se conservan y el ultimo frame retenido puede repetirse durante pausa.
- [x] Al publicar una fecha menor que la anterior, liberar solo pendientes READY de la linea temporal anterior. No liberar frames WRITING/SENDING ni reinterpretar buffers de otra generacion.
- [x] Fechas iguales conservan la imagen mas reciente; generaciones nuevas reinician el seguimiento de fechas.
- [x] Contadores dropped_expired, dropped_discontinuity y pts_regressions separados de dropped_late/full/mode.
- [x] Distinguir progressive_submitted y fields_submitted. Son solicitudes de envio, NO confirmaciones de hardware; repeated conserva su significado anterior.
- [x] Medir promedio/maximo de conversion, envio, WaitSync y edad de primera solicitud por imagen, con resumen al cerrar y sin logs por frame ni reservas adicionales.
- [x] Tests de expiracion, limite de 250 ms, retencion durante pausa, frames futuros, regresion/duplicados, buffers en uso y rechazo de generaciones obsoletas.
- [x] Simulaciones de 600 ticks con 24000/1001, 30000/1001, 25 y 50 fps: seleccion por fechas enteras, sin presentar antes de tiempo ni perder imagenes en la cadencia compatible.
- [x] Release x64, CTest Release y CTest AddressSanitizer correctos.
- [ ] Validar fisicamente pausas/reanudaciones, saltos adelante/atras y cambios de modo con esta DLL.
- [ ] Recoger resumen de tiempos en el PC de reproduccion antes de cambiar la espera activa interna de la API.

No se cambia WaitSync, el algoritmo de paridad/orden de campos, el audio, Automatic, API, receptor ni RBF en esta tanda. La deteccion observa la fecha monotona de presentacion, NO el PTS de medios original: no todos los saltos de VLC hacen retroceder esa fecha. La revision A/V completa y el orden de campos siguen pendientes. Medir no demuestra una mejora de CPU o de latencia; la edad hasta primera solicitud no incluye presentacion fisica en CRT.

Entrega incremental: dist/groovy-video-queue-20260920.zip. Conservar la DLL Automatic/15 kHz anterior como version fisicamente comprobada para poder volver atras.

### Audio al reanudar (2026-09-20)

Resultado fisico de la tanda anterior: el usuario confirma saltos correctos; al reanudar tras pausa el video avanza durante 1-2 segundos sin audio. Este fue el fallo corregido en esta tanda; el 21 de septiembre el usuario confirma superadas las pruebas fisicas posteriores.

Mecanismo encontrado: VLC desplaza las fechas al reanudar, pero un audio-filter no recibe el callback pause del audio-output. Nuestra copia de muestras retenia fechas antiguas y el siguiente bloque con fechas nuevas podia provocar su descarte; faltaba tambien pf_flush para los seeks. Se rechazaba toda lectura si la fecha del video superaba el INICIO del ultimo bloque, incluso quedando muestras utilizables. Es una explicacion de fuente compatible con el sintoma, no una captura del incidente remoto.

- [x] Observar INPUT_EVENT_STATE del input de la playlist propietaria del filtro (o input ancestro directo). Retener referencia y retirar callback antes de cerrar/liberar audio. No buscar inputs globales de otras instancias.
- [x] Al pausar, dejar de consumir/enviar audio; al reanudar, desplazar base, final y ultima fecha de video consumida por el tiempo de pausa observado. Conservar las muestras precargadas.
- [x] Registrar pf_flush para vaciar la cola por peticion de VLC; un seek durante pausa vacia muestras sin perder el estado pausado.
- [x] Comparar continuidad con el final del bloque anterior, no con su inicio; actualizar el final incluso despues de un reset.
- [x] Leer muestras disponibles aunque el video alcance/supere el final de audio recibido, respetando capacidad/canales. No esperar al inicio del siguiente bloque para usar la cola actual.
- [x] No marcar una fecha de video como consumida cuando no hay muestras o falla malloc; permitir reintento cuando llega audio.
- [x] Consultar disponibilidad de audio/buffer API antes de consumir muestras; acotar escritura circular incluso para un bloque mayor que la capacidad.
- [x] Tests de retencion de 2 segundos de prebuffer, pausas de 0.1/5/60/3600 segundos, 1.000 ciclos, callbacks duplicados, ultimo bloque, audio tardio, flush durante pausa y overflow.
- [x] DLL Release x64 y CTest 2/2 tanto Release como AddressSanitizer.
- [x] Validacion fisica de la correccion: el usuario comunica que las pruebas quedan superadas (2026-09-21), tras el informe anterior de saltos correctos. No se ha recibido una captura de logs ni una medicion de desfase.
- [ ] Mantener para pruebas ampliadas: salto estando pausado, cambio de pista y medicion A/V prolongada; no asumir casos concretos no descritos en la confirmacion general.

Limites: el tiempo se toma de los eventos de estado del input, no de un timestamp privado del core. Debe comprobarse precision A/V en hardware. Si no hay playlist/input propietario accesible (por ejemplo ciertos usos embebidos de LibVLC), se avisa en log y no se garantiza el reajuste de pausa. No se cambia el conversor de formatos de audio en esta tanda ni se corrige toda la negociacion PCM pendiente. Las pruebas unitarias no ejecutan callbacks de VLC ni su driver de audio.

Referencias VLC 3.0.16: [pausa del audio-output y flush de filtros](https://github.com/videolan/vlc/blob/3.0.16/src/audio_output/dec.c#L397), [desplazamiento de fechas de video](https://github.com/videolan/vlc/blob/3.0.16/src/video_output/video_output.c#L1201), [recursos de audio de la playlist](https://github.com/videolan/vlc/blob/3.0.16/src/playlist/engine.c#L250).

Entrega: dist/groovy-audio-resume-20260920.zip, solo DLL. Mantener receptor XDP y RBF existentes; no se modifican API ni cola/modos de video en esta correccion.

### Arranque con receptor tardio (2026-09-21)

Base fisicamente validada: dist/groovy-audio-resume-20260920.zip. Se conserva para rollback.

Fallo encontrado en fuente: despues de cinco errores de CMD_INIT el trabajador terminaba, pero threadStarted/timer_initialized seguian indicando que ya estaba creado. No se volvia a intentar hasta cerrar el vout. La API enlazada ya devuelve error al agotar el handshake y limpia sus sockets entre intentos; faltaba recuperacion en el emisor.

- [x] Mantener un unico trabajador intentando iniciar mientras la reproduccion siga abierta, sin limite artificial de cinco intentos.
- [x] Esperas entre intentos de 100, 200, 400, 800, 1600 y como maximo 2000 ms, ademas del tiempo de handshake. No esperar desde el callback de video ni audio.
- [x] Cancelar la espera al cerrar mediante la condicion existente; comprobar cierre antes de reintentar y despues de que termine CMD_INIT. No publicar exito tardio tras un cierre observado.
- [x] No mantener videoMutex durante llamadas de red. Conservar los cambios de modo solicitados durante el arranque para aplicarlos al conectar.
- [x] Limitar avisos a los primeros seis fallos y cada diez posteriores. Registrar numero de intentos/fallos y recuperacion; no añadir logs por frame.
- [x] Mantener el estado no preparado hasta que se aplique el modo; no acumular video/audio durante un inicio fallido.
- [x] Probar exito en el intento 12, exito inmediato sin espera, cancelacion antes/durante/despues del intento, 10.000 fallos con backoff acotado y overflow del contador.
- [x] Probar cancelacion de una espera real con condition_variable de dos segundos, sin esperar a que venza ni lanzar un intento posterior.
- [x] Compilar DLL Release x64; CTest 3/3 Release y 3/3 AddressSanitizer.
- [x] Verificacion final 2026-09-28: repetir build, ambas suites, 20 ejecuciones consecutivas de startup y CTest API 4/4 (incluido UDP local). DLL SHA-256: `E103347316A4A05855C1767C62531D756DC120FFEC9C7E4E3AF3571FB61CA7C7`.
- [x] El usuario confirma que abrir Groovy despues de comenzar el video funciona (2026-09-28); no comunica la duracion exacta de espera.
- [ ] Prueba fisica nueva: cerrar/parar mientras el receptor no responde; comprobar cierre y reapertura posterior. La espera entre intentos se cancela, pero una llamada CMD_INIT en curso termina segun los timeouts de la API.
- [ ] Regresion breve: inicio normal, pausa/reanudacion, salto y cambio de resolucion/aspecto.

Limites: esta tanda recupera el INICIO fallido; no implementa deteccion/reconexion tras perder un stream ya establecido. Tampoco elimina aun la dependencia de la primera muestra de audio. Si la IP/configuracion es incorrecta, seguira reintentando con espera hasta parar; no se añade dialogo de error a VLC. No se ha ejecutado VLC local ni conectado/desplegado en MiSTer.

Entrega incremental preparada el 28 de septiembre: dist/groovy-startup-retry-20260928.zip, solo DLL y notas. Repetidas compilacion y pruebas locales antes de empaquetar. API, XDP, RBF, WaitSync, conversion, cola y audio conservan su funcionamiento anterior. Siguiente punto de implementacion: arranque sin audio con negociacion segura si el audio aparece despues; las mediciones fisicas pendientes no se sustituyen por esta correccion.

### Perdida de sesion por cold reset (2026-09-28)

El usuario confirma arranque tardio correcto, pero un cold reset completo (vuelta al inicio/menu, no reset interno de Groovy) mientras VLC sigue reproduciendo acaba en congelacion al cargar de nuevo el core. Se interrumpe la siguiente tarea de arranque sin audio para atender esta regresion.

Hallazgos de fuente: el emisor no comprobaba perdida de avance de ACK y continuaba enviando la sesion anterior indefinidamente. El receptor no invalidaba toda su sesion al detener/reabrir servidor; legacy solo exigia que existiese poc, que tambien puede existir por el logo. No es una captura del cuelgue real: en un reinicio completo el proceso puede empezar de cero, por lo que no se atribuye el fallo observado exclusivamente a estado HPS retenido.

- [x] Watchdog monotono de 1000 ms desde el primer envio, renovado solo por un frameEcho que avance. Duplicados, ACK atrasados y contador remoto reiniciado no renuevan el plazo; se admite wrap del contador.
- [x] Mantener la lectura normal de ACK dentro de WaitSync para no sustraerle correcciones de raster. Antes de declarar timeout, hacer una ultima lectura no bloqueante por si acaba de llegar una confirmacion.
- [x] Al detectar perdida, deshabilitar captura, vaciar audio y pendientes de video, liberar la imagen retenida e invalidar la generacion del conversor sin reutilizar su buffer en uso.
- [x] Renegociar CMD_INIT con los reintentos cancelables anteriores; reiniciar contador de frame y cache de modo. Reaplicar incluso los mismos timings antes de aceptar media. Cambios de preferencias durante la espera siguen protegidos por la cola existente.
- [x] Reiniciar el estado de binding de inputs: CMD_INIT cierra tambien ese socket y debe volver a abrirse al recuperar conexion.
- [x] Contador reconnects y aviso unico al detectar cada perdida; sin nueva traza por frame ni cambios en el algoritmo WaitSync.
- [x] Receptor: invalidar sesion, reensamblados, modo y estados de audio/video al parar/arrancar servidor, sin acceder a FPGA durante la invalidacion de software.
- [x] Receptor: exigir INIT antes de comandos legacy de hardware y modo antes de video/audio, aunque exista poc del logo; no publicar transferencias si la sesion esta desconectada o hay fallo FPGA.
- [x] Tests de watchdog: duplicados, reordenacion, timeout, wrap, reset, 10.000 avances normales, invalidacion de conversion pendiente y reaplicacion de modo identico en sesion nueva.
- [x] Tests de permiso de comandos legacy: 256 valores, conectado/desconectado y con/sin modo; validacion bajo ASan+UBSan en Linux.
- [x] API real sobre UDP loopback con watchdog de produccion: receptor simulado pierde la sesion durante 2.5 segundos, ACK duplicados de la sesion anterior, reintentos de INIT, nueva sesion/puerto, MODE antes de VIDEO y pixels exactos. Cuatro casos (legacy/v2, raw/LZ4), tres ejecuciones consecutivas correctas. El test no ejecuta el receptor HPS ni VLC.
- [x] DLL Release x64; CTest plugin 4/4 Release y 4/4 ASan; API 4/4 y nuevo loopback de reconexion 1/1 (cuatro casos).
- [x] Compilacion ARM XDP aislada, ELF ARM hard-float y mismas dependencias compartidas que la entrega anterior. No se ha cambiado RTL/RBF ni desplegado en MiSTer.
- [ ] Instalar ambos binarios con VLC detenido y repetir una vez el cold reset mientras reproduce; entrar de nuevo en Groovy sin reabrir el video. Comprobar imagen, audio y acceso al OSD.
- [ ] Si la primera prueba funciona, repetir en raw/LZ4 y comprobar pausa/saltos/cambios de modo despues de recuperar.
- [ ] Cerrar VLC mientras el receptor esta ausente y comprobar que no queda emisor; regresar despues al core e iniciar otra reproduccion.

Limites: durante el primer segundo sin avance todavia puede salir media hasta detectarse la perdida. No se pausa VLC: al reconectar se utiliza la posicion actual, sin reproducir el atraso. Si VLC permanece pausado al perder conexion, se descarta la ultima imagen local y puede necesitar reanudar para recibir una nueva. El audio descartado debe precargarse de nuevo; no se garantiza reanudacion instantanea. Los timeouts no pueden recuperar una llamada de driver bloqueada internamente. Sin log fisico no se considera demostrado que el cuelgue quede eliminado.

Entrega: dist/groovy-cold-reset-20260928.zip con windows/libgroovy_mister64_plugin.dll y mister/MiSTer_groovy_XDP. RBF y ajustes de red sin cambios. Guardar ambos binarios anteriores para rollback. DLL SHA-256 `B896BE4E32FEB2D51FD20715F11D4185FC3FDACDD0AA03970E7FC233DEB811AF`; XDP SHA-256 `6261C35AF23B3A5514078C436B04E2823CC1BC86AFC1E8F3191B3B40C37DB153`.

### Cold reset frente a apagado/encendido: limpieza de XDP (2026-09-28)

- [x] Resultado fisico comunicado: apagar y encender MiSTer durante reproduccion recupera el stream con la entrega anterior. El cold reset con vuelta al inicio sigue fallando. NO dar por validada la correccion de cold reset anterior.

Evidencia en fuente: la seleccion de core de menu.cpp llamaba a groovy_stop, pero user_io.cpp (coldreset_req) y otras rutas de vuelta a menu.rbf llamaban directamente a fpga_load_rbf. Esta carga termina en app_restart/execl, sin reiniciar Linux. La fuente libxdp incluida crea los sockets AF_XDP sin SOCK_CLOEXEC; el receptor tampoco los marcaba. Ademas no retenia/cerraba el objeto BPF y dejaba recursos de intentos de arranque fallidos. Esto explica una diferencia verificable entre cambio de core/exec y arranque desde cero; no demuestra por si solo el bloqueo exacto del driver observado.

- [x] Invocar y comprobar groovy_stop en fpga_load_rbf, reboot y app_restart, antes de resetear/reprogramar FPGA o hacer exec. Ahora la ruta comun cubre tambien atajos y retorno a menu.rbf.
- [x] Cierre idempotente: una segunda llamada sin recursos no toca el core siguiente. Devolver fallo e impedir continuar la sustitucion si no se puede liberar XDP/UMEM con seguridad.
- [x] Retener objeto BPF, fd de programa e interfaz propios. Desenganchar solo el programa esperado; retirar la entrada XSK antes de destruir socket/UMEM y memoria. Cerrar objeto BPF y poner handles a cero/-1.
- [x] No desmapear memoria si UMEM sigue referenciada; conservar estado para reintento. No volver a sondear un XSK parcialmente liberado.
- [x] Liberar estructuras/sockets en fallos parciales de inicializacion, y cerrar tambien sockets de inputs/GMC. UDP usa SOCK_CLOEXEC y consulta F_GETFL para flags de no bloqueo.
- [x] Marcar los descriptores AF_XDP y UMEM FD_CLOEXEC como segunda defensa frente a herencia por exec.
- [x] El socket temporal para ioctl de MTU tiene propiedad local, cierre garantizado y no pisa el descriptor del servidor. Preservar flags de interfaz al cambiar MTU.
- [x] Actualizar el build aislado XDP para copiar fpga_io.cpp modificado desde Main_MiSTer en cada build; fallar si falta el hook. Evita usar la copia persistente antigua del fichero base.
- [x] Compilar receptor ARM XDP con la ruta comun modificada. SHA-256 de fpga_io.cpp de origen y copia aislada coinciden: `4523D9CF224F18EE5DF53E87A1F888EEDA4CA56B35257E6A4823D158C3A27942`.
- [x] CTest API/seguridad 6/6: incluye orden de teardown con 10.000 cierres, fallos parciales y reintentos, y contrato de fuente que comprueba los tres hooks antes de operaciones FPGA/exec.
- [x] Helper de teardown con ASan+UBSan Linux; helper FD_CLOEXEC con fork/exec real de un proceso de prueba. No se ejecutan el kernel XDP ni los callbacks de hardware.
- [x] ELF ARM hard-float con las mismas dependencias compartidas anteriores. DLL de VLC sin cambios: conservar B896BE4E... del paquete cold-reset anterior.
- [ ] Instalar solo el nuevo MiSTer_groovy_XDP con stream detenido; hacer UN apagado/encendido completo inicial para eliminar posibles recursos heredados del binario anterior.
- [ ] Iniciar Groovy/video; repetir una vez el cold reset problemático y volver a Groovy sin detener VLC. Verificar imagen, audio y acceso al OSD.
- [ ] Si funciona, repetir tres veces y probar tambien salir a otro core/menu y regresar, seguido de pausa/saltos/cambio de modo.
- [ ] Si falla, detener el stream y conservar /tmp/groovy.log y /tmp/groovy.log.1 antes de reiniciar. Buscar [Groovy STOP] y [XDP][STOP], ademas del log VLC de reconexion; anotar si el OSD y la administracion responden.

Entrega: dist/groovy-cold-reset-cleanup-20260928.zip, SOLO receptor y notas. Mantener la DLL anterior y el RBF. XDP SHA-256 `F090F3A01340F0006263F0CF5C52F10681E69A373ECF10854C82B76ED76761A8`. No hay despliegue remoto ni pruebas de VLC local. La prueba fisica sigue siendo necesaria; una comprobacion de fuente y un orden de liberacion simulado no prueban el comportamiento del driver Ethernet real.

### Confirmacion de cold reset y coherencia de los tres receptores (2026-09-29)

- [x] El usuario confirma que la correccion de cleanup funciona perfectamente tras la prueba de cold reset. Esta confirmacion actualiza el estado pendiente de la tanda anterior; no presupone tres repeticiones ni todos los casos ampliados.
- [x] Respaldar los tres ejecutables anteriores en dist/groovy-receivers-before-20260929. Conservar tambien el ZIP de cleanup XDP validado fisicamente.
- [x] Construir standard, XDP y Wi-Fi desde el mismo overlay completo de Groovy/protocolo y la misma base Main_MiSTer modificada, incluidos los tres hooks de cierre antes de FPGA/exec.
- [x] Centralizar perfiles y rechazar la combinacion simultanea de XDP y Wi-Fi. Identificador comun coherent-20260929 y nombre de perfil dentro de cada ejecutable.
- [x] Mantener diferencias: UDP normal con buffer solicitado de 2 MiB/TOS 1; XDP solo eth0 con sus recursos/limpieza especificos; Wi-Fi con buffer solicitado de 8 MiB/TOS 0xB8 y busy-poll opcional.
- [x] Wi-Fi no requiere ni reconfigura eth0, su MTU o su afinidad IRQ. UDP normal admite la interfaz inalambrica sin exigir Ethernet; XDP sigue exigiendo Ethernet.
- [x] Igualar el limite de trabajo a cuatro lotes de 64 paquetes: UDP deja de procesar solo cuatro datagramas por poll, conservando no-bloqueo y presupuesto acotado. No afirmar mejora de rendimiento sin medir en hardware.
- [x] Compartir validacion/staging, legacy/v2, limites y timeouts, invalidacion de sesion, cierre idempotente, FD_CLOEXEC y logs acotados; no duplicar las correcciones en tres fuentes divergentes.
- [x] Unificar scripts build-standard/build-xdp/build-wifi/build-all con arboles de objetos separados, copia fresca de fuentes y comprobacion previa de hooks. Sin despliegue remoto.
- [x] Reconstruccion ARM completa de los tres desde directorios nuevos, sin mezclar objetos ni flags de variantes. Verificar fuentes, ELF hard-float, marcadores y dependencias de cada artefacto.
- [x] API/perfiles CTest 9/9; plugin 4/4 Release y 4/4 ASan; loopback de reconexion 1/1 (cuatro casos). No se ejecuta VLC local ni HPS/FPGA.
- [x] Conservar DLL VLC B896BE4E... y RBF actuales: las mejoras de aspecto, seleccion de modo, cola, pausa/audio y reconexion del emisor siguen en esa DLL, no se trasladan al receptor.
- [ ] Probar fisicamente cada nuevo ejecutable: inicio normal/tardio, pausa, saltos, cambio de modo/aspecto, cold reset y apagado/encendido durante el stream.
- [ ] Comprobar acceso al OSD y continuidad sostenida de audio/video con raw/LZ4; registrar tiempos antes de valorar mejoras de rendimiento.
- [ ] Wi-Fi: repetir sin Ethernet conectado y ajustar el caudal a la red real. No extrapolar la prueba de cable a la red inalambrica.

Entrega conjunta: dist/groovy-receivers-20260929.zip, con los tres nombres historicos exactos, instrucciones y SHA-256. Seleccionar solo un main= en MiSTer.ini. Detener el stream y respaldar antes de sustituir; hacer un apagado/encendido inicial. La confirmacion fisica del XDP anterior no sustituye la comprobacion de esta reconstruccion conjunta. Detalle de perfiles y compilacion en ../Groovy_MiSTer/hps_linux/VARIANTES.md.

### Video independiente del audio (2026-09-29)

- [x] Confirmacion posterior del usuario: los tres receptores funcionan correctamente. Se conservan sin cambios en esta tanda. No inferir mediciones de rendimiento ni todos los casos ampliados de esa confirmacion general.
- [x] Eliminar la condicion timer_audio_init del arranque de video y los contadores globales pre-audio leidos desde ambos hilos.
- [x] Publicar el formato de audio bajo su mutex desde la apertura del filtro, sin esperar al primer bloque. Fallos de reserva no publican un formato disponible.
- [x] Si no hay filtro, negociar 48000 Hz/estereo y no enviar paquetes de audio. No se fabrican muestras ni se cambia el protocolo; permite incorporar audio tardio a 48 kHz sin reiniciar video.
- [x] Si aparece otra frecuencia compatible, detener media, invalidar pendientes/generaciones, vaciar audio y negociar INIT desde el unico trabajador propietario de la API. Reaplicar MODE antes de aceptar media; conservar reintentos/cancelacion existentes.
- [x] Reconsultar el formato en cada intento de INIT y actualizar el negociado solo tras exito. Comprobar de nuevo bajo el mutex de audio antes de consumir/enviar muestras; no enviar un buffer de una frecuencia con la sesion anterior.
- [x] Cerrar el filtro de audio no cierra el emisor ni obliga a cambiar de sesion. Reabrir a la misma frecuencia no renegocia; reabrir con otra frecuencia si lo hace.
- [x] Rechazar filtro solapado: la captura de audio sigue siendo global. Mantener orden de retirada de callbacks antes de liberar estado; limpiar referencia prestada incluso tras fallo de reserva.
- [x] Retirar soporte declarado incorrectamente de FL64/PCM entero: el conversor actual solo lee float32 nativo. Rechazar frecuencias que CMD_INIT no representa; admitidas 22050/44100/48000 Hz con salida estereo. No se implementa remuestreo ni nuevo downmix en esta tanda.
- [x] Tests de politica: ausencia de audio, llegada a 48 kHz, todas las transiciones entre frecuencias admitidas, rechazo de formatos, handshake no confirmado, 10.000 aperturas/cierres simulados y generaciones/modo.
- [x] Prueba con API real sobre UDP local: seis frames exactos y cuatro bloques de audio; solo cuatro INIT/MODE, con frecuencias 48/44.1/22.05/48 kHz. Cuatro combinaciones legacy/v2 y raw/LZ4. No simula callbacks reales VLC ni el receptor HPS/FPGA.
- [x] DLL Release x64 compilada; CTest plugin 5/5 Release y 5/5 ASan; reconexion/audio UDP 2/2 (ocho casos de red en total).
- [ ] Prueba fisica: archivo sin pista de audio y archivo con audio deshabilitado; deben dar imagen desde el inicio.
- [ ] Habilitar audio a 48 kHz durante el video; comprobar imagen continua y sonido. Alternar habilitar/deshabilitar varias veces.
- [ ] Cambiar entre pistas a 48/44.1/22.05 kHz cuando esten disponibles: comprobar renegociacion, recuperacion de imagen y tono/velocidad correctos. Puede haber breve corte por INIT/MODE, no se promete transicion sin interrupcion.
- [ ] Regresion: pausa/reanudacion, saltos, cambios de modo/aspecto, cold reset, cierre durante reintentos y reapertura.

Entrega: dist/groovy-video-only-20260929.zip, SOLO DLL, instrucciones y SHA-256. Mantener los tres receptores y RBF validados. Backup de la DLL previa B896BE4E... en dist/groovy-before-video-only-20260929. DLL nueva: `3A9798A1EA6718DD775A4002497C5E273C0516FA905B37265437EC14DF0D6FB2`.

Limites: si no hay nuevas imagenes tras renegociar estando pausado, puede ser necesario reanudar. El filtro rechaza formatos no implementados en vez de interpretarlos como float; en ese caso el video sigue, pero el audio de MiSTer no se garantiza. El remuestreo, mapas de canales y revision completa del ciclo de vida multipista siguen pendientes. El trabajador revisa cambios de audio tambien durante espera sin frames, con sondeo acotado a 50 ms. No se ha abierto VLC local ni conectado a MiSTer.

### Confirmacion de video sin audio y metricas del stream (2026-09-29)

- [x] El usuario confirma que funciona la DLL video-only. Conservarla como base fisicamente comprobada; no asumir todos los cambios de frecuencia/casos ampliados sin detalle de pruebas.
- [x] Anillo fijo de 256 envios para vincular frameEcho con numero de envio, secuencia de imagen y campo. Sin reservas por frame ni cambios en protocolo/receptores.
- [x] Leer solo el estado ya consumido por la API antes del envio/despues de WaitSync. No añadir lecturas de socket ni sustraer ACKs a la correccion de raster.
- [x] Contabilizar solo coincidencias exactas: duplicados/estados cacheados no aumentan confirmaciones. Un salto de echo deja anteriores como no observados, NO como perdidos ni confirmados acumulativamente.
- [x] Separar correlaciones sobrescritas por limite, pendientes y abandonadas al cambiar sesion. Conservar acumulados tras reconectar o renegociar audio, sin reutilizar la correlacion antigua.
- [x] Distinguir ACKs observados de cuadros progresivos/campos entrelazados. No cambiar el algoritmo de eleccion de campo ni declarar validado su orden fisico.
- [x] Medir desde inicio del envio hasta observacion del ACK: incluye envio, sondeo y planificacion; NO es RTT de red puro ni latencia CRT.
- [x] Dos lineas de debug cada 30 s y al cerrar: contadores, ocupacion/pico de cola, descartes, reconexiones y medias/maximos de conversion, envio, WaitSync, edad y observacion ACK. Acumulados de toda la reproduccion, no medias por ventana.
- [x] Copiar el resumen bajo mutex y escribir estas lineas fuera del bloqueo de la cola; evitar rafagas de logs tras paradas largas.
- [x] Tests de coincidencia, ACK desconocido/duplicado/reordenado, wrap, nueva sesion, saturacion de 256 registros, reloj atrasado, 100.000 envios y limite exacto de 30 segundos.
- [x] DLL Release x64 y tests plugin 6/6 Release y 6/6 ASan. API 9/9 y reconexion/audio UDP 2/2; el cliente de sesiones comprueba ademas seis coincidencias ACK y rechazo de duplicados en legacy/v2, raw/LZ4.
- [ ] Recoger 60-90 segundos de log VLC con detalle 2 en el PC de reproduccion. Mantener mismo archivo, modo, compresion y receptor al comparar.
- [ ] Comprobar que no aparecen nuevos cortes al emitirse los resumenes; probar pausa/salto/cambio de modo y cierre.
- [ ] Usar medidas de WaitSync/envio/conversion y CPU del equipo real para decidir el siguiente ajuste. No modificar espera activa ni mover conversion a prepare sin evidencia.

Entrega: dist/groovy-stream-metrics-20260929.zip, solo DLL y notas. DLL SHA-256 `452B86AFEB3ABA5B6F160EF6E3A1B4FE02DFB391732AFF94EEE1B1EC6B070912`. Backup de la anterior 3A9798A1... en dist/groovy-before-stream-metrics-20260929. API, receptores, RBF, audio y algoritmo de sincronizacion sin cambios. Las pruebas locales no ejecutan VLC ni FPGA; no se declara mejora de rendimiento.

### Copia directa de audio en el hilo de video (2026-09-29)

El usuario pide continuar; no se han recibido logs de la tanda de metricas. Se
mantiene pendiente cualquier ajuste de espera activa y se retira un temporal
verificablemente innecesario del hilo consumidor compartido de audio/video.

- [x] Eliminar GetAudioForFrame: reservaba un bloque, copiaba muestra por muestra con modulo y se volvia a copiar a la API antes de liberarlo.
- [x] Sustituirlo por helper sin reservas que copia del anillo al buffer API en uno o dos tramos por bloque. No reservar nuevo scratch en Release ni transferir propiedad del buffer.
- [x] Mantener bloques de hasta 16384 muestras PCM16 (32768 bytes), con pares estereo completos; no confundir bloque CMD_AUDIO con datagrama UDP.
- [x] Mantener seleccion por Timeline, formato negociado, campo 0, pausa y disponibilidad de audio/API antes de consumir datos.
- [x] Actualizar cursor, muestras disponibles y PTS solo tras la copia/envio solicitado. Redondear la duracion una sola vez para toda la lectura, igual que antes, no una vez por bloque.
- [x] Conservar el mutex durante copia/envio sin cambios de orden; la API consume el bloque sincronicamente. Un envio void sigue sin confirmar recepcion fisica.
- [x] Rechazar punteros/capacidades/indices invalidos, cantidades superiores a disponibles, pares estereo incompletos y destino insuficiente.
- [x] Tests exhaustivos en anillos pequeños, cruces de final, limite de bloque y tres segundos completos; comparar bytes, cantidad/orden de bloques y guardas de memoria con el recorrido anterior.
- [x] Verificar redondeo a 22050/44100/48000 Hz, supresion de PTS repetido, pausa/flush y 10.000 lecturas repetidas.
- [x] CTest plugin 7/7 Release y 7/7 ASan; API 9/9; red 2/2. La API real recibe cuatro lecturas circulares en ocho bloques (32768+5632 bytes por lectura), con legacy/v2 y raw/LZ4; contenido exacto comprobado.
- [ ] Regresion fisica: reproduccion continua, pausa/reanudacion y saltos, sin huecos ni chasquidos; probar audio tardio y cambio de pista.
- [ ] Comprobar 44.1/48 kHz y comparar logs/CPU con el mismo archivo y modo. No declarar mejora de rendimiento sin medir.

Alcance: se elimina una reserva/liberacion y una copia completa por lectura de
audio en el hilo de envio. La conversion de entrada en el hilo productor aun
reserva su temporal; no se modifica el downmix ni la negociacion PCM. Las
metricas de la tanda anterior se conservan. No hay cambios en API, receptores,
RBF, WaitSync, reloj de audio o algoritmo de campos.

Entrega: dist/groovy-audio-direct-20260929.zip, solo DLL y notas. DLL SHA-256
`C78911EFCFAEEB1A63D5617B0EE8B45232FCEC1911CE3E68705BC21652F49BBC`.
Backup inmediato en dist/groovy-before-audio-direct-20260929 (DLL de metricas
452B86AF...). Se conserva tambien video-only 3A9798A1..., validada fisicamente.

Cierre de entrega el 2026-09-30: se conserva la denominacion audio-direct-20260929.
Repetidas las suites Release/ASan antes de empaquetar; DLL y receptores sin
cambios adicionales. La prueba fisica de esta optimizacion sigue pendiente.

### Conversion/escalado exactos tras medir los dos clips (2026-09-30)

- [x] Analizar los logs reales: 1551/2343 imagenes aceptadas, salida 480i cercana a 60 Hz, sin descartes ni reconexiones. Las repeticiones de campos son coherentes con 24p; ACK no observado no se marca como perdida.
- [x] Priorizar conversion de 14.517/15.738 ms medios; no modificar audio que el usuario comunica correcto ni atribuirle fallos por avisos de WASAPI.
- [x] Congelar conversor anterior en tests/reference_frame_20260929.h; benchmark antes de modificar produccion para verificar paridad inicial aproximada de 1.00x.
- [x] Precalcular coordenadas horizontales por bloques de 256 y verticales por fila/bloque. Scratch de columnas 4 KiB, sin heap ni estado compartido, manteniendo anchos validos arbitrarios.
- [x] Calcular medias de cajas hasta 8x8 con reciproco entero y correccion del resto: mismo resultado exacto que division, no aproximacion. Mantener fallback general para cajas mayores.
- [x] RGB en 32 bits con limites suficientes para matrices admitidas y mismo redondeo/saturacion. Mantener coeficientes, filtro, bandas, SAR, crop, pitch y BGR.
- [x] Comparar byte a byte 1200 casos aleatorios y casos SD/720p/1034p/1080p/4K/vertical, matrices/rangos, filtros, aspecto, padding, crop impar y limites de bloque; dos conversiones concurrentes con fuentes separadas.
- [x] Comprobar exhaustivamente sumas de 1..64 bytes y redondeo RGB en 24 millones de valores.
- [x] DLL Release x64; plugin 8/8 Release y 8/8 ASan; API 9/9 y red 2/2 sin regresiones locales. No ejecutar VLC local ni conectar a MiSTer.
- [x] Dos benchmarks finales alternando referencia/nuevo: aproximadamente 17-23% menos tiempo con suavizado para los dos tamaños del usuario en ESTE equipo. No extrapolar a CPU/latencia real de reproduccion.
- [x] Identificador converter=exact-tiled-v1 en el log de arranque para reconocer la DLL instalada.
- [ ] Repetir ambos clips 60-90 s en el PC de reproduccion; guardar logs con nombres distintos para no perder la comparacion.
- [ ] Comparar conversion media/maxima, descartes, cola y CPU con mismos presets, aspecto, suavizado, compresion y receptor.
- [ ] Comprobar visualmente color/bandas y alternar aspecto, suavizado y resolucion; mantener pausa/saltos/cierre como regresion breve.

Resultados y comandos reproducibles: [BENCHMARK_CONVERSION_20260930.md](BENCHMARK_CONVERSION_20260930.md).
Entrega: dist/groovy-conversion-20260930.zip, solo DLL, notas y benchmark. SHA-256
DLL `DFCA9D36A200C6427D71B174585CE6003BFF69A2D4BC3E72F99AC5190BA6FA27`.
Anterior C78911EF... respaldada en dist/groovy-before-conversion-20260930.
Mantener receptores y RBF. La referencia solo pertenece a tests, no a la DLL.

### Validacion del conversor y correccion del mando (2026-09-30)

- [x] Recibir logs separados con converter=exact-tiled-v1: modelines2.txt (720p, ~64 s) y modelines.txt (1920x1034, ~77 s).
- [x] Comparar conversion: medias 5.647/8.067 ms; maximos 28/13 ms; reducciones observadas 61.1%/48.7% respecto de logs previos. No equivalen a CPU total ni benchmark controlado.
- [x] Registrar cero descartes, errores de envio o reconexiones y cola pico 1 en ambos logs.
- [x] Confirmacion del usuario: video y audio correctos. No extender esta validacion a casos visuales no probados ni a CPU no medida.
- [x] Posponer DX11/DXA9/OpenGL por decision del usuario.
- [x] Eliminar dependencia intf_thread_t NULL del mando y de init_class. Localizar/retener solo la playlist antecesora del vout, sin buscar objetos globales.
- [x] Ejecutar ordenes VLC en hilo de control separado; cola fija de 16 ordenes, caducidad 250 ms, sin llamadas VLC de control en el hilo de envio.
- [x] Sondear entradas tambien sin nueva imagen. Mantener todas las llamadas a la API Groovy en el hilo de video.
- [x] Pulsaciones de transporte por flanco; saltos +/-1 s con repeticion inicial 400 ms y periodo 150 ms, sin rafagas para recuperar tiempo.
- [x] B1 solo avanza; mantener compatibilidad con B10+B1. Combinar los mapas digitales de mandos 1 y 2; ignorar combinaciones ambiguas.
- [x] Proteger input inexistente, duracion desconocida/corta, limites de capitulo y seek no permitido; liberar cada referencia al input.
- [x] Limpiar cola/counters al reconectar, resetear mapas/counters de API al volver a suscribirse para admitir frames bajos tras cold reset.
- [x] Corregir memset del lector de entradas sin API: inicializar estructura del llamador, no la direccion del puntero local. Admitir argumento NULL.
- [x] Logs separados para controlador listo, suscripcion solicitada, primer paquete real, acciones y resumen al cerrar.
- [x] Tests 10/10 Release y 10/10 ASan; se compila joypad.cpp real contra dobles VLC para comprobar acciones, hilo, referencias y cierre. No ejecuta VLC instalado.
- [x] UDP localhost 3/3, incluido wrapper/API reales para paquetes digitales/analogicos y reinicio 100000 -> 1; API 9/9.
- [x] Corregir carrera del simulador audio: esperaba cualquier CLOSE, incluidos los de INIT intermedios, y podia terminar antes del ultimo bloque. Ahora espera CLOSE final tras todo el PCM; no se modifica audio de produccion.
- [ ] Prueba fisica: Joysticks=Digital/Analog en core, pausa/reanudar tras varios segundos, ambas direcciones, capitulos/secciones, anterior/siguiente/parar.
- [ ] Comprobar segundo mando, cierre/reapertura y cold reset durante reproduccion, con continuidad de audio/video.
- [ ] Guardar log detalle 2 con controller=playlist-v1, primer paquete, acciones y resumen. Si no hay paquetes, revisar configuracion del core y trayecto UDP 32101 antes de cambiar video/audio.

Entrega: dist/groovy-joypad-20260930.zip. DLL SHA-256
`C47115CD38AC5E17823CAD8C05807B5A0F03981BE8DA6A9EFE0870BE2B9EB3D2`.
Backup de la DLL de conversion validada: dist/groovy-before-joypad-20260930,
SHA-256 `DFCA9D36A200C6427D71B174585CE6003BFF69A2D4BC3E72F99AC5190BA6FA27`.
Solo instalar DLL; mantener los tres receptores y RBF. El mando pertenece al
ciclo de vida del vout: no es un control remoto persistente tras destruirse la
salida al parar. No se añade soporte a ejes analogicos ni a LibVLC embebido sin playlist.

### Revision del ciclo de vida solicitada (2026-09-30)

- [x] Confirmacion del usuario: el punto 1 (prueba fisica del mando) ha ido bien. No inferir que haya probado individualmente todos los escenarios del checklist ampliado.
- [x] Revisar propietarios, estados globales, mutexes, callbacks, hilos y orden de cierre del vout, filtro de audio, mando y API.
- [x] Revisar apertura/cierre y cambio de pista/archivo a nivel de codigo; contrastar reutilizacion de vout y destruccion de filtros con fuentes de VLC 3.
- [x] Reejecutar suites existentes: 10/10 Release, 10/10 ASan. No cubren los dos escenarios nuevos descritos debajo.
- [x] Mantener DLL validada C47115CD...; no compilar/distribuir otra ni modificar produccion por una solicitud de revision.
- [x] Retirar del trabajo activo los puntos 3 y 4 por decision del usuario: no pruebas prolongadas/orden de campos, no mediciones ni cambios en WaitSync.

Hallazgos de revision (no reproducidos en VLC fisico):

1. **Alta, condicionada a doble apertura de la interfaz auxiliar**: module.cpp
   mantiene config_sync_timer/config_sync_timer_active/last_synced_modeline
   globales. Una segunda OpenInterface sobrescribe el unico handle. El cierre
   destruye el segundo temporizador, deja el primero activo y pierde su handle;
   este conserva como dato el objeto de la primera interfaz, que puede haberse
   liberado. Riesgo de callback sobre memoria liberada y, entre callbacks,
   acceso concurrente a last_synced_modeline. Video/audio tienen guardas de
   instancia, pero esta interfaz no. Correccion propuesta: estado/temporizador
   en p_sys de cada interfaz, destruido antes de liberar su objeto, o rechazo
   explicito y seguro de duplicados. No requiere cambiar streaming.

2. **Media, cambio de archivo o cierre con una orden pendiente**: Command solo
   contiene accion y fecha. dispatch retira la orden y execute obtiene despues
   playlist_CurrentInput, por lo que un seek/pausa recibido en A puede aplicarse
   a B si cambia la reproduccion dentro de los 250 ms. Limpiar la cola en init/
   close no invalida una orden ya extraida; ademas capture::close espera primero
   al emisor y solo despues detiene el controlador. Correccion propuesta: asociar
   las ordenes a identidad/generacion del input, invalidar pendientes al cambiar
   de input y cerrar la admision de ordenes al comenzar el cierre. Mantener las
   acciones fuera del hilo de video y referencias liberadas fuera de mutexes.

Protecciones verificadas por inspeccion:

- capture::close senala parada y despierta las esperas; hace join del emisor
  antes de liberar buffers/API. initializeUntilStopped no publica exito tardio
  tras cancelacion. Una llamada de red ya en curso termina por su timeout, no
  se cancela instantaneamente.
- CloseFilterAudio retira callback antes de tomar audioMutex/liberar estado;
  productor, consumidor y cierre serializan el buffer de audio con ese mutex.
- El mando libera las referencias temporales al input y termina su hilo antes
  de soltar playlist/objeto. Esto evita fugas, pero no resuelve la identidad de
  la orden al cambiar de archivo.
- La conversion en curso conserva su slot ante cambios de generacion. No se
  ha encontrado evidencia de un nuevo acceso a buffers liberados en el cierre
  normal serializado por VLC. No se declara ausencia absoluta de carreras.

Fuentes externas contrastadas: [interfaces VLC 3](https://github.com/videolan/vlc-3.0/blob/master/src/interface/interface.c),
[ciclo del audio](https://github.com/videolan/vlc-3.0/blob/master/src/audio_output/dec.c)
y [ciclo del vout](https://github.com/videolan/vlc-3.0/blob/master/src/video_output/video_output.c).
Estas fuentes confirman el contexto de VLC; los hallazgos proceden del codigo
local del plugin. No se ha ejecutado VLC instalado ni contactado con MiSTer.

Correcciones propuestas en la revision y posteriormente encargadas:

- [x] Aislar el estado de la interfaz auxiliar y probar dos aperturas/cierres,
      fallo de creacion del timer y callback en vuelo durante cierre.
- [x] Vincular ordenes del mando a la reproduccion y probar A->B entre
      encolado/despacho, reset con orden extraida y cierre durante reconexion.

### Correcciones de ciclo de vida (2026-09-30)

- [x] Mover handle y ultimo preset de la interfaz auxiliar a intf->p_sys.
      InterfaceTimer no es copiable, no permite sobrescribir un handle activo,
      limpia creacion fallida y destruye/espera el timer antes de liberar sys.
- [x] Retener el input de origen al encolar cada accion del mando. Solo se
      consulta la playlist cuando hay accion, no en todos los campos de video.
- [x] Validar input y solicitar acciones de transporte bajo el mismo bloqueo
      de playlist. Los setters de seek/capitulo usan exclusivamente el input
      retenido y se ejecutan fuera de ese bloqueo, incluso si cambia el input.
- [x] Añadir generacion de sesion a las ordenes. Invalidate/stop vacian la cola
      y rechazan las ya extraidas pero no admitidas. Una accion ya admitida
      termina antes de retornar invalidate/stop; no se intenta deshacerla.
- [x] Cerrar admision del mando al entrar en capture::close, antes del join
      del emisor o de esperar el handshake. Un init tardio no reabre admision.
- [x] Invalidar al comenzar reconexion/renegociacion, no solo tras INIT exitoso.
- [x] Liberar referencias fuera de los mutexes de cola/ejecucion/playlist;
      cubrir descarte por identidad, caducidad, saturacion, reset y cierre.
- [x] Conservar mapeo/repeticion del mando, conversor exacto, audio y WaitSync.
- [x] Identificador de log controller=playlist-v2; no hay nuevas opciones.
- [x] Tests 11/11 Release y 11/11 ASan; repetir 20 veces los tests de mando y
      timer. API 9/9 y UDP localhost 3/3. Build DLL Release x64 y diff-check.
- [ ] Regresion fisica breve opcional: playlist A->B usando mando, pausa/saltos,
      cierre durante ausencia de MiSTer y reapertura. No requiere pruebas
      prolongadas ni medicion de CPU/WaitSync.

Pruebas nuevas: temporizadores independientes, fallo de creacion, apertura
duplicada del mismo owner, destructor, callback en vuelo durante cierre;
orden A extraida de cola antes de cambiar a B (seek/pausa/next/stop), cambio
despues de comprobar identidad (setter sigue dirigido a A), reset/cierre con
orden extraida y otra pendiente, caducidad, cola llena y referencias balanceadas.
Se usa codigo real contra dobles de las APIs VLC, no VLC instalado/CRT.

Entrega: dist/groovy-lifecycle-20260930.zip. DLL SHA-256
`90DD043307018F2B5409ECAB898EDFE7B2BAED9A291B240D2C5547DC79BB371E`.
Backup de la DLL joypad fisicamente validada en dist/groovy-before-lifecycle-20260930:
`C47115CD38AC5E17823CAD8C05807B5A0F03981BE8DA6A9EFE0870BE2B9EB3D2`.
Solo DLL; mantener receptores/RBF. Puntos 3 y 4 descartados por el usuario.

## Unificación del repositorio — 2026-10-01

- [x] Integrar Groovy_MiSTer (API, protocolo, receptores, pruebas y RTL).
- [x] Integrar la base Main_MiSTer modificada, incluidos hooks de cold reset.
- [x] Conservar los tres perfiles y el wrapper más reciente del mando.
- [x] Retirar rutas externas del build y compilar LZ4/API desde las fuentes locales.
- [x] Añadir README, procedencia, licencias conservadas y exportador de fuentes.
- [x] Compilar DLL/API y pasar 23/23 pruebas también desde un export limpio.
- [x] Compilar los tres receptores desde un export limpio y verificar identidad
      binaria, perfiles, hooks y dependencias respecto del build integrado.
- [x] Conservar originales, paquetes anteriores y RBF; sin despliegue ni VLC local.
- [ ] Revisión de licencias/dependencias heredadas antes de publicación pública.
- [ ] Commit/subida a GitHub, no realizados en esta tarea.

Detalles y límites: [UNIFICACION.md](UNIFICACION.md). La importación del RTL no
equivale a validar ni entregar un RBF nuevo. Los puntos 3 y 4 siguen descartados.
