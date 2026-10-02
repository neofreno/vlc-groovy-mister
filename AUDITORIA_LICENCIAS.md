# Revisión de licencias previa a publicación

## Decisión de publicación — 2026-10-02

Tras recibir el resultado de esta revisión, el responsable ha solicitado
publicar en `https://github.com/neofreno/vlc-groovy-mister`, incluyendo las
bibliotecas precompiladas. La publicación conserva esta auditoría y sus
limitaciones; no equivale a certificar como cerrados los puntos pendientes.
Se utiliza una instantánea con historial Git nuevo, sin los commits históricos
del repositorio local. Las afirmaciones de no publicación que figuran debajo
describen el estado anterior a esa decisión.

## Seguimiento — 2026-10-02

Se ha completado la corrección documental sin modificar el código activo,
RTL ni bibliotecas precompiladas. El responsable solicita conservar esas
bibliotecas y confirma que Groovy/Groovy MiSTer permiten la distribución.
Se registra esa indicación sin atribuir al autor upstream una excepción o
una nueva licencia que no conste en sus avisos.

| Punto | Actuación y estado |
|---|---|
| Alcance del plugin | Añadido `LICENSE.md`: el conjunto no se presenta exclusivamente como LGPL. Metadatos históricos intactos por petición expresa; no son una excepción a GPL |
| GPL de Groovy/Main | Avisos originales conservados y autorización de distribución indicada por el responsable registrada. Sigue sin acreditarse el alcance preciso de la opción de versión del código HPS/API sin aviso individual; no se afirma GPL v2-only ni se inventa GPL v3 |
| Bibliotecas precompiladas | Conservadas; 24 entradas de enlace inventariadas en `third_party/BINARIES.json`, incluidos alias del SDK. Se adjuntan 14 archivos fuente con URL/tamaño/SHA-256 en `SOURCES.json` |
| libelf | Correspondencia binaria exacta con Debian ARM 0.183-1; adjuntados fuente upstream, parches/reglas Debian y descriptor con hashes verificados |
| Otras bibliotecas | Versiones/cabeceras/cadenas contrastadas; evidencias y límites detallados en `third_party/README.md`. No se ha demostrado la correspondencia completa de todos los binarios históricos |
| Textos y atribuciones | Añadidos GPL/LGPL, excepción syscall, BSD/MIT, avisos Zstd, SDK, BlueZ, FreeType, Imlib2, bzip2, libpng y zlib |
| Intel/Altera | Incluido acuerdo original Quartus 17.0 y documentada su condición de dispositivo; avisos intactos, sin cambios RTL ni nuevo RBF. No se declara todo GPL |
| Exportación | El export incorpora las fuentes originales y avisos junto a las bibliotecas; no incorpora `.git`. Se verifica su contenido antes de ofrecerlo |
| Publicación | No se ha hecho commit ni push. El historial Git original no se ha auditado exhaustivamente ni reescrito; el ZIP es una instantánea sin historial |

**Límite pendiente:** aportar archivos fuente de la versión identificada no
demuestra por sí solo que no existieran parches locales ni opciones necesarias
en una compilación histórica. En especial, para libbluetooth no se dispone de
la configuración/registros originales completos. Obtenerlos del proveedor, o
reconstruir y validar bibliotecas desde una base documentada, cerraría esa
incertidumbre. No se sustituyen bibliotecas funcionales como parte de una tarea
restringida a documentación. Tampoco se resuelve una ambigüedad de permiso
mediante una declaración propia de compatibilidad.

La revisión reduce y documenta los riesgos, pero **no certifica que todos los
puntos jurídicos/de procedencia estén cerrados**. Para publicar conservando los
binarios históricos, falta resolver esas evidencias; no basta con marcar el
checklist como completado. Los apartados siguientes conservan el diagnóstico
inicial, y sus referencias a archivos ausentes deben leerse con este seguimiento.

Comprobación de preservación: 1740 archivos existentes de código, RTL y build
coinciden por SHA-256 con la instantánea tomada antes de esta corrección
documental. Las 24 entradas del manifiesto binario y los 14 archivos fuente
pasan la verificación de tamaño y SHA-256. Las 16 entradas ARM coinciden además
con sus hashes originales de importación. No se ha ejecutado VLC ni accedido a
MiSTer. GitHub está autenticado, pero no hay remoto configurado.

## Diagnóstico inicial — 2026-10-01

### Resultado inicial

**No dar todavía por listo para publicación pública el paquete unificado actual.**
Se han identificado declaraciones inconsistentes, avisos incompletos y
dependencias binarias cuya procedencia/licencia de distribución no está cerrada.
Esto no significa que el proyecto no pueda publicarse: hace falta completar
estos puntos antes de presentarlo como una distribución conforme.

