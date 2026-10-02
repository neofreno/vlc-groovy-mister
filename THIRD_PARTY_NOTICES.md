# Procedencia y avisos de terceros — 2026-10-02

La agrupación en un repositorio no cambia la licencia ni la autoría de los
componentes. Se conservan los textos y cabeceras existentes; no se asigna una
licencia única nueva al conjunto.

| Componente | Texto / avisos incluidos |
|---|---|
| Groovy MiSTer, API y core | `Groovy_MiSTer/LICENSE` (texto GPL v2), cabeceras y README originales |
| Main MiSTer | `Groovy_MiSTer/hps_linux/src/LICENSE` (texto GPL v3), cabeceras de cada archivo |
| LZ4 | `Groovy_MiSTer/api/lz4/LICENSE` y cabeceras |
| SDK VLC | VideoLAN y autores de las cabeceras; `sdk/NOTICE.md`, `sdk/COPYING.LIB`, fuentes VLC 3.0.16 en `third_party/sources/` |
| libbpf / xdp-tools | Textos bajo `Groovy_MiSTer/hps_linux/src/support/groovy/kernel/lib/xdp-tools/` y sus subdirectorios |
| Cabeceras Linux | Etiquetas SPDX y avisos en `hps_linux/src/support/groovy/kernel/usr/include/` |
| Bibliotecas de Main | Avisos en `hps_linux/src/lib/`, incluidos miniz, zstd, libchdr y demás componentes |
| libelf | elfutils 0.183 / Debian 0.183-1; alternativa LGPL-3.0-or-later o GPL-2.0-or-later según sus fuentes; textos GPL/LGPL y fuentes con parches Debian incluidos |
| libbpf | v0.7.0; avisos BSD-2-Clause/LGPL-2.1 según archivo; texto BSD original y fuentes de esa versión incluidos |
| libxdp | xdp-tools v1.2.0; avisos BSD/LGPL de usuario y GPL de programas BPF conservados en el archivo fuente completo |
| BlueZ / libbluetooth | Proyecto BlueZ y contribuyentes; biblioteca con avisos LGPL, herramientas GPL; textos `LICENSES/BlueZ-COPYING*.txt`, fuentes 5.54 y referencia histórica 5.43 |
| FreeType | The FreeType Project; David Turner, Robert Wilhelm, Werner Lemberg y contribuyentes; alternativa FTL en `LICENSES/FreeType-FTL.txt`, fuentes 2.7.1 |
| Imlib2 | Carsten Haitzler y contribuyentes enumerados en `LICENSES/Imlib2-AUTHORS.txt`; condiciones completas en `LICENSES/Imlib2.txt`, fuentes 1.4.9 |
| bzip2 | Julian Seward; licencia original `LICENSES/bzip2-1.0.6.txt`, fuentes 1.0.6 |
| libpng | Autores/copyrights enumerados en `LICENSES/libpng-1.6.28.txt`, fuentes 1.6.28 |
| zlib | Jean-loup Gailly y Mark Adler; licencia y avisos en `LICENSES/zlib-1.2.11.txt`, fuentes 1.2.11 |
| Intel/Altera | IP generado con condiciones propias; acuerdo Quartus 17.0 en `LICENSES/Intel-Quartus-17.0.txt`, alcance en `LICENSES/README.md` |

Este producto utiliza FreeType e Imlib2. Se reconoce expresamente el trabajo
de sus autores; no se atribuye su autoría al proyecto VLC → Groovy MiSTer.
Los copyrights individuales completos se conservan en los textos indicados y
en las distribuciones fuente. No se eliminan sus exclusiones de garantía.

Zstandard 1.5.5 se acompaña de LICENSE/COPYING originales junto a sus fuentes;
se utiliza su alternativa BSD. Linux UAPI conserva sus avisos individuales y
se acompaña del texto `LICENSES/Linux-syscall-note.txt` donde corresponde.
El alcance del plugin combinado, distinto de una declaración exclusivamente
LGPL, está explicado en [LICENSE.md](LICENSE.md).

Los `.a` y `.so` se conservan sin alterar. El build no los recompila.
[third_party/README.md](third_party/README.md) distingue correspondencias
verificadas de identificaciones inferidas; no se afirma que todas las fuentes
históricas hayan sido certificadas. Los manifiestos `SOURCES.json` y
`BINARIES.json` registran descargas y entradas de enlace por SHA-256.
Las fuentes auxiliares libbpf 1.2 no se presentan como fuentes del binario 0.7.

Estos avisos, las licencias y las fuentes que correspondan deben acompañar a
la distribución, también si se prepara una release binaria separada. El ZIP
documental no sustituye una revisión del contenido concreto de cada release.
El estado y las comprobaciones pendientes figuran en
[AUDITORIA_LICENCIAS.md](AUDITORIA_LICENCIAS.md).
