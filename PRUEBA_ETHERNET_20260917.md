# Prueba de estabilidad Ethernet — 17 de septiembre de 2026

Entrega empaquetada el 20 de septiembre: `groovy-stream-fix-20260920.zip`.

Paquete de prueba, no validado todavia en hardware real. Se han compilado y
probado los componentes locales; no se ha ejecutado VLC en el PC de desarrollo
ni instalado nada en MiSTer. No se incluye RBF nuevo: esta tanda modifica HPS,
API y plugin, manteniendo la interfaz FPGA existente.

## Contenido e instalacion

1. Guardar una copia de la DLL y del `MiSTer_groovy_XDP` actuales FUERA de sus
   carpetas de carga. Conservar tambien MiSTer.ini. No dejar otra DLL antigua
   del modulo dentro del arbol de plugins de VLC.
2. Cerrar VLC en el PC conectado directamente por Ethernet. Sustituir su
   `libgroovy_mister64_plugin.dll` por `windows/libgroovy_mister64_plugin.dll`
   del ZIP, en la misma ubicacion de plugins donde estaba el modulo. Es para
   VLC 3 Windows x64; no para VLC 32 bits ni VLC 4.
3. Con el stream detenido, sustituir en la SD de MiSTer el archivo seleccionado
   por `main=MiSTer_groovy_XDP` por `mister/MiSTer_groovy_XDP` del ZIP. No
   reemplazar a ciegas el ejecutable principal `MiSTer` ni cambiar MiSTer.ini.
   Reiniciar MiSTer y seleccionar Groovy para cargar el nuevo receptor.
4. No sustituir el RBF durante esta primera prueba. Guardar el nombre/version
   del RBF existente para correlacionar cualquier fallo.
5. `sdk/libgroovymister.lib` y `sdk/groovymister.h` son para desarrollo; no se
   copian a VLC. La API ya esta enlazada estaticamente dentro de la DLL.

Para volver atras: detener VLC, restaurar ambos binarios guardados y reiniciar
MiSTer. No se proporciona ningun instalador ni script que sobrescriba archivos
automaticamente. SHA256SUMS.txt identifica los binarios incluidos.

## Primera prueba controlada

- [ ] Confirmar cable directo, enlace de 1 Gbps y host de stream **192.168.2.11**
  (si sigue siendo la IP de eth0). 192.168.1.210 es administracion por Wi-Fi.
- [ ] MTU 1500; Jumbo Frames desactivado. No extrapolar la prueba UDP local de
  MTU 9000 al receptor XDP, cuyos chunks son de 4096 bytes.
- [ ] Activar log Groovy, preferiblemente nivel 1; evitar nivel 3 durante la
  primera medida de fluidez. El log actual rota a un archivo anterior acotado.
- [ ] Usar un clip conocido **con audio**, iniciar con un preset progresivo
  admitido por el CRT: 240p para 29.97/59.94 o 288p para 25/50.
- [ ] Aspect Ratio activado, CRT 4:3; Smooth Video activado por defecto.
- [ ] Probar primero sin compresion, despues con LZ4, sin cambiar otros ajustes.
- [ ] Dejar 10 minutos cada caso: imagen continua, audio estable y menu/OSD de
  MiSTer accesible. Registrar si aparecen saltos desde el inicio o al cabo de un tiempo.
- [ ] Cerrar/reabrir VLC cinco veces. La nueva sesion debe iniciar sin reiniciar
  MiSTer. Probar tambien stop/play.

## Segunda prueba, solo si la primera es estable

- [ ] Automatic con clips 25/50 y 23.976/29.97/59.94. Registrar modo elegido.
  A 23.976 sobre ~60 Hz existe cadencia 2:3; no se promete movimiento uniforme.
- [ ] Alternar Aspect Ratio y Smooth Video 20 veces: no deben cambiar los
  timings de hardware si el preset es el mismo, ni congelar VLC.
- [ ] Alternar modos seguros 240p/480i/576i y comprobar cierre. No probar
  frecuencias/resoluciones no admitidas por el CRT.
- [ ] Verificar proporcion de circulos y personas con fuentes 4:3 y 16:9,
  incluyendo anamorfismo; 16:9 debe mostrar bandas en el CRT 4:3.
- [ ] Comparar Smooth Video activado/desactivado con detalles finos y movimiento.
- [ ] Probar pausas, saltos y 30 minutos de A/V. Registrar deriva y orden de campos.

## Si falla

Detener el stream y guardar cuanto antes `/tmp/groovy.log` y, si existe,
`/tmp/groovy.log.1`, junto al log VLC de detalle 2. Si MiSTer sigue accesible,
copiarlos **antes de reiniciar**: /tmp se pierde al reiniciar. Si no responde,
anotar el tiempo hasta el cuelgue y si el OSD tambien dejo de responder.

Adjuntar preset, FPS/resolucion del clip, raw/LZ4, RBF, version VLC y si el fallo
ocurre al iniciar, al cambiar modo o durante reproduccion estable. Un mensaje
`[FPGA] timeout/failure` distingue falta de respuesta del core de una transferencia
de red incompleta. `submitted` en el resumen VLC no es confirmacion FPGA.

## Alcance de las correcciones

Receptor: propiedad RX/TX, despertares y trabajo acotado XDP, validacion de
paquetes, staging/timeout legacy, protocolo v2 en XDP y esperas FPGA limitadas.
Plugin/API: FPS sin truncado, selector de modo por cadencia, cola por fecha de
presentacion, promedio para reduccion y temporizacion API con mejor precision.

Los tests no simulan el driver de Ethernet ni el RTL. Siguen pendientes la
revision completa de discontinuidades A/V, arranque sin audio, orden de campos
y mediciones de CPU/latencia. Las mejoras visuales y la desaparicion del cuelgue
solo se podran confirmar con esta prueba fisica.
