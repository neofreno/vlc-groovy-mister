# Fuentes y procedencia de bibliotecas incluidas

Se conservan **todos los binarios de enlace existentes**, sin sustituirlos,
recompilarlos ni modificar el código del proyecto. Los archivos de `sources/`
son distribuciones fuente originales, no versiones nuevas instaladas en el
build. El Makefile y la solución siguen usando las mismas bibliotecas.

## Identificación y nivel de evidencia

| Biblioteca | Fuentes suministradas | Evidencia y límites |
|---|---|---|
| libelf, estática y dinámica | elfutils 0.183 + Debian 0.183-1 | Ambas bibliotecas coinciden byte a byte con los paquetes Debian ARM hard-float originales; fuentes y parches verificados contra los SHA-256 del `.dsc` |
| libbpf 0.7.0 | archivo upstream v0.7.0 completo | Las 13 cabeceras instaladas coinciden con v0.7.0 normalizando CRLF; nombre del binario y versión API coinciden. No se presentan las fuentes auxiliares 1.2 como sus fuentes |
| libxdp 1.2.0 | xdp-tools v1.2.0 completo | Nombre/ABI 1.2.0; cabeceras base y ajustes locales existentes, descritos abajo; no hay reproducción binaria exacta demostrada |
| libbluetooth | BlueZ 5.54 completo | Las 15 cabeceras coinciden con 5.54 normalizando CRLF; cadenas de fabricantes coherentes. El binario identifica GCC Linaro 6.2-2016.11 y entorno Buildroot 2017.02. No se dispone del log/configuración original completo |
| Imlib2 / FreeType | Imlib2 1.4.9 / FreeType 2.7.1 | Versiones de las recetas Buildroot 2017.02, coherentes con las rutas de compilación de los binarios; identificación inferida, no igualdad binaria verificada |
| bzip2 / libpng / zlib | 1.0.6 / 1.6.28 / 1.2.11 | Versiones presentes en cadenas del binario y en Buildroot 2017.02; archivos originales con hashes de esas recetas |
| SDK VLC | VLC 3.0.16 completo | Versión declarada por las cabeceras; bibliotecas de importación conservadas, sin reconstrucción comparativa |

`SOURCES.json` identifica las descargas por URL, tamaño y SHA-256.
`BINARIES.json` identifica los binarios de enlace publicados por ruta y SHA-256.
Los hashes detectan cambios y duplicados; por sí solos no prueban autoría ni
que unas fuentes no verificadas sean las fuentes correspondientes de un binario.

## libelf: correspondencia verificada

Paquetes originales examinados:

- `https://archive.debian.org/debian/pool/main/e/elfutils/libelf1_0.183-1_armhf.deb`
- `https://archive.debian.org/debian/pool/main/e/elfutils/libelf-dev_0.183-1_armhf.deb`

SHA-256 de los archivos, idénticos en el paquete Debian y el proyecto:

```text
2ec6ea4093295ece44b33cd394db5b6062978db00640d64f515135b1406bc881  libelf.so
0f915e1e19ef2f62529feed792a9887df55e405d2dae0aba0c40fadd0e93c686  libelf.a
```

Se incluyen el tarball original, el `.debian.tar.xz` con parches/reglas y el
descriptor `.dsc`. No se ha verificado criptográficamente la firma PGP del
descriptor; sí los hashes que contiene, descargado por HTTPS del archivo Debian.
En Linux, `dpkg-source -x elfutils_0.183-1.dsc` prepara las fuentes y parches.
Las reglas de compilación originales están en ese paquete fuente.

## Recetas y ajustes ya existentes

Se incluye Buildroot 2017.02 completo, con recetas, hashes, parches y manual.
Sus paquetes `imlib2`, `freetype`, `bzip2`, `libpng` y `zlib` describen las
versiones y opciones del entorno identificado. También se conserva BlueZ 5.43,
la versión de la receta original, **pero la referencia de las cabeceras del
proyecto es BlueZ 5.54**. No se afirma que 5.43 sea la fuente del binario actual.
No se inventa una `.config` de la compilación original ni se garantiza que
reconstruir esas recetas sin ella produzca el mismo ejecutable.

Las cabeceras locales libxdp contienen adaptaciones anteriores a esta tarea:
`libxdp.h` incluye libbpf y compatibilidad `extern "C"`, `parsing_helpers.h`
incluye `bpf_endian.h`, y `xsk.h` admite ambas semánticas de inline de GCC.
Estas cabeceras exactas ya están en `Groovy_MiSTer/hps_linux/src/lib/libxdp/xdp/`.
Se preservan; no se sustituyen por las del tarball. El archivo xdp-tools incluye
también las fuentes GPL de los programas BPF, además de la parte de usuario
con alternativa BSD/LGPL. libbpf v0.7.0 se proporciona separadamente.

## Distribución y recompilación

Publicar esta carpeta completa junto a los binarios y los textos de `LICENSES/`;
no publicar solamente enlaces que puedan desaparecer. No confundir los
archivos fuente originales con las pruebas de igualdad binaria descritas arriba.
Los archivos son menores de 100 MiB cada uno; pueden versionarse sin Git LFS.

Para recompilar las bibliotecas, seguir las instrucciones incluidas en cada
distribución fuente con un toolchain ARM hard-float y sus dependencias. Para
libelf se suministran además las reglas Debian originales; para el entorno
antiguo se suministran las recetas Buildroot. No se promete una reconstrucción
bit a bit de todo el entorno histórico. La compilación del plugin/receptores
del README principal permanece sin cambios.

La revisión documental no acredita la ausencia de parches no documentados en
los binarios históricos. Si se requiere certificar la correspondencia completa
de esos binarios, habrá que obtener sus registros/fuentes exactos del proveedor
o reconstruirlos desde las fuentes declaradas y validar el resultado. En esta
tarea no se reemplazan las bibliotecas que ya funcionan.
