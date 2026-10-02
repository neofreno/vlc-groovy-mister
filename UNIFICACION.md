# Unificación de fuentes — 2026-10-01

## Orígenes

Se importó el estado de trabajo local, incluidos cambios sin commit, sin alterar
los repositorios originales:

- Groovy_MiSTer: base `109908c9ddbd4397ba7aadb193de61fdb8cb34e6`, origen
  `https://github.com/psakhis/Groovy_MiSTer.git`.
- Main_MiSTer: base `0b2e6eb2bdbcc0a73cb53e9a581b8a485c9a3292`, origen
  `https://github.com/MiSTer-devel/Main_MiSTer.git`.

Los commits por sí solos **no** representan las correcciones locales.
`Groovy_MiSTer/IMPORT_MANIFEST.json` registra origen, ruta y SHA-256 de los
2600 archivos tal como se importaron, antes de adaptar rutas y documentación.
No es un lockfile del resultado final. Los scripts/Makefiles se normalizaron a
LF; `.gitattributes` conserva ese formato en futuros clones de Windows.

## Cambios analizados y conservados

| Área | Integración |
|---|---|
| API | Negociación legacy/v2, sesiones, fragmentación, validación de ACK, límites y cierre de recursos; LZ4 desde fuentes |
| Protocolo | Cabeceras únicas de validación, reensamblado, recursos, descriptores POSIX y perfiles |
| Receptor común | Staging de transferencias completas, timeouts, límites de trabajo, limpieza y recuperación de sesión |
| UDP / XDP / Wi-Fi | Misma implementación común, distintos perfiles de buffers, Ethernet, AF_XDP y dependencias |
| Main MiSTer | Integración de Groovy en `user_io`, menús y memoria; `groovy_stop` antes de carga de core, reboot y exec en `fpga_io.cpp` |
| Mando / wrapper | Se conserva la implementación más reciente del plugin: reinicio de binding/contadores al reconectar, lectura nula segura y logs por ACK desactivados |
| FPGA | Se importan `Groovy.sv`, `Groovy.qsf`, `rtl/sound.v`, `audio_sample_tick.v` y los módulos JTFRAME locales; no se cambian ni se declara validado su resultado físico |

La base completa Main se integra directamente en `hps_linux/src/`. No se
superpone sobre ella el antiguo `user_io.cpp` o `menu.cpp` del overlay Groovy:
se conservan las versiones realmente usadas por las últimas compilaciones.
Las fuentes específicas `support/groovy` y el Makefile proceden del Groovy local.

La implementación única del wrapper vive en `Groovy_MiSTer/api/`.
Los dos archivos homónimos en `src/` son adaptadores de inclusión, no copias de
la lógica. Se conservan las correcciones más recientes de VLC, no las versiones
antiguas del wrapper importado.

## Cambios exclusivamente de integración

- Solución Visual Studio, proyecto y fixtures apuntan a rutas internas.
- Eliminadas las rutas personales de vcpkg y referencias residuales a libass
  y a bibliotecas LZ4 antiguas en las configuraciones del proyecto.
- CMake raíz agrupa las 23 pruebas existentes.
- Build ARM usa la base local sin argumento Main_MiSTer externo; mantiene
  directorios de objetos aislados y valida los hooks antes de compilar.
- Documentación, procedencia y entradas de build para un checkout nuevo.
- Resultados de compilación, configuración personal y librerías obsoletas se
  retiran del índice de Git, **no del disco**. Las bibliotecas de importación
  de VLC y las dependencias ARM sí son entradas necesarias y se conservan.
- Corregido el `.gitignore` heredado de las cabeceras UAPI: ya no oculta las
  cabeceras necesarias para XDP. Solo excluye los registros generados `.cmd`.

## Exclusiones deliberadas

No se importan `.git`, cachés Quartus, objetos, builds antiguos, ejecutables
históricos, el árbol RetroArch ajeno a este flujo, ni scripts de despliegue FTP.
Los receptores y RBF previamente probados siguen en la carpeta original y en
los paquetes de distribución existentes. Los builds nuevos no los sobrescriben.
Las herramientas BPF y sus fuentes auxiliares se conservan como referencia;
su antigua receta `kernel/sergi/Makefile` no es parte de `build-all.sh` y todavía
requiere adaptar el entorno si se quiere reconstruir el programa BPF.

