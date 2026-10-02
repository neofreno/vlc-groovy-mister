# Licencias y alcance de esta distribución

Este repositorio reúne varios componentes; **no se distribuye todo bajo una
única licencia LGPL**. Se conservan las licencias, condiciones y autorías de
cada componente. Este archivo aclara el alcance, no cambia la licencia del
código de terceros ni concede permisos adicionales sobre él.

## Plugin y API

Los archivos propios del plugin que declaran LGPL-2.1-or-later conservan esa
declaración. La API Groovy incorporada conserva la GPL de
`Groovy_MiSTer/LICENSE`. La distribución de una combinación que incorpora esa
API debe respetar también GPL v2; no se ofrece la combinación exclusivamente
bajo LGPL. Se incluyen ambas licencias en `LICENSES/`.

Por petición expresa del responsable del proyecto, no se modifica código:
`VERSION_LICENSE` y `VLC_MODULE_LICENSE` permanecen tal como estaban. Sus cadenas
LGPL identifican el aviso histórico del plugin, **no constituyen una excepción
a la GPL de la API ni describen por sí solas todas las condiciones del conjunto**.
Este aviso y `THIRD_PARTY_NOTICES.md` deben acompañar también a las distribuciones
binarias del plugin. No se borran ni sustituyen los avisos originales.

## Receptores y FPGA

- Main MiSTer: GPL v3, con opción de versiones posteriores donde lo declaran
  sus cabeceras; texto en `Groovy_MiSTer/hps_linux/src/LICENSE`.
- Groovy: licencia y avisos originales conservados. La autorización para
  distribuir la integración ha sido confirmada por el responsable del proyecto
  el 2026-10-02. No se convierte esa confirmación en una nueva licencia universal
  ni en una declaración inventada del autor upstream.
- `Groovy.sv`: GPL-2.0-or-later; módulos JTFRAME: GPL-3.0-or-later según sus
  cabeceras. La opción «or later» es específica de los archivos que la incluyen.
- Archivos Intel/Altera: condiciones propias, no GPL. Ver
  `LICENSES/Intel-Quartus-17.0.txt` y `LICENSES/README.md`.
- Cabeceras Linux UAPI: licencias/alternativas SPDX de cada archivo y excepción
  `Linux-syscall-note` cuando está indicada.

La coexistencia en un repositorio no relicencia sus componentes. No se afirma
que cualquier combinación futura de versiones o bibliotecas sea compatible.
La autorización declarada para distribuir no equivale, por sí sola, a conceder
una excepción de enlace o a cambiar de GPL v2-only a GPL v3.

## Terceros y fuentes

Ver `THIRD_PARTY_NOTICES.md`, `third_party/README.md` y
`third_party/SOURCES.json`. Las bibliotecas precompiladas se conservan y se
acompañan de archivos fuente upstream, avisos y evidencias de identificación.
Los niveles de verificación se distinguen expresamente; no se afirma una
reconstrucción binaria exacta de todas las bibliotecas históricas.

No se ofrecen garantías distintas de las previstas en las licencias originales.
