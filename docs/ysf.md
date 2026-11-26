# YSF (Fusion / C4FM) en OpenDMR

Esta página resume la implementación de YSF6148 (Yaesu Fusion, C4FM) en la rama `feature/ysf-mode`. El código del
protocolo viene del fork [DM32-nxdn-p25](https://github.com/) (lyra XE1POC, GPL-3.0-or-later); la capa física y la
integración con la radio son las de OpenDMR.

## Modo y canal

- `CODEPLUG_MODE_YSF = 8`, etiqueta de canal opendmr `0xA7`. El editor de canal muestra la fila de **DG-ID** donde
  otros modos muestran el colour code / NAC / RAN / CAN: el byte se comparte en la estructura del codeplug.
- Ciclo de la tecla de modo: FM → AM → USB → LSB → DMR → P25 → NXDN → M17 → **YSF**.
- DG-ID 0 – 127 en el editor (un Yaesu usa 0 – 99; 0 = «recibir todo», transmitir 0). Se envía en el FICH (`SC`) de
  cada trama; en recepción actúa como un squelch digital. Con un DG-ID seleccionado también se abren las tramas con
  `SC = 0` (sin DG-ID) y `SC = 127` (Break Out), como en un Yaesu.
- El destino de TX es el nombre del contacto (o `ALL`); la fuente es el indicativo (callsign) de «General» en el CPS.
  Elegir **All Call** con `#` envía `ALL` (no el texto traducido de la UI).
- El panel QSO muestra el callsign en grande, el destino sobre el S-meter y el DG-ID en la barra de estado. El
  escaneo usa `SCAN_YSF` (mismo `heard_within()` que NXDN).

## Teclas (operación)

| Acción | Tecla |
|---|---|
| Llegar al modo YSF desde la pantalla principal | `SK1 + *` hasta `YSF` (orden: FM → AM → USB → LSB → DMR → P25 → NXDN → M17 → YSF) |
| Editar el DG-ID del canal | Menú → detalles del canal → fila **DG-ID** (donde otros modos ponen el colour code): `▲ ▼` elige la fila, `◀ ▶` o el knob cambia de 1 en 1, `menú` guarda, `volver` descarta |
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

La voz es AMBE+2 de 9 bytes por bloque, el mismo formato que DMR en este firmware:

- **V/D-1 (V/D Mode 1)**: el campo VCH son 9 bytes de AMBE directos, cinco por trama.
- **DN (V/D-2)**: el campo de 13 bytes se convierte a 9 bytes de AMBE con `ysf_voice_dn_to_frame()` y viceversa.
- **VW** no se soporta: necesita IMBE de 55 bits y este firmware sólo tiene el vocoder AMBE+2 de 9 bytes.

La secuencia de TX construye cabecera, tramas de comunicación y terminador, con CSD en la cabecera y `ysf_tx_frame_build()`;
las tramas terminan en la FIFO 4FSK de `p25_phy.c`.

## Recepción y transmisión en el firmware

- RX: `afrx.c` graba el discriminador a 8 kHz y `ysf_demod.c` (derivado de `p25_demod.c`) busca la sync de 20
  símbolos, con fit de ganancia/DC y ecualizador de 3 taps. Emite dibits alineados al primer símbolo de la sync.
  `ysf_radio.c` los coloca en un anillo de 480, comprueba la sync con `ysf_sync_errors()` y descodifica FICH/DCH.
- TX: el micrófono se codifica en AMBE+2 (5 bloques por trama) y se envía por la FIFO 4FSK compartida con P25, con
  el escalado de desviación propio de YSF.
- El estado del receptor (`ysf_demod`, buffers, PCM) vive en `.ysf_bss`, mapeado por `ckcpu.ld` a `.ysf_rx` en
  `__vocoder_ram_end`, compartiendo dirección con NXDN y el receptor DMR en software bajo `OVERLAY_AMBE`.

## Verificación

- `python3 tools/ysf/run.py`: pruebas host de frame, secuencia TX, voz y demodulador C4FM (modulación RRC → samples →
  dibits → tramas).
- `GCC=gcc python3 tools/radio_fsm/run.py`: regresión de la máquina de estados de radio.
- `make -C src CROSS=<toolchain>`: firmware compilado y enlazado; el _target_ final `.elf.asm` falla en este entorno
  porque el `objdump` del toolchain no encuentra `libdebuginfod.so.1` (ajeno al cambio).

## Límites

No hay soporte para VW/IMBE, cifrado, trunking, datos YSF ni base de datos de callsigns. El modo hotspot MMDVM sigue
siendo sólo DMR. La recepción/transmisión YSF no ha sido probada en el aire todavía.

Para un hotspot MMDVM/Pi-Star externo en YSF: el TX ya emite el preámbulo `0x77` de 200 ms y la trama sigue el formato
de MMDVM (sync, whitening, FICH y DCH). Hay que igualar la desviación: un canal **narrow** de 12.5 kHz (±1350 Hz)
necesita `LoDev=1` en MMDVMHost, si el hotspot está configurado para YSF **wide** (±2700 Hz). El offset de frecuencia
del hotspot también es crítico: en narrow los niveles interiores son sólo ±450 Hz. El escalado de desviación del
firmware asume que `MODEM_MOD_SCALE_ONE = 1024` es ±1800 Hz; si el modulo del radio da ±1944 Hz, los valores actuales
quedan ~8% altos (medir con un discriminador antes de cambiar las constantes).