Revisión técnica de archivos locales y fuentes oficiales, no dictamen jurídico.
No se han cambiado licencias, eliminado dependencias, contactado autores ni
publicado código. No se ha auditado exhaustivamente el historial de Git.

## 1. Declaración LGPL del plugin frente a la API GPL

Evidencias locales:

- `src/version.h`: `VERSION_LICENSE` declara `LGPL-2.1-or-later`.
- `src/module.cpp`: `VLC_MODULE_LICENSE` usa `VLC_LICENSE_LGPL_2_1_PLUS`.
- `src/vlc-groovy-mister.vcxproj` enlaza la API estática compilada desde
  `Groovy_MiSTer/api/` dentro de la DLL del plugin.
- `Groovy_MiSTer/LICENSE` contiene GPL v2; no se ha encontrado una licencia
  alternativa LGPL específica para `api/groovymister.cpp` o `.h`.
- `src/msvc-compat/poll.h` conserva además un aviso GPL-2.0-or-later de gnulib.

Conclusión: **no debe describirse toda la DLL combinada como exclusivamente
LGPL** basándose en los metadatos actuales. Una licencia de los archivos propios
no elimina las condiciones de la API incorporada. Hay que confirmar el alcance
de la licencia de Groovy y adecuar los avisos de distribución de la combinación.
No se debe cambiar arbitrariamente la licencia de los archivos de terceros.

