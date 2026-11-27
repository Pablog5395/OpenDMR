# Licencias de la parte YSF (rama `ysf-gpl-0.9.3`)

Esta rama es un **fork personal y experimental** de OpenDMR que añade YSF (Yaesu Fusion / C4FM).
Se publica únicamente desde el fork [`Pablog5395/OpenDMR`](https://github.com/Pablog5395/OpenDMR),
**independientemente de [`va3bpf/OpenDMR`](https://github.com/va3bpf/OpenDMR)**. No es una
contribución oficial del proyecto original.

Cada archivo conserva su licencia en la cabecera. Este documento resume las licencias de la parte
YSF y de los componentes que la acompañan; no sustituye a los textos completos.

---

## 1. Código YSF portado — GPL-3.0-or-later

Los siguientes archivos llevan `SPDX-License-Identifier: GPL-3.0-or-later` y el copyright de su
autor original:

| Archivo | Origen |
|---|---|
| `src/services/ysf/ysf_frame.c/.h` | Portado de [DM32-nxdn-p25](https://github.com/donttellmeimcute/DM32-nxdn-p25), lyra XE1POC, GPL-3.0-or-later |
| `src/services/ysf/ysf_tx.c/.h` | Idem (secuenciador de TX) |
| `src/services/ysf/ysf_voice.c/.h` | Idem (conversión de voz AMBE+2) |
| `src/services/ysf/ysf_radio.c/.h` | Idem + integración con la radio de OpenDMR |
| `tools/ysf/run.py`, `tools/ysf/test_ysf.c`, `tools/ysf/test_ysf_demod.c` | Arnés de pruebas de host del mismo origen / del fork |

El texto completo de la GPL v3 está en [`licences/GPL-3.0-or-later.txt`](licences/GPL-3.0-or-later.txt).
Los avisos de autoría y procedencia de las cabeceras se mantienen intactos.

### Referencias de lectura (sin código copiado)

- [MMDVM-Host](https://github.com/g4klx/MMDVM-Host) — Jonathan Naylor G4KLX, GPL-2.0-or-later
  según sus cabeceras. Se usó como referencia para la sync, FICH, FEC, CRC, interleave y
  AMBEFEC; los algoritmos se reimplementaron. **No se distribuyen archivos de ese repositorio.**
- Yaesu, *Amateur Radio Digital Standards*, Rev. 1.02 — referencia del protocolo YSF.

---

## 2. Resto de OpenDMR — licencia de OpenDMR

Los archivos que no son de la parte YSF (por ejemplo `src/services/ysf/ysf_demod.c/.h`, derivado de
`p25_demod.c`, y el cambio de mapa de símbolos en `src/services/p25_phy.c/.h`) mantienen la
licencia de OpenDMR, con su cláusula 4 de uso no comercial. Ver [`LICENSE`](LICENSE) y
[`NOTICE`](NOTICE).

Componentes de terceros ya presentes en OpenDMR:

- FreeRTOS (`src/freertos/`): licencia MIT.
- Fuentes (`tools/fonts/ttf/`): SIL Open Font License 1.1.
- Código portado de RadioFoundry: MIT (ver `NOTICE`).

---

## 3. Submódulos

| Submódulo | Repositorio | Licencia |
|---|---|---|
| `vocoder/` | [Pablog5395/dmr-vocoder](https://github.com/Pablog5395/dmr-vocoder) (fork de `va3bpf/dmr-vocoder`) | GPL-3.0-or-later (ver `vocoder/LICENSE`) |
| `p25/` | [va3bpf/p25-core](https://github.com/va3bpf/p25-core) | GPL-3.0-or-later (ver `p25/LICENSE`) |

El submódulo `vocoder` apunta al commit `50ed497` del fork `Pablog5395/dmr-vocoder`, que corrige
los bits finales b3/b4 del AMBE. El mismo cambio está propuesto como PR separado en
[`va3bpf/dmr-vocoder`](https://github.com/va3bpf/dmr-vocoder). `.gitmodules` apunta al fork para
que la rama se pueda clonar y compilar completa.

---

## 4. Compatibilidad entre licencias

La GPL-3.0-or-later **no permite añadir restricciones adicionales** (como la cláusula no
comercial de OpenDMR) al código que se distribuye. La GPL y la licencia de OpenDMR no son
compatibles para una **distribución combinada** del firmware. Por ese motivo:

- esta rama se publica de forma separada del repositorio original y no se presenta como un PR
  para fusionar en `va3bpf/OpenDMR`;
- si redistribuyes el binario o el código, conserva los avisos y los textos de licencia de cada
  parte y no impongas restricciones adicionales sobre los archivos GPL;
- consulta a un profesional del derecho si necesitas una opinión formal. Este documento es un
  resumen técnico de procedencia, no asesoría legal.

---

## English summary

This is a personal, experimental fork of OpenDMR published only from `Pablog5395/OpenDMR`,
independently of `va3bpf/OpenDMR`. The YSF files (`src/services/ysf/*`, `tools/ysf/*`) are
GPL-3.0-or-later, ported from DM32-nxdn-p25 (lyra XE1POC); see
`licences/GPL-3.0-or-later.txt`. MMDVM-Host and the Yaesu standard were reading references only,
with no files copied. The rest of the firmware keeps the OpenDMR licence, including its
non-commercial clause (`LICENSE`). The GPL and the OpenDMR licence are not compatible for a
combined distribution, so this branch is published outside the upstream project.
