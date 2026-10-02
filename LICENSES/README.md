# Textos originales y aplicación

Los textos de esta carpeta acompañan a los componentes; no se aplican todos
indistintamente a todos los archivos. Las cabeceras y licencias específicas
siguen siendo la referencia para cada componente.

| Texto | Aplicación / procedencia |
|---|---|
| GPL-2.0.txt | Copia de `Groovy_MiSTer/LICENSE`; API, código GPL v2 y componentes que permiten esa versión |
| GPL-3.0.txt | Copia de `Groovy_MiSTer/hps_linux/src/LICENSE`; Main y componentes GPL v3 |
| LGPL-2.1.txt | Texto incluido por xdp-tools; plugin/SDK y partes LGPL 2.1 |
| LGPL-3.0.txt | `elfutils-0.183/COPYING-LGPLV3`; alternativa de libelf, acompañada de GPL v3 |
| Linux-syscall-note.txt | Texto original de Linux v6.1; excepción de las cabeceras UAPI |
| MIT.txt, BSD-2-Clause.txt, BSD-3-Clause.txt | Textos de Linux v6.1; no sustituyen los copyrights de cada cabecera |
| libbpf-BSD-2-Clause.txt | Texto de libbpf v0.7.0; alternativa BSD, manteniendo los avisos individuales |
| BlueZ-COPYING*.txt | Textos de BlueZ 5.54; GPL v2 y LGPL 2.1, según los archivos |
| FreeType-FTL.txt | FreeType 2.7.1, alternativa FTL; atribución pública en THIRD_PARTY_NOTICES.md |
| Imlib2.txt, Imlib2-AUTHORS.txt | Imlib2 1.4.9; permiso y reconocimiento de autores |
| bzip2-1.0.6.txt | Licencia original de bzip2 1.0.6 |
| libpng-1.6.28.txt | Licencia original de libpng 1.6.28 |
| zlib-1.2.11.txt | README original con licencia y avisos de zlib 1.2.11 |
| Intel-Quartus-17.0.txt | Acuerdo instalado de Quartus 17.0, sin editar |

Zstandard tiene sus textos originales de 1.5.5 en
`Groovy_MiSTer/hps_linux/src/lib/zstd/LICENSE` y `COPYING`; se utiliza la
alternativa BSD para la distribución de ese componente. LZ4 y miniz conservan
sus LICENSE originales junto a las fuentes. El SDK tiene `sdk/COPYING.LIB` y
se acompaña del archivo fuente completo VLC 3.0.16.

En las cabeceras UAPI con alternativas se puede seguir la rama GPL con excepción
syscall que indiquen, sin imponer esa GPL al programa de espacio de usuario.
Las cabeceras con una conjunción `AND MIT` conservan también la condición MIT.
Donde se autoriza «GPL-1.0+» o «LGPL-2.0+», los textos GPL 2 / LGPL 2.1 permiten
ejercer la opción posterior. Esto no se extiende a archivos que no la ofrezcan.

## Intel/Altera

Los avisos de `rtl/fifo_sound.v`, `rtl/fifo_vga.v`, `rtl/pll.v` y de los PLL
generados bajo `sys/` se conservan literalmente. El apartado 2.5 del acuerdo
Quartus 17.0 contempla distribución de IP Megafunctions/Components fuente y
derivados, condicionada al uso en los dispositivos definidos en ese acuerdo.
Los apartados de definiciones y 20 mantienen las condiciones separadas y los
derechos de terceros. No se redistribuye Quartus ni se concede una licencia
general de las herramientas. No se elimina la restricción de dispositivo.

Esta tarea no modifica el RTL ni crea un RBF. El acuerdo se suministra como
documentación de las condiciones; sus limitaciones no se convierten en GPL.
No debe usarse un archivo LICENSE global para borrar esos avisos.