## Límite importante del RTL

El `Groovy.sv` local establece 720×480 de arranque (aunque un comentario antiguo
diga «720p»). `rtl/sound.v` contiene cambios locales del acumulador de fase;
`audio_sample_tick.v` se conserva, pero no se añade al proyecto ni se conecta
automáticamente. Esta tarea no revisa ni corrige ese comportamiento. No cambiar
el RBF validado como consecuencia de la unificación.

## Checklist

- [x] Inventariar fuentes, diferencias y dependencias externas.
- [x] Importar API, protocolo, RTL, pruebas y receptores, conservando licencias.
- [x] Incorporar Main MiSTer y sus hooks de cold reset.
- [x] Unificar wrapper sin perder las correcciones recientes del mando.
- [x] Eliminar dependencias de carpetas hermanas en el build activo.
- [x] Compilar Release x64 y pasar las 23 pruebas desde CMake raíz.
- [x] Completar reconstrucción ARM y comprobar los tres artefactos.
- [x] Comprobar un export limpio sin cachés ni repositorios hermanos.
- [x] Revisar las condiciones de publicación y documentar hallazgos en
  `AUDITORIA_LICENCIAS.md` (seguimiento de 2026-10-02).
- [x] Añadir textos, atribuciones y 14 archivos fuente de terceros sin modificar
  el código activo, el RTL ni las bibliotecas de enlace.
- [ ] Cerrar las ambigüedades restantes de alcance GPL y procedencia histórica
  enumeradas en la auditoría; no confundir fuentes candidatas con correspondencia
  completa demostrada.
- [x] Publicación realizada el 2026-10-02 en
  `https://github.com/neofreno/vlc-groovy-mister`, conservando visibles los
  puntos pendientes de la auditoría. Rama `main`, importación inicial `751151d`;
  instantánea con historial nuevo, sin reescribir ni publicar el historial del
  repositorio local original.

No se reabren las pruebas físicas prolongadas ni las medidas de CPU/WaitSync
que el usuario decidió descartar. Los builds locales no equivalen a validación
en CRT/Ethernet; no se ha iniciado VLC ni accedido a MiSTer durante esta tarea.

## Resultados de verificación

- Release x64: compilación de DLL/API correcta; 23/23 pruebas del CMake raíz.
- Export limpio en una carpeta temporal de Windows: DLL/API compiladas desde
  cero; 23/23 pruebas (33,70 s). No existen repositorios hermanos en esa carpeta.
- Export limpio en `/var/tmp` de WSL: compilación desde cero de standard, XDP
  y Wi-Fi. ELF ARM hard-float, hooks, perfiles y dependencias correctos.
- Los tres receptores del export coinciden byte a byte con la reconstrucción
  del árbol integrado realizada el mismo día y con el mismo toolchain.
- Verificado el manifiesto: los cambios respecto de la importación son la
  integración descrita y normalización LF de scripts/Makefiles; no se alteraron
  las fuentes de API, protocolo, receptor, Main o RTL fuera de esos ajustes.
- 46 entradas obsoletas/generadas se retiraron del índice, permaneciendo sus
  archivos locales. No se creó ningún commit ni se configuró un remoto.

SHA-256 de los receptores reconstruidos (2026-10-01):

```text
6de3c2312aeb6606ae6cf88e343bb092f58ecc6b1d33801bf2122ca3eefb1702  MiSTer_groovy
a32dde01e1798aa58699351a0320608c076ee9860b9e9daddc92e2c2d9aa51ec  MiSTer_groovy_XDP
ada4b8bfa50c72967136cf23eb6aa3f7ac98e7b2eb8d787cedbfc9b630de341a  MiSTer_groovy_wifi
```

Estos hashes identifican los builds de comprobación, no sustituyen el registro
de los ejecutables físicamente validados. El Makefile incluye fecha de build;
no se promete igualdad binaria entre compilaciones de días/toolchains distintos.
