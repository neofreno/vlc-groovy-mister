# VLC → Groovy MiSTer

Plugin de salida de vídeo para VLC 3 en Windows x64, con audio y control desde
los mandos de MiSTer. Este repositorio reúne el emisor, la API de transporte,
los tres receptores ARM y las fuentes del core FPGA.

## Organización

| Carpeta | Contenido |
|---|---|
| `src/` | Plugin VLC: vídeo, audio, modos, reconexión y mandos |
| `Groovy_MiSTer/api/` | API, wrapper C compartido y LZ4 compilado desde fuentes |
| `Groovy_MiSTer/protocol/` | Protocolo, reensamblado, límites y perfiles compartidos |
| `Groovy_MiSTer/hps_linux/src/` | Main MiSTer completo integrado con Groovy y correcciones de cold reset |
| `Groovy_MiSTer/rtl/`, `sys/` | RTL, soporte MiSTer y proyecto Quartus |
| `tests/`, `Groovy_MiSTer/tests/` | Pruebas unitarias y transporte UDP local |
| `sdk/` | Cabeceras y bibliotecas de importación de VLC incluidas en el proyecto |
| `LICENSES/`, `third_party/` | Licencias, atribuciones, fuentes originales de dependencias y manifiestos SHA-256 |

No hace falta clonar `Groovy_MiSTer` ni `Main_MiSTer` en carpetas hermanas.
No se requiere vcpkg. No hay submódulos ni descargas durante la compilación.

Verificado el 2026-10-01: DLL Release x64, 23/23 pruebas y los tres receptores
ARM, también desde exports limpios sin las carpetas originales.

## Compilar en Windows

Requisitos: Visual Studio 2022, herramientas C++ v143 y Windows SDK.
Para las pruebas: CMake ≥ 3.22 y Python 3 en PATH.

```powershell
.\scripts\build-windows.ps1 -Test
```

Resultado Release x64: `x64/Release/libgroovy_mister64_plugin.dll`.
También se puede abrir `vlc-groovy-mister.sln`. La API y LZ4 se recompilan como
dependencia de la DLL; las carpetas antiguas `groovymister/` y `lz4/` ya no se usan.
La configuración validada es **Release x64 / VLC 3**; Win32 no está validada.

Para ejecutar solamente la suite conjunta:

```powershell
cmake -S . -B build/tests -G "Visual Studio 17 2022" -A x64
cmake --build build/tests --config Release
ctest --test-dir build/tests -C Release --output-on-failure
```

Estas pruebas no abren VLC ni contactan con MiSTer: los fixtures de red usan
loopback. No ejecutar las suites UDP simultáneamente sobre los mismos puertos;
la prueba del mando utiliza 32100 y 32101.

## Compilar los tres receptores

En Linux/WSL, instalar bash, rsync, make y Python 3, y poner ARM GNU
10.2-2020.11 (`arm-none-linux-gnueabihf-gcc`) en PATH. Desde esta raíz:

```sh
bash Groovy_MiSTer/hps_linux/build-all.sh /var/tmp/groovy-receivers-build
python3 Groovy_MiSTer/tests/check_receiver_artifacts.py \
    /var/tmp/groovy-receivers-build Groovy_MiSTer/hps_linux/src
```

La carpeta de trabajo debe estar fuera de `Groovy_MiSTer` y ser exclusiva de
esta compilación. Para una reconstrucción limpia, elegir una carpeta nueva.
Los resultados, excluidos de Git, aparecen en `Groovy_MiSTer/hps_linux/`:
`MiSTer_groovy`, `MiSTer_groovy_XDP` y `MiSTer_groovy_wifi`.
Se pueden construir por separado con `build-standard.sh`, `build-xdp.sh` y
`build-wifi.sh`, pasando la misma clase de argumento.

Los scripts no despliegan ni ejecutan los receptores. Las bibliotecas ARM de
terceros incluidas son entradas de enlace, no fuentes reconstruidas por estos
scripts. XDP sigue requiriendo el kernel, programa BPF y bibliotecas adecuados
en MiSTer; no se sustituyen automáticamente.
Ver [diferencias e instalación](Groovy_MiSTer/hps_linux/VARIANTES.md).

## FPGA y validación física

Se conserva el proyecto `Groovy_MiSTer/Groovy.qpf` y el RTL local importado.
**Esta unificación no reconstruye ni valida un RBF nuevo.** Los cambios locales
del RTL no deben confundirse con el RBF que pasó las pruebas físicas. Mantener
el RBF validado; en particular, no asumir que el modo de arranque del RTL es
seguro para un CRT de 15 kHz. Ver el inventario de integración.

## Documentación

- [Inventario, procedencia y checklist de unificación](UNIFICACION.md).
- [Componentes y licencias conservadas](THIRD_PARTY_NOTICES.md).
- [Alcance de las licencias](LICENSE.md) y [estado de la revisión](AUDITORIA_LICENCIAS.md).
- [Procedencia de bibliotecas y fuentes adjuntas](third_party/README.md).
- [Guía de configuración](GUIA_CONFIGURACION_MODULO.md).
- [Guía de mejoras y pruebas físicas históricas](GUIA_MEJORAS_STREAM_VIDEO.md).
- [Detalle de pruebas](tests/README.md).

Los informes históricos conservan las rutas y los resultados de sus respectivas
tandas; los comandos de este README son los del repositorio unificado.
No se ha publicado ningún commit ni release desde los scripts de compilación.

Para exportar los archivos de trabajo que Git recogería (incluidos los nuevos,
sin hacer commit) a un ZIP nuevo:

```powershell
python scripts/export-sources.py dist/groovy-unified-sources.zip
```

El export no es una auditoría de secretos ni de licencias. Revisar el contenido
antes de publicarlo. Los informes históricos pueden contener rutas locales.
Incluye las bibliotecas precompiladas y 14 archivos de fuentes originales de
terceros (aproximadamente 49 MiB adicionales); no es un paquete exclusivamente
de código fuente ni se licencia todo como LGPL. Las identificaciones inferidas
de bibliotecas antiguas están señaladas en la documentación de procedencia.
