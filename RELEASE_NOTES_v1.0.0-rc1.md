# VLC → Groovy MiSTer v1.0.0-rc1

Primera release unificada: salida de vídeo y filtro de audio para VLC 3 en
Windows **x64 y x86**, junto a los receptores **UDP, XDP y Wi-Fi** para MiSTer.

## Descargas

- `windows-x64.zip`: DLL para VLC de 64 bits.
- `windows-x86.zip`: DLL para VLC de 32 bits, aunque Windows sea de 64 bits.
- `receivers.zip`: los tres receptores; seleccionar solo uno en `MiSTer.ini`.
- `sources.zip`: fuentes del mismo commit, SDK, bibliotecas y fuentes de terceros.
- `INSTALACION_Y_USO.md`: instrucciones completas, también dentro de los ZIP.
- `SHA256SUMS.txt`: hashes de los paquetes y la guía.

Todos los ZIP llevan el prefijo `vlc-groovy-mister-v1.0.0-rc1-`.

## Instalación rápida

1. Cerrar VLC y respaldar la DLL y la configuración actuales.
2. Copiar la DLL de la arquitectura de **VLC** a `plugins/video_output/`.
   No mantener copias antiguas en otras carpetas de plugins. Instalar el runtime
   Microsoft Visual C++ v14 correspondiente si falta.
3. Con el stream detenido, copiar los receptores a `/media/fat/` y seleccionar
   en la sección `[Groovy]` de `MiSTer.ini` uno de estos valores:

   ```ini
   [Groovy]
   main=MiSTer_groovy
   ```

   Alternativas: `MiSTer_groovy_XDP` o `MiSTer_groovy_wifi`. Reiniciar MiSTer
   normalmente y entrar en el core Groovy. No reemplazar el ejecutable `MiSTer`
   principal ni el RBF ya validado.
4. VLC: activar salida de vídeo **Groovy Mister** y filtro de audio **Groovy
   Mister**, indicar la IP de stream y desactivar la decodificación por hardware.
5. Empezar con Automatic, MTU 1500 y **15 kHz only (Automatic)** si la pantalla
   es de 15 kHz. Aspect Ratio conserva la geometría para pantalla física 4:3.

La guía adjunta incluye rutas exactas x64/x86, ejemplo PowerShell, mando,
diagnóstico, logs y rollback. **XDP requiere una instalación preparada** con
kernel/driver AF_XDP, programa BPF y libelf: no se incluyen actualizaciones de
kernel, rootfs ni RBF. Si no se dispone de ese entorno, utilizar UDP normal.

## Cambios reunidos

- Cola de vídeo y control de imágenes atrasadas, conversión I420/J420 optimizada.
- Proporción para pantalla 4:3, selección automática por FPS y cambios de modo
  durante reproducción, incluido el límite automático a 15 kHz.
- Recuperación de sesiones, inicio tardío del receptor, pausa/reanudación y
  correcciones de limpieza tras cold reset compartidas por los tres receptores.
- Control desde mandos MiSTer y protecciones de cierre/cambio de archivo.
- Proyecto autosuficiente, scripts de build y fuentes/licencias de dependencias.

## Validación y límites

- DLL Release x64 y x86 compiladas con MSVC v143 (14.42); 23/23 pruebas locales
  por arquitectura. Cabeceras PE, arquitectura, exportaciones VLC 3 y dependencias
  comprobadas sin iniciar VLC.
- Receptores reconstruidos con ARM GNU 10.2-2020.11 desde las fuentes de esta
  release; comprobados ELF ARM hard-float, perfiles, hooks y dependencias.
- No hay cambios de código de ejecución ni de RTL para preparar esta release.
  El script Windows añade selección de arquitectura para reproducir el build.
- **x86 no está probado físicamente en VLC/MiSTer.** Las confirmaciones previas
  de hardware no equivalen a una prueba física de estos cinco binarios recién
  compilados. Por ello se publica como pre-release/RC, no como validación final.
- No se ha ejecutado VLC local ni accedido a una MiSTer para esta entrega.
- D3D9/D3D11 y otras superficies GPU opacas no están admitidas; usar software.
- El límite 15 kHz no protege frente a modos manuales ni al arranque del RBF.
- La versión interna histórica del módulo sigue siendo `1.0.0-dev`; el tag
  `v1.0.0-rc1` identifica este empaquetado sin alterar metadatos del código.
- La auditoría de licencias y sus puntos pendientes de procedencia/alcance GPL
  siguen publicados y acompañan los binarios; no se presentan como resueltos.

Fuentes fijadas por el tag y disponibles como asset `sources.zip` en esta misma
release. Cada ZIP binario contiene `BUILD_MANIFEST.json` con commit, arquitectura,
tamaño y SHA-256 de sus artefactos. No se promete igualdad entre compilaciones
con distinta fecha/toolchain; el build ARM incorpora fecha.
