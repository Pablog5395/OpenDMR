# Estructura del fork OpenDMR-YSF

Este fork añade YSF (C4FM) y el roger beep por el aire sobre OpenDMR. El objetivo de esta
estructura es que el código propio viva en carpetas aisladas y que, al actualizar desde
`va3bpf/OpenDMR`, los conflictos se limiten a unos pocos puntos de enganche.

## Carpetas propias

| Carpeta | Contenido | Licencia |
|---|---|---|
| `src/services/ysf/` | Pila YSF completa: `ysf_frame`, `ysf_radio`, `ysf_tx`, `ysf_voice`, `ysf_demod` | GPL-3.0-or-later (excepto `ysf_demod.c/.h`, que es de OpenDMR) |
| `src/services/roger/` | Roger beep DMR por el aire: patrones, tramas AMBE nativas y estado del generador | Licencia de OpenDMR |
| `tools/ysf/` | Arnés de pruebas de host del YSF | GPL-3.0-or-later |
| `docs/ysf.md` | Documentación de YSF y de las pruebas en aire | — |

El roger beep **no** incluye los estilos de sonido locales (Classic / Chirp): esos siguen en
`src/services/sound.c` y `src/app/sound_options.c`, porque son parte del motor de beeps del
speaker y su delta es pequeño.

## Puntos de enganche (lo que sí toca archivos compartidos)

Estos archivos contienen como máximo unas pocas líneas del fork: una llamada, un campo o una
fila de menú. Si un merge de upstream falla, mirar primero aquí.

### YSF

- `src/services/codeplug.h` / `codeplug.c`: modo `CODEPLUG_MODE_YSF`, DG-ID, SSID y modo de voz.
- `src/app/channel_details.c`: filas SSID, YSF Mode y DG-ID.
- `src/app/channel_screen.c`, `vfo_screen.c`, `qso_panel.c`, `scan.c`, `tx_screen.c`, `tx_target.c`: pantallas y llamada.
- `src/services/radio_rx.c`, `radio_tx.c`, `dmr/codec.c`, `dmr/dmr.c`, `dmr/hr_c7000.c`,
  `services/dmr_soft.c`: despacho de recepción/transmisión.
- `src/ui/text/text.h`, `text_en.c`, `text_keys.c` y `tools/text/languages/en.json`: textos.
- `src/Makefile`, `src/ckcpu.ld`: inclusión de fuentes y overlay del vocoder.

### Roger beep

- `src/services/roger/roger.c/.h`: todo el generador.
- `src/dmr/codec.c` y `src/services/dmr_soft.c`: `roger_tone()` en lugar de `vocoderEncodeFrame()`.
- `src/dmr/hr_c7000.c`: `roger_request(ROGER_EVENT_TX_START)` desde la ISR del timeslot.
- `src/services/radio_tx.c`: `roger_begin()` / `roger_active()` / `roger_cancel()` y la espera del PTT.
- `src/app/sound_options.c`: las dos filas del menú.
- `src/services/sound.c/.h`: carga/guardado (`roger_load()`, `roger_save()`) y los estilos locales.

## Regla para cambios nuevos

1. La lógica nueva va en `src/services/ysf/` o `src/services/roger/`.
2. Un archivo compartido solo recibe la llamada de enganche; nunca la implementación.
3. Cada feature nuevo empieza en su propia rama y se prueba con los tests de host antes de
   entrar en la rama de integración.

## Ramas y actualización desde upstream

```
upstream        espejo de origin/develop (no se commitea)
feature/ysf     pila YSF y su integración
feature/roger   roger beep por el aire
feature/sound   estilos de sonido locales
fork/main       solo merges de las features; es la rama publicada
```

Actualizar:

```sh
git fetch origin
git checkout feature/ysf   && git rebase origin/develop
git checkout feature/roger && git rebase origin/develop
git checkout feature/sound && git rebase origin/develop
git checkout fork/main
git merge --no-ff feature/ysf feature/roger feature/sound
git push --force-with-lease fork feature/ysf feature/roger feature/sound
git push fork fork/main
```

Activar `git rerere` ayuda a resolver los mismos conflictos de los puntos de enganche en cada
actualización.

## Releases del fork

Nombre de tag: `v<base>-ysf.<n>` (por ejemplo `v0.9.3-ysf.1`).

Cada release publica:

- `OpenDMR-ysf-gpl-<version>.zip`: firmware, CPS, herramientas, `LICENSES-YSF.md` y textos de licencia.
- `SHA256SUMS.txt`.
- Código fuente en el tag (obligatorio para la parte GPL).
- Notas: base upstream, cambios del fork, pruebas en aire y limitaciones.

Herramienta:

```sh
python3 tools/release/make_release.py --version 0.9.3-ysf.1 --repo Pablog5395/OpenDMR
```

Mantener `dist/` fuera de git (ya está en `.gitignore`): los artefactos van como *Release* de
GitHub, no como commits.

## Verificación antes de publicar

```sh
python3 tools/ysf/run.py            # YSF: frame, TX, voz y demodulador
GCC=gcc python3 tools/radio_fsm/run.py
make -C src CROSS=...               # build del firmware
```
