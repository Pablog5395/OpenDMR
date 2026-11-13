# OpenDMR — Baofeng DM-32 / DM-32UV

## Contenido

| Archivo | Para qué sirve |
|---|---|
| `opendmr.bin` | Firmware para flashear con OpenDMR ya corriendo (radio encendida, cable puesto) |
| `opendmr_BFUV32.bin` | Firmware para modo bootloader (PTT + SK1 al encender) |
| `fw_update.py` | Actualiza por datos (radio encendida, ~35 s) |
| `dm32_dump.py` | Backup/escritura por bootloader (radio apagada → encender en 30 s) |
| `write_keps.py` | Escribe satélites amateur al codeplug (OpenGD77 header necesario) |
| `manual.en.md` / `manual.es.md` | Manual de usuario |
| `installation.en.md` / `installation.es.md` | Guía de instalación |
| `README.md` / `README.en.md` | Instrucciones rápidas |
| `version.txt` | Commit del código y fecha |

## ¿Qué tiene esta build?

- **GPS rx**: recibe la posición (ETSI GPS Info) del que transmite y muestra **distancia y rumbo** desde ti en la pantalla de recepción, y la guarda en *Last heard*.
- **Español** como idioma 2 (inglés 0 / chino 1 / español 2).
- **P25** (modo `SK1 + *`, ciclo FM→AM→USB→LSB→DMR→P25).
- **APRS beacon now** (`SK2 + 2` o menú *Options → APRS → Send beacon now*) y marca **@** en el panel inferior cuando el beacon está activo.

> ⚠️ El driver de GPS aún no está escrito, así que la radio **no obtiene su posición sola**. Pásala a mano en *Radio info → Location* si querés ver distancia + rumbo (sin tu posición, muestra solo las coordenadas del que transmite).

## Cómo flashear

### Si la radio ya corre OpenDMR (radio encendida, cable puesto):

```powershell
py -u fw_update.py opendmr.bin --port COM3
```

### Si no arranca o no hay OpenDMR:

Apagá la radio, mantené **PTT + SK1** (tecla lateral bajo el PTT) y encendé (LED verde). Luego:

```powershell
py -u dm32_dump.py COM3 program opendmr_BFUV32.bin --write --start 0xC000
```

## Cómo probar

1. Menú → *Options → Language* → cicla hasta **Español**.
2. En una recepción DMR con otro handy que envíe GPS, verás distancia y rumbo desde ti (ej. `12.3 km NE`).
3. En *Last heard* queda guardada la posición y el TG del que llamó.
4. P25: **SK1 + `*`** para cambiar de modo.
5. APRS: en un canal FM con `aprs_config_index`, **SK2 + 2** envía baliza.

**SHA256:** usá `certutil -hashfile opendmr.bin SHA256` en Windows para confirmar que el archivo bajó bien.
