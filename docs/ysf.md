# YSF (Fusion / C4FM) en OpenDMR

Esta página resume la implementación de YSF6148 (Yaesu Fusion, C4FM) añadida a OpenDMR. El código del protocolo se
portó desde el fork [DM32-nxdn-p25](https://github.com/donttellmeimcute/DM32-nxdn-p25) (lyra XE1POC,
GPL-3.0-or-later); la capa física y la integración con la radio son las de OpenDMR. La procedencia detallada, los
créditos y las licencias están al final de esta página.

> Esta rama (`ysf-gpl-0.9.3`) es un **fork personal y experimental**, publicado desde
> [`Pablog5395/OpenDMR`](https://github.com/Pablog5395/OpenDMR) y no desde el repositorio original. La parte YSF es
> GPL-3.0-or-later y el resto mantiene la licencia de OpenDMR; el desglose está en
> [`LICENSES-YSF.md`](../LICENSES-YSF.md). No es una contribución para fusionar en `va3bpf/OpenDMR`.

## Modo y canal

- `CODEPLUG_MODE_YSF = 8`, etiqueta de canal opendmr `0xA7`. El editor de canal muestra la fila de **DG-ID** donde
  otros modos muestran el colour code / NAC / RAN / CAN: el byte se comparte en la estructura del codeplug.
- Ciclo de la tecla de modo: FM → AM → USB → LSB → DMR → P25 → NXDN → M17 → **YSF**.
- DG-ID 0 – 127 en el editor (un Yaesu usa 0 – 99; 0 = «recibir todo», transmitir 0). Se envía en el FICH (`SC`) de
  cada trama; en recepción actúa como un squelch digital. Con un DG-ID seleccionado también se abren las tramas con
  `SC = 0` (sin DG-ID) y `SC = 127` (Break Out), como en un Yaesu.
- El destino de TX es el nombre del contacto (o `ALL`); la fuente es el indicativo (callsign) de «General» en el CPS,
  con un sufijo SSID opcional (`-1` … `-15`) por canal. El SSID se edita en la fila **SSID** de los detalles del canal
  (0 = sin sufijo) y se guarda en el byte que YSF no usa del codeplug; la llamada de grupo se envía como `ALL`, que en
  el aire son diez asteriscos. Con un hotspot Pi-Star, un sufijo en el indicativo del propio hotspot o `SelfOnly=1`
  pueden rechazar fuentes con `-N`; para usar SSID hay que dejar el callsign del hotspot sin sufijo y `SelfOnly=0`.
- El modo de voz del canal se elige en la fila **YSF Mode**: `V/D-1` (por defecto, también para un canal creado por el
  CPS), `DN` o `VW`. El DT se guarda en `flag3` del codeplug con la etiqueta `0xC0`; en TX manda el modo configurado,
  pero en RX manda el DT recibido: una llamada VW se descodifica con IMBE aunque el canal esté en V/D-1, y al terminar
  (o antes del siguiente PTT) el camino de TX vuelve al modo del canal.
- El panel QSO muestra el callsign en grande, el destino sobre el S-meter y el DG-ID en la barra de estado. El
  escaneo usa `SCAN_YSF` (mismo `heard_within()` que NXDN).

## Teclas (operación)

| Acción | Tecla |
|---|---|
| Llegar al modo YSF desde la pantalla principal | `SK1 + *` hasta `YSF` (orden: FM → AM → USB → LSB → DMR → P25 → NXDN → M17 → YSF) |
| Editar el DG-ID del canal | Menú → detalles del canal → fila **DG-ID** (donde otros modos ponen el colour code): `▲ ▼` elige la fila, `◀ ▶` o el knob cambia de 1 en 1, `menú` guarda, `volver` descarta |
| Añadir el SSID al indicativo | Menú → detalles del canal → fila **SSID** (sólo en YSF): `◀ ▶` o el knob recorren 0 – 15, `menú` guarda, `volver` descarta |
| Elegir V/D-1, DN o VW | Menú → detalles del canal → fila **YSF Mode** (sólo en YSF): `◀ ▶` o el knob recorren `V/D-1 → DN → VW`, `menú` guarda, `volver` descarta |
| Elegir a quién llamar | `SK2 + #` → lista de contactos → `menú`: el **nombre del contacto** se envía como destino |
| Recorrer los contactos del grupo RX del canal | `▶ ◀` en la pantalla principal |
| Poner `ALL` (llamada de grupo) | Sin contacto seleccionado, o `#` → **All Call** → `menú` (envía `ALL`) |
| Ver el destino y el DG-ID | Línea debajo de la frecuencia: `NOMBRE  DG-ID n` (o `ALL  DG-ID n`) |
| Ver al que llama | Panel QSO: callsign grande, destino sobre el S-meter, `DG-ID n` en la barra de estado |

Los números de TG/PC de `#` se muestran como `TG n` / `PC n` en el destino; en YSF conviene usar **contactos con
nombre de callsign**, porque el destino viaja como texto de 10 caracteres. El canal se puede crear en el CPS como
digital y cambiar el modo en el editor del radio; el CPS 0.13.0 no tiene una etiqueta propia para YSF (modo 8, tag
`0xA7`).

## Capa física y trama

| Parámetro | Valor |
|---|---|
| Modulación | 4FSK (C4FM), 4800 símbolos/s, RRC α = 0.2 |
| Ancho de canal | 12.5 kHz; desviación ±1350 Hz (narrow) o ±2700 Hz (wide) |
| Trama | 100 ms = 480 dibits = 120 bytes = 5 bloques de 20 ms |
| Preámbulo | 200 ms de `0x77` (960 dibits, `+3 -3 +3 -3…`) antes de la primera sync, como MMDVM/Yaesu |
| Sync | `D471C9634D`, 20 símbolos, al inicio de cada trama (20 dibits con hasta 4 errores) |
| FICH | 20 bytes con FEC, protegidos por interleave; DT, FI, FN, CM, SC (DG-ID), etc. |
| DCH1 / DCH2 | 20 + 20 bytes por trama; CSD (10 bytes destino + 10 bytes fuente) y datos de voz |

La voz depende del DT de la trama:

- **V/D-1 (DT 0)**: el campo VCH son 9 bytes de AMBE+2 directos, cinco por trama (el mismo formato que DMR).
- **DN (DT 2)**: el campo de 13 bytes se convierte a 9 bytes de AMBE con `ysf_voice_dn_to_frame()` y viceversa. El
  ciclo tiene siete tramas de comunicación (`FN 0..6`, `FT 6`) aunque no haya datos auxiliares: `FN0` destino, `FN1`
  fuente, `FN2` downlink, `FN3` uplink, `FN4/5` los cuatro campos remotos (5+5) y `FN6` la unidad DT1. `FT` se
  mantiene también en la cabecera y el terminador; en una llamada de voz `FN6` se rellena con ceros.
- **VW (DT 3)**: el campo VCH es directamente una trama IMBE de 18 bytes por bloque; se codifica y descodifica con el
  vocoder IMBE del overlay P25. Sólo se carga el código/tablas del overlay, nunca su BSS: `ckcpu.ld` la comparte con
  `.ysf_rx`, así que borrarla destruiría el demodulador y la cola PCM en mitad de la llamada. El estado IMBE vive en
  `.bss` normal; al terminar el frame se devuelve el código AMBE al overlay.
- **Data FR (DT 1)** no lleva voz.

La secuencia de TX construye cabecera, tramas de comunicación y terminador, con CSD en la cabecera y `ysf_tx_frame_build()`;
las tramas terminan en la FIFO 4FSK de `p25_phy.c`.

## Recepción y transmisión en el firmware

- RX: `afrx.c` graba el discriminador a 8 kHz y `ysf_demod.c` (derivado de `p25_demod.c`) busca la sync de 20
  símbolos, con fit de ganancia/DC y ecualizador de 3 taps. Emite dibits alineados al primer símbolo de la sync.
  `ysf_radio.c` los coloca en un anillo de 480, comprueba la sync con `ysf_sync_errors()` y descodifica FICH/DCH.
- TX: el micrófono se codifica en el modo del canal (AMBE+2 para V/D-1 y DN, IMBE para VW; 5 bloques por trama) y se
  envía por la FIFO 4FSK compartida con P25, con el escalado de desviación propio de YSF. RX elige el codec por el DT
  recibido (incluso si el canal transmite otro modo) y restaura el camino de TX al terminar la llamada.
- El estado del receptor (`ysf_demod`, buffers, PCM) vive en `.ysf_bss`, mapeado por `ckcpu.ld` a `.ysf_rx` en
  `__vocoder_ram_end`, compartiendo dirección con NXDN y el receptor DMR en software bajo `OVERLAY_AMBE`.

## Trabajo realizado

- Nuevo modo de canal, editor de DG-ID / SSID / YSF Mode, integración en pantallas (VFO, canal, TX, QSO), escaneo y
  documentación del manual (en español e inglés).
- Capa de trama (sync, FICH con CRC + Golay + convolución + interleave, DCH por modo), voice (AMBE+2 V/D-1 y DN,
  IMBE en VW) y secuenciador de TX (cabecera, ciclos y terminador, preámbulo `0x77`).
- Demodulador C4FM propio derivado del receptor P25: búsqueda de sync, ganancia/DC, ecualizador y salida de dibits.
- Encaje en el firmware: `Makefile` y `ckcpu.ld` (RAM de overlays), `afrx`/`radio_rx`/`radio_tx`, `dmr_soft`,
  `vocoder`, codeplug y last-heard; el camino de voz se restaura al terminar cada llamada.
- Correcciones incluidas: destino `ALL` en el aire y en pantalla, preámbulo antes del primer frame, prevención de
  bucles con sync inválida y `strlen()` sin límite en CSD, DG-ID 0/127 y SC desde el primer frame, mapa de bits b3/b4
  del AMBE, mantenimiento de las mitades del micrófono entre ciclos, SSID por canal y selección de modo + codec
  cruzado en RX.
- El firmware compila y enlaza con el toolchain C-SKY. En este fork se incluye también el arnés de pruebas de host
  `tools/ysf/` (frame, TX, voz, demodulador y un vector DN capturado en el aire), con la misma licencia GPL de la
  parte YSF. `python3 tools/ysf/run.py` da 5582 comprobaciones y `GCC=gcc python3 tools/radio_fsm/run.py` 163, sin
  fallos.
- **Sonidos** (commits de sonido y roger incorporados en este fork): roger beep **por el aire** como tramas AMBE
  nativas, con las filas **DMR air beep** (Off/Start/Stop/Both) y **Roger style** (seis melodías); y **Sound style**
  local (Classic/Chirp) para los beeps del altavoz. Se configuran en Menú → Sound options.

## Verificación en aire (fork)

Pruebas hechas con el handy propio, no por el proyecto original:

- **Yaesu reales en simplex directo:** conversación verificada contra un **FT-70D** y un **FT-5D** sin hotspot ni
  reflector, en los **tres modos de voz**: V/D-1, DN y VW (IMBE). Los tres abren y suenan en ambos sentidos; **DN es
  el que mejor anduvo** (voz más estable y sin cortes). También se probó la selección cruzada de codec dentro del
  handy.
- **PTT real:** el 26/11/2026 a las 14:07:51 UTC `LU8HPA` (SSID 0) entró al reflector a través del hotspot
  `LU8HPA B / URFARG`; el dashboard lo mostró con el camino completo de voz. El DT1 del FN6 no hizo falta: nuestro
  TX manda ceros ahí y MMDVMHost no interpreta ese campo (sólo lo descarta en `YSFPayload.cpp`).
- **Retransmisión en reflector:** 16 tramas DN generadas por `ysf_tx.c` (voz AMBE de silencio) enviadas como
  `OPENDMR` a `bellville-link.tplinkdns.com:42000` (URF/XLXARG) se reenviaron a un segundo socket y se recibieron en
  otro handy. El reflector reescribe tag/FICH/CSD antes de reenviar, así que las tramas recibidas no son
  byte-idénticas a las generadas.
- **Cruce YSF→DMR del Pi-Star (`ysf2dmr`):** la misma transmisión entró por YSF al reflector y salió por DMR en el
  otro equipo.
- **SSID y hotspot:** con `[System Fusion] SelfOnly=1` o un callsign con sufijo en el Pi-Star, las fuentes con `-N`
  se rechazan (`invalid access attempt`) aunque el SSID del radio sea válido. Con SSID 0 entra; para usar
  `-1`…`-15` hay que dejar el callsign del hotspot sin sufijo y `SelfOnly=0`.

## Límites

No hay soporte para cifrado, trunking, datos YSF (DT 1 sólo se ignora) ni base de datos de callsigns. La semántica de
los 49 parámetros AMBE+2 en DN no está confirmada contra un vector público; la correspondencia de bits se sigue de la
referencia indicada abajo. La interoperabilidad con radios Yaesu reales (FT-70D y FT-5D, simplex directo, los tres
modos de voz) sí quedó verificada en esta rama, con DN como el modo más estable; también se probaron el hotspot
Pi-Star y el reflector URF. No lo verificó el proyecto original, y quedan por probar otras radios y redes.

Para un hotspot MMDVM/Pi-Star externo en YSF: el TX ya emite el preámbulo `0x77` de 200 ms y la trama sigue el formato
de MMDVM (sync, whitening, FICH y DCH). Hay que igualar la desviación: un canal **narrow** de 12.5 kHz (±1350 Hz)
necesita `LoDev=1` en MMDVMHost, si el hotspot está configurado para YSF **wide** (±2700 Hz). El offset de frecuencia
del hotspot también es crítico: en narrow los niveles interiores son sólo ±450 Hz. El escalado de desviación del
firmware asume que `MODEM_MOD_SCALE_ONE = 1024` es ±1800 Hz; si el módulo del radio da ±1944 Hz, los valores actuales
quedan ~8 % altos (medir con un discriminador antes de cambiar las constantes).

El submódulo `vocoder` apunta al commit `50ed497`, que corrige los bits finales b3/b4 del AMBE. Ese commit está
publicado en la rama `fix/ambe-b3b4-tails` del fork
[Pablog5395/dmr-vocoder](https://github.com/Pablog5395/dmr-vocoder/tree/fix/ambe-b3b4-tails), y `.gitmodules` apunta
a ese fork para que la rama se pueda compilar completa. Cuando el cambio se integre en
[va3bpf/dmr-vocoder](https://github.com/va3bpf/dmr-vocoder), conviene volver a apuntar el submódulo al repositorio
original; el contenido del commit es el mismo en ambos.

## Procedencia, créditos y licencias

Código fuente del que salió esta implementación, y referencias de lectura:

- **[DM32-nxdn-p25](https://github.com/donttellmeimcute/DM32-nxdn-p25)** — lyra XE1POC, GPL-3.0-or-later. Prototipo
  de host del que se portaron la capa de trama, el secuenciador de TX y la conversión de voz YSF (archivos
  `src/ysf/ysf_frame.*`, `ysf_tx.*`, `ysf_voice.*`). Los avisos de autoría y licencia se conservan en las cabeceras de
  los archivos portados.
- **[MMDVM-Host](https://github.com/g4klx/MMDVM-Host)** — Jonathan Naylor G4KLX, GPL-2.0-or-later según las
  cabeceras de sus fuentes. Referencia de lectura para la sync, el formato del FICH, las cadenas FEC del DCH, la
  codificación convolucional, el CRC y las tablas de interleave (`YSFFICH.cpp`, `YSFPayload.cpp`,
  `YSFConvolution.cpp`, `CRC.cpp`, `Golay24128.cpp`, `AMBEFEC.cpp`). Los algoritmos se reimplementaron en C para el
  firmware; no se copiaron archivos de ese repositorio. El arnés `tools/ysf/` genera sus propios vectores con su
  codificador, salvo el vector DN que se capturó en el aire.
- **Yaesu, *Amateur Radio Digital Standards*, Rev. 1.02** — referencia de lectura del protocolo YSF (figuras 4-1,
  4-3, 4-4 y 4-32), en especial para la secuencia de tramas y el mapa de bits de voz DN.
- **[dmr-vocoder](https://github.com/Pablog5395/dmr-vocoder/tree/fix/ambe-b3b4-tails)** — fork del submódulo original
  [va3bpf/dmr-vocoder](https://github.com/va3bpf/dmr-vocoder), base del codec AMBE+2 y del overlay IMBE; contiene el
  cambio de b3/b4 documentado más arriba.
- La capa física, el integrador de radio y el demodulador derivado de `p25_demod.c` siguen las licencias y avisos de
  OpenDMR (Junqiang Shi, VA3BPF / BG8AM) y de [p25-core](https://github.com/va3bpf/p25-core).

Los archivos portados conservan `SPDX-License-Identifier: GPL-3.0-or-later` y el copyright de lyra XE1POC; el resto
mantiene la licencia de cada archivo en el árbol de OpenDMR. El desglose por archivo, los textos completos y la nota
de compatibilidad están en [`LICENSES-YSF.md`](../LICENSES-YSF.md).
