# OpenDMR guía de instalación

Reemplazar el firmware de fábrica del **Baofeng DM-32 / DM-32UV** por OpenDMR, actualizarlo después y volver al firmware
de fábrica. Versión en chino: [installation.md](installation.md). Versión en inglés: [installation.en.md](installation.en.md). Ya instalado, cómo se usa la radio: el
[manual de usuario](manual.es.md).

> ⚠️ **Flashear puede dejar la radio inutilizable ("brick"). Lo hace bajo su propio riesgo.**
> **El primer paso debe ser la copia de seguridad**: la flash de datos tiene **la calibración de transmisión y recepción
> propia de esta radio**, que no se puede recuperar de otra radio.
> Con una copia de seguridad se puede volver a dejar todo como estaba, incluso tras una escritura fallida: el chip tiene
> un bootloader en ROM, así que una flash de programa dañada siempre se puede volver a escribir.

## Contenido

1. [Qué se necesita](#1-qué-se-necesita)
2. [Copia de seguridad (obligatoria)](#2-copia-de-seguridad-obligatoria)
3. [Escribir el firmware y la fuente china](#3-escribir-el-firmware-y-la-fuente-china)
4. [Primer arranque](#4-primer-arranque)
5. [Escribir el codeplug](#5-escribir-el-codeplug)
6. [Actualizar](#6-actualizar)
7. [Volver al firmware de fábrica](#7-volver-al-firmware-de-fabricá)
8. [Solución de problemas](#8-solución-de-problemas)

---

## 1. Qué se necesita

### El paquete de la versión

Descargue **`OpenDMR-<version>.zip`** desde la página de Releases y descomprímalo en una carpeta (ejecute todos los
comandos de abajo en esa carpeta). Contiene:

| Archivo | Para qué sirve |
|---|---|
| `opendmr.bin` | El firmware |
| `opendmr_BFUV32.bin` | El mismo firmware con la cabecera del modo de actualización de fábrica (sólo casos especiales, como en [7](#7-volver-al-firmware-de-fabricá)) |
| `cjk.bin` | La fuente china (2.7 MB, va en la flash de datos) |
| `tools/backup/dm32_dump.py`, `tools/backup/dumper/dumper.bin` | Herramienta de copia / escritura (a través del bootloader del chip) |
| `tools/fonts/ttf/RobotoCondensed-VF.ttf` | La fuente que usa esa herramienta para el texto en la pantalla de la radio |
| `tools/debug/fw_update.py`, `tools/debug/updater/updater.bin` | Actualizaciones (con la radio corriendo OpenDMR) |
| `tools/fonts/upload_cjk.py` | Vuelve a escribir la fuente china con la radio corriendo OpenDMR |
| `docs/manual.es.md`, `docs/manual.en.md`, `docs/manual/` | Manual de usuario |
| `opendmr-cps.html` | Nuestro software de codeplug (OpenDMR CPS): doble clic, en Chrome / Edge; ver [5](#5-escribir-el-codeplug) |
| `docs/installation.es.md`, `docs/installation.en.md`, `README.es.md`, `README.en.md` | Esta guía, el README del proyecto |
| `licences/` | Las licencias OFL de las fuentes (Roboto Condensed, Noto Sans SC) |
| `VERSION.txt` | Versión, git commit, hora de empaquetado |

Una descarga más, **`SHA256SUMS.txt`**, tiene las sumas de verificación (compruébela con
`certutil -hashfile OpenDMR-<version>.zip SHA256`).

Para el codeplug, use **OpenDMR CPS** (`opendmr-cps.html` en la versión, y en la página de la versión por separado) o el
**OpenGD77 CPS** (no está en la versión: descárguelo de <https://www.opengd77.com/>, una versión con DM32/UV008; probado
con E2026.07.13.01). Ver [5](#5-escribir-el-codeplug).

### Computadora

- Windows 10 / 11.
- **Python 3** (<https://www.python.org/downloads/>; marque "Add python.exe to PATH" en el instalador), después en
  PowerShell:

  ```powershell
  py -m pip install pyserial pillow
  ```

  (pillow es opcional: con él la pantalla de la radio muestra texto además del progreso.)

### Cable de programación

- El cable CH340 que vino con la radio funciona; un cable FTDI (p. ej. BTECH PC03) es más confiable.
- Conectado, el Administrador de dispositivos → Puertos (COM y LPT) muestra su puerto COM (los comandos de abajo dicen `COM6`: use el suyo).
  Instale el controlador CH340 si Windows no lo tiene.
- Empuje el conector hasta el tope: el enchufe del DM-32 está apretado, y un conector que no está completamente insertado no comunica.

### Batería

Cárguela completamente antes. Todo el trabajo (sobre todo la copia de seguridad) lleva unos 40 minutos.

---

## 2. Copia de seguridad (obligatoria)

El DM-32 tiene dos chips flash:

| Chip | Contenido | Tiempo de copia |
|---|---|---|
| Flash de programa (25Q08 o 25Q16, 1 – 2 MB) | Bootloader, firmware de fábrica | Unos 1 minuto |
| Flash de datos (25Q128, 16 MB) | Codeplug de fábrica, fuentes, voz, **calibración** | Unos 13 minutos |

**Lea cada chip dos veces**: sólo dos lecturas idénticas hacen una copia confiable.

Cada ejecución es igual:

1. **Apague la radio** y conecte el cable;
2. Ejecute el comando; dice `encienda la radio ahora (dentro de 30 s)`;
3. **Encienda la radio dentro de 30 s, sin pulsar ninguna tecla.** Una pantalla negra o una pantalla de progreso, y el
   LED apagado, son normales;
4. Espere a que termine el comando (muestra los bytes y los segundos), luego **apague la radio** antes del siguiente.

```powershell
mkdir backup
py -u tools\backup\dm32_dump.py COM6 program backup\program-1.bin
py -u tools\backup\dm32_dump.py COM6 program backup\program-2.bin
py -u tools\backup\dm32_dump.py COM6 data    backup\data-1.bin
py -u tools\backup\dm32_dump.py COM6 data    backup\data-2.bin
```

Compare las dos lecturas:

```powershell
fc.exe /b backup\program-1.bin backup\program-2.bin
fc.exe /b backup\data-1.bin backup\data-2.bin
```

Las dos deben decir **"FC: no differences encountered"** (ninguna diferencia encontrada). Si no, lea otra vez hasta que
dos lecturas sean iguales.

Copie la carpeta `backup` a algún lugar seguro (nube, memoria USB) y **etiquétela con el número de serie de la radio**:
una copia de seguridad pertenece sólo a su propia radio.

Compare las dos lecturas:

```powershell
fc.exe /b backup\program-1.bin backup\program-2.bin
fc.exe /b backup\data-1.bin backup\data-2.bin
```

Ambas deben decir **"FC: no differences encountered"**. Si no es así, lea de nuevo hasta tener dos lecturas iguales.

Copie la carpeta `backup` a algún lugar seguro (nube, USB) y **etiquétela con el número de serie de la radio**: una copia
de seguridad es de su propia radio solamente.

---

## 3. Escribir el firmware y la fuente china

Como en la copia de seguridad: apague la radio, ejecute el comando, encienda dentro de 30 s, sin pulsar tecla.
`--write` escribe sólo los sectores de 4 KB que difieren del archivo, lee cada uno de vuelta y compara todo de nuevo al
final.

```powershell
py -u tools\backup\dm32_dump.py COM6 program opendmr.bin --write --start 0xC000
```

(La flash de bootloader, debajo de `0xC000`, nunca se toca.) Debe terminar con `... sectors written, all ... the same as
the file`. Apague la radio.

```powershell
py -u tools\backup\dm32_dump.py COM6 data cjk.bin --write --start 0x280000
```

La fuente china va en la flash de datos desde 0x280000 (unos 5 minutos). Escríbala aunque la interfaz sea inglés:
nombres de canales y contactos chinos la necesitan. Apague la radio al final.

---

## 4. Primer arranque

La radio arranca en la pantalla de canal. Sin ajustes todavía usa los por defecto; el codeplug está vacío: el CPS lo
escribe en el paso siguiente.

Conviene hacerlo primero:

- **Idioma**: mantenga la tecla de menú (la de la izquierda bajo la pantalla) para el menú principal → Opciones →
  Idioma, la tecla de menú cambia Inglés / 中文 / Español;
- **Su DMR ID**: Opciones → Opciones generales → DMR ID de usuario (o en el CPS);
- **Hora y fecha**: menú principal → Información de la radio → Hora / Fecha (con una posición fija el reloj se fija solo:
  ver "GPS" en el manual).

Teclas y pantallas: el [manual de usuario](manual.es.md).

---

## 5. Escribir el codeplug

Puede usar cualquiera de los dos; el codeplug es el mismo, así que se pueden mezclar:

| | OpenDMR CPS (`opendmr-cps.html` en la versión) | OpenGD77 CPS |
|---|---|---|
| Instalar | Nada: doble clic, en Chrome / Edge | Un programa de Windows |
| Nombres chinos | Escribir en, en GB2312, se muestran en la radio | No probado |
| Canales P25 y el NAC, AM / USB / LSB | Se editan | P25 se ve como DMR, AM etc. como analógico: se pierden al editarse |
| Los ajustes de la radio (potencias, retroiluminación, ganancia de micrófono, ...) | Se leen y se escriben | No se leen ni se escriben |
| Base de DMR IDs (nombres de los que llaman) | Todavía no | Se escribe |

**OpenDMR CPS**:

1. Abra `opendmr-cps.html` en Chrome o Edge;
2. Encienda la radio **normalmente** (con OpenDMR corriendo), conecte el cable, haga "Conectar" y elija el puerto serie
   del cable;
3. "Leer radio" → editar → "Escribir radio"; la radio se reinicia al escribirse. "Guardar archivo" antes para una copia
   de seguridad.

**OpenGD77 CPS**:

1. Descargue el OpenGD77 CPS de <https://www.opengd77.com/> (una versión con DM32/UV008; probado con E2026.07.13.01) y
   instálelo;
2. Inicie el OpenGD77 CPS, **Radio type DM32/UV008**;
3. Encienda la radio **normalmente** (con OpenDMR corriendo) y conecte el cable;
4. Primero **Leer codeplug**, edite canales, zonas, contactos, listas de grupos, ..., luego **Escribir codeplug**.

Mientras lee o escribe, la radio muestra qué parte y cómo va: no desconecte el cable.

- Los archivos CPS y de codeplug de fábrica **no pueden** usarse: haga el codeplug de nuevo.
- El OpenGD77 CPS también escribe la base de DMR IDs y los keps de satélites; no lee ni escribe los ajustes de la radio:
  cámbielos en los menús de la radio, o con OpenDMR CPS.

---

## 6. Actualizar

Con OpenDMR corriendo, una nueva versión no necesita ciclo de potencia ni sincronización: radio encendida, cable
conectado, y entonces

```powershell
py tools\debug\fw_update.py opendmr.bin --port COM6
```

Sólo se escriben los sectores que cambian (unos 35 s en todos), la pantalla muestra el progreso, la radio se reinicia al
final. Los ajustes y el codeplug se conservan.

Si los release notes dicen que la fuente cambió, escriba también la fuente (la radio encendida como siempre; reiníciela
después):

```powershell
py tools\fonts\upload_cjk.py COM6 --file cjk.bin
```

Si `fw_update.py` falla o la radio no arranca, escriba `opendmr.bin` otra vez como en la sección 3 (protocolo de encendido
y apagado).

---

## 7. Volver al firmware de fábrica

Escriba la copia de seguridad **de esta propia radio** (apague, ejecute, encienda dentro de 30 s):

```powershell
py -u tools\backup\dm32_dump.py COM6 program backup\program-1.bin --write
py -u tools\backup\dm32_dump.py COM6 data    backup\data-1.bin --write
```

`--write` cambia sólo los sectores que difieren. Con ambos chips escritos de vuelta, la radio queda exactamente como al
empezar (firmware de fábrica, codeplug, voz, calibración).

Para poner sólo el firmware de fábrica y dejar la flash de datos: mantenga **PTT + SK1** mientras enciende (el modo de
actualización de fábrica), luego escriba el firmware de fábrica con el Firmware loader del CPS de fábrica.

Lo que OpenDMR escribió en la flash de datos (fuente china, codeplug, ajustes) queda ahí, y los datos que el firmware de
fábrica usa pueden haber sido sobrescritos: para una restauración completa, escriba de vuelta la copia de seguridad de la
flash de datos.

---

## 8. Solución de problemas

**No pasa nada después de `encienda la radio ahora`, o un error**

- Empiece con la radio **apagada**, enciéndala después del mensaje, sin pulsar ninguna tecla mientras arranca.
- Cuando una radio con OpenDMR no se apaga con la perilla, **quite la batería y póngala de nuevo**.
- Verifique el número del puerto COM y que el conector esté bien insertado.
- Pruebe con un cable de otro chip (el CH340 es quisquilloso con el timing; los cables FTDI andan mejor).
- `py -u tools\backup\dm32_dump.py COM6 program --speedtest` muestra qué velocidades logra el cable; si es poco fiable,
  añada `--baud 115200` (más lento, más estable).

**Se interrumpió una escritura**

No hay daño: vuelva a ejecutar el mismo comando. Sólo se escriben los sectores que difieren, los ya hechos se dejan.
El bootloader está en el chip, así que una flash de programa dañada siempre se puede volver a escribir.

**El texto chino sale como cajas o no aparece**

Falta o está incompleta la fuente china: escriba `cjk.bin` como en la sección 3, o con `upload_cjk.py` con la radio
corriendo (sección 6), y después reinicie la radio.

**¿Qué versión tiene la radio?**

Menú principal → Información del firmware: la primera línea es la versión (git commit), luego la hora de la compilación.

**¿Cuánto tiempo hay que tener la copia de seguridad?**

Para siempre. Su calibración se midió en esta radio en la fábrica: sin ella la potencia de transmisión y la recepción ya no
son exactas.