La FSF distingue un agregado de programas independientes de código enlazado en
un mismo programa, y describe las condiciones de combinación con GPL:
[FAQ oficial](https://www.gnu.org/licenses/gpl-faq.en.html#MereAggregation).
VideoLAN también advierte de las condiciones GPL de determinados módulos:
[documentación legal](https://docs.videolan.me/vlc-user/en/support/faq/legalconcerns.html).

## 2. GPL v2 y GPL v3: aclarar el alcance, no asumir incompatibilidad global

- `Groovy_MiSTer/hps_linux/src/main.cpp` declara GPL-3.0-or-later y el
  `LICENSE` de esa base contiene GPL v3.
- El código Groovy HPS se compila dentro de ese mismo ejecutable. Sus archivos
  principales no contienen una declaración individual que permita resolver
  inequívocamente la relación con el GPL v2 de la raíz de Groovy.
- `Groovy_MiSTer/Groovy.sv` sí autoriza GPL v2 **o posterior**.
- Los dos módulos locales `rtl/JTFRAME/ram/jtframe_rpwp_ram.v` y
  `rtl/JTFRAME/video/jtframe_hsize.v` autorizan GPL v3 **o posterior**.

No se puede concluir que todo Groovy sea «GPL v2 only» solo porque exista el
texto GPL v2, ni extender a todos sus archivos el «or later» de `Groovy.sv`.
Debe confirmarse con los avisos originales o con el titular la licencia aplicable
al código HPS sin declaración individual y su compatibilidad con la base Main.

GPL v2-or-later puede combinarse bajo GPL v3; GPL v2-only y GPL v3 no son
intercambiables. Véase la [guía oficial de GPL v3](https://www.gnu.org/licenses/quick-guide-gplv3.en.html).
Tener varias licencias en un repositorio no es por sí mismo un problema: importa
qué se combina en cada programa y qué se distribuye separadamente.

## 3. Los archivos .a y .so incluidos también son una distribución binaria

El ZIP denominado «fuentes» incluye entradas de enlace ARM bajo
`Groovy_MiSTer/hps_linux/src/lib/`: libelf, libbpf, libxdp, Bluetooth, Imlib2,
FreeType, bzip2, libpng y zlib. No basta con excluir las DLL y ejecutables finales.

Hallazgos concretos:

- libelf: las cabeceras señalan elfutils 0.183 y ofrecen LGPL-3.0-or-later o
  GPL-2.0-or-later. Se incluyen `libelf.a` y `libelf.so`, pero no las fuentes
  correspondientes completas ni una vía de entrega de fuentes documentada.
  La cabecera identifica la versión esperada; no demuestra por sí sola la
  procedencia exacta del binario.
- libbpf: el binario se denomina `libbpf.so.0.7.0`, y sus cabeceras indican 0.7.
  Las fuentes auxiliares copiadas dentro de xdp-tools indican 1.2. **No son
  evidencia de fuentes correspondientes al binario 0.7.**
- libbpf y libxdp tienen avisos duales LGPL/BSD en los archivos inspeccionados.
  No se debe afirmar que todo binario exige fuentes por GPL/LGPL sin verificar
  qué licencia alternativa y qué componentes se aplican. La ruta BSD puede
  simplificar la redistribución, conservando los avisos exigidos y verificando
  la versión/composición exactas. [libbpf oficial](https://github.com/libbpf/libbpf).
- Las otras bibliotecas precompiladas necesitan identificación de versión,
  procedencia, avisos completos y comprobación individual de sus condiciones.

Opciones: proporcionar fuentes correspondientes y avisos cuando proceda, o
dejar de redistribuir esos binarios y preparar la adquisición/compilación de
dependencias identificadas. No se han retirado en esta revisión porque rompería
la compilación autosuficiente que acabamos de verificar.

La FSF explica que las fuentes correspondientes deben ser las del binario
distribuido, no simplemente una versión parecida o más reciente:
[FAQ oficial](https://www.gnu.org/licenses/gpl-faq.en.html#SourceAndBinaryOnDifferentSites).

## 4. Textos y atribuciones incompletos

- LZ4: cabeceras BSD-2-Clause y texto de licencia conservado en `api/lz4/LICENSE`.
- miniz: texto de licencia conservado; libchdr: condiciones BSD conservadas en
  sus fuentes; libco: declaración de dominio público en sus cabeceras.
- Zstandard local identifica 1.5.5. Sus fuentes permiten BSD o GPL v2, pero
  faltan los archivos LICENSE/COPYING a los que remiten sus cabeceras. Para
  utilizar claramente la alternativa BSD, añadir el aviso original de esa
  versión: [Zstandard v1.5.5](https://github.com/facebook/zstd/blob/v1.5.5/LICENSE).
- SDK VLC: cabeceras con LGPL-2.1-or-later, pero sin archivo COPYING/LICENCE
  dentro de `sdk/`. Hay una copia LGPL 2.1 en otro componente, sin un mapa
  suficiente de atribuciones para el SDK. Deben explicitarse licencia y origen
  de las bibliotecas de importación, que no equivalen a redistribuir VLC completo.
- UAPI Linux: conservar SPDX y acompañar la excepción `Linux-syscall-note`
  donde corresponde. No se ha encontrado su texto completo en esta importación.
  La excepción permite el uso de UAPI sin extender automáticamente GPL a la
  aplicación: [reglas oficiales del kernel](https://docs.kernel.org/process/license-rules.html).
- No existe una declaración de alcance en la raíz que explique de forma
  suficiente la licencia del código propio y las exclusiones de terceros.
  `THIRD_PARTY_NOTICES.md` es un inventario preliminar, no esa declaración.

## 5. RTL generado por Intel/Altera: no etiquetarlo todo como GPL

`rtl/fifo_sound.v`, `rtl/fifo_vga.v` y varios PLL bajo `sys/` incluyen avisos de
Intel que remiten a sus acuerdos y limitan el uso a dispositivos Intel/Altera.

Se consultó el acuerdo instalado de la misma generación:
`C:/intelFPGA_lite/17.0/licenses/license.txt`, identificado como Quartus Prime
License Agreement Version 17.0. Su apartado 2.5 permite distribuir determinados
IP Megafunctions/Components en código fuente, sujeto a su condición de uso en
los dispositivos definidos. El apartado 20 preserva las licencias separadas.

Por tanto, **no es correcto concluir que estos archivos estén prohibidos para
redistribución**, pero tampoco que sean GPL por estar dentro de este repositorio.
Hay que documentar qué archivos y permiso concreto se aplican, preservar sus
avisos y revisar el encaje de la distribución del core combinado. El acuerdo
local ayuda a identificar la vía, pero no sustituye verificar el permiso que
acompañaba a cada IP original. No se ha generado ni distribuido un RBF nuevo.

## Orden recomendado antes de publicar

- [ ] Confirmar el alcance GPL y la opción «or later» del Groovy API/HPS donde
  no hay declaración individual suficiente. No asignar GPL v3 por suposición.
- [ ] Acordar la declaración de distribución del plugin combinado y corregir
  sus metadatos sin borrar licencias/autorías originales.
- [ ] Completar textos, excepciones y atribuciones que faltan.
- [ ] Identificar cada biblioteca binaria y resolver sus requisitos; distinguir
  claramente las alternativas BSD de las obligaciones GPL/LGPL.
- [ ] Documentar y verificar los permisos aplicables al RTL Intel/Altera.
- [ ] Revisar también el historial Git que se vaya a publicar: una eliminación
  del índice no borra binarios de commits anteriores. No reescribirlo sin decisión
  expresa del usuario.
- [ ] Regenerar el paquete y revisar su contenido antes del commit/push.

No hace falta abandonar el proyecto unificado, pero el ZIP actual no debe
presentarse como una distribución «solo fuentes, todo LGPL» ni como una revisión
de cumplimiento ya cerrada. Una revisión jurídica especializada puede ser
necesaria para las ambigüedades de titularidad/licencia que no resuelva upstream.
