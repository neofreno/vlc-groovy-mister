# SDK VLC incluido

Las cabeceras incluidas identifican VLC 3.0.16 y conservan los avisos de VideoLAN
y de sus autores. `COPYING.LIB` proporciona el texto LGPL 2.1; las cabeceras que
permiten versiones posteriores mantienen esa opción.

Las bibliotecas `.lib` de `lib/` y `lib64/` son entradas de enlace/importación
del proyecto, no una distribución completa del reproductor VLC. Se conservan
sin cambios. Para que los destinatarios dispongan también de las fuentes de
esa versión se incluye `third_party/sources/vlc-3.0.16.tar.xz`, obtenido del
servidor oficial de VideoLAN. Su hash figura en `third_party/SOURCES.json`.

No se afirma haber reconstruido o comparado byte a byte estas bibliotecas de
importación con un SDK nuevo. No se ha inspeccionado ni ejecutado una instalación
local de VLC para esta comprobación. Las fuentes de VLC y sus dependencias
tienen sus propios avisos dentro del archivo suministrado.
