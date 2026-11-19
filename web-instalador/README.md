# Web instalador de OpenDMR (estática, sin backend)

Página en español para copiar y grabar la radio directamente desde el navegador. No necesita PHP ni base de datos: es HTML/CSS/JS y archivos estáticos.

La portada reúne el instalador web, el acceso al CPS y la herramienta APRS/DMR con el gateway `222999`. El ZIP, la guía y los `.bat` siguen generándose en `public/descargas/` sin enlace en la portada.

## Qué incluye

- **En la portada:** grabación por Web Serial (copia de seguridad, instalación, instalación del fork va3bpf, restauración y firmware de fábrica), sin Python ni `.bat`, botón «Abrir CPS online» y enlaces a los repositorios de GitHub (proyecto original y fork).
- **Publicado pero sin enlace en la portada:** el ZIP `OpenDMR-0.5.0-lu8hpa-ES.zip`, los paquetes de las releases originales `OpenDMR-0.6.1.zip` y `OpenDMR-0.6.0.zip`, la guía en texto y HTML, los cuatro `.bat`, `sha256.txt` y los `.bin` que usa el grabador. El CPS v0.9.1 (0.9.0 con el APRS del ISS en 437.825 MHz; página de Temas, interfaz en español y elección del idioma del menú de la radio, además de NAC/RAN de P25/NXDN, airband, radio IDs por CSV y borrado múltiple) sí se abre desde el botón de la portada.
- **En la portada, APRS/DMR:** herramienta para cargar por Web Serial el gateway APRS (por defecto `222999`) y «Send via» (`AFSK` / `DMR` / `Ambos`). Usa los comandos del firmware (`D g` / `D w`, en `app/test_link.c`) para grabar solo esos valores, sin reescribir el sector `0x6000` ni tocar canales, ID DMR, PIN ni tema; verifica leyendo el bloque de opciones en `0x6140` y reinicia la radio.

## 1. Preparar los archivos

Desde la raíz del repo:

```bash
chmod +x web-instalador/deploy.sh
./web-instalador/deploy.sh
```

Eso crea `web-instalador/public/` con:

```
public/
├── index.html
├── index.php          # solo redirige a index.html en hostings que priorizan PHP
├── styles.css
├── app.js
├── flasher.js         # protocolo Web Serial (copia / instalación / restauración)
├── .htaccess
└── descargas/
    ├── OpenDMR-0.5.0-lu8hpa-ES.zip
    ├── OpenDMR-0.6.1.zip            # release original (va3bpf v0.6.1)
    ├── OpenDMR-0.6.0.zip            # release original anterior
    ├── LEEME-PRINCIPIANTE.txt
    ├── opendmr-cps.html
    ├── SHA256SUMS.txt
    ├── sha256.txt              # lo lee app.js para verificar el ZIP
    ├── bat/
    │   ├── 1-RESPALDO.bat
    │   ├── 2-INSTALAR.bat
    │   ├── 3-ACTUALIZAR.bat
    │   └── 4-VOLVER-ORIGINAL.bat
    └── firmwares/
        ├── dumper.bin           # lector que se carga en la RAM de la radio
        ├── opendmr.bin          # se saca del ZIP
        ├── cjk.bin              # se saca del ZIP
        └── sha256.txt           # lo lee flasher.js antes de grabar
```

Si quieres subirlo en el mismo paso:

```bash
./web-instalador/deploy.sh usuario@IP_DEL_VPS:/var/www/opendmr
```

## 2. DNS del subdominio

En tu proveedor de DNS crea un registro **A** (o AAAA si usas IPv6):

| Tipo | Nombre | Valor |
|---|---|---|
| A | `instala` (o `opendmr`) | IP pública del VPS |

Espera a que resuelva: `dig instala.tudominio.com +short`.

## 3. Servidor web (nginx)

Instala nginx y certbot:

```bash
sudo apt update
sudo apt install -y nginx certbot python3-certbot-nginx
sudo mkdir -p /var/www/opendmr
sudo chown -R "$USER":www-data /var/www/opendmr
```

Copia `nginx.conf.example` a `/etc/nginx/sites-available/opendmr`, cambia `instala.tudominio.com` por tu subdominio y activa:

```bash
sudo ln -s /etc/nginx/sites-available/opendmr /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

Certificado HTTPS (**obligatorio** para Web Serial, tanto para el CPS como para grabar):

```bash
sudo certbot --nginx -d instala.tudominio.com
```

Con Apache, en vez de lo anterior, activa `mod_ssl` y usa un `VirtualHost` con `DocumentRoot /var/www/opendmr`; pide el certificado con `sudo certbot --apache -d instala.tudominio.com`.

## 4. Servidor PHP / Apache (cPanel, Plesk, hosting compartido)

**Sí sirve.** No hace falta instalar PHP ni escribir código PHP: Apache sirve los `.html`, `.css` y `.js` directamente aunque el servidor tenga PHP activado.

1. En el panel de hosting, crea el subdominio y anota su **DocumentRoot**. Suelen ser:
   - cPanel: `/home/USUARIO/public_html/instala/` o `/home/USUARIO/domains/instala.tudominio.com/public_html/`
   - Plesk: `/var/www/vhosts/tudominio.com/instala.tudominio.com/httpdocs/`
2. Ejecuta `deploy.sh` localmente y sube **el contenido** de `web-instalador/public/` a esa carpeta (no la carpeta `public` dentro):
   ```bash
   ./web-instalador/deploy.sh
   # sube con FileZilla, o:
   rsync -avz web-instalador/public/ usuario@IP:/home/USUARIO/public_html/instala/
   ```
3. El `.htaccess` incluido hace que el ZIP, los `.bat` y los `.bin` se descarguen como archivos y no se muestren en el navegador. Si el hosting no permite `Options -Indexes` y da error 500, elimina esa línea del `.htaccess`.
4. Activa **SSL/HTTPS** para el subdominio (en cPanel: *AutoSSL*; si no, *Let's Encrypt*). Sin HTTPS, ni el CPS ni los botones de grabación pueden usar el puerto serie.
5. Si usas Cloudflare delante, pon el modo SSL en **Full** y purga la caché la primera vez.

Si el subdominio ya tiene un `index.php` (por ejemplo un WordPress), reemplázalo por el `index.php` incluido, que solo redirige a `index.html`.

## 5. Grabar desde el navegador

La sección **“Grabar desde el navegador (sin instalar nada)”** usa Web Serial: el propio Chrome/Edge habla con el cable y baja el firmware de tu servidor. No se instala Python, no hay que descomprimir nada y no se sube nada al servidor.

| Botón | Qué hace | Igual que |
|---|---|---|
| 1 · Hacer copia de seguridad | Lee dos veces la memoria de programa y dos veces la de datos; calcula el SHA-256 de cada lectura y dice si coinciden; ofrece los cuatro `.bin` para guardar. | `1-RESPALDO.bat` |
| 2 · Instalar OpenDMR | Graba `opendmr.bin` en `0xC000` y `cjk.bin` en `0x280000`. Pide confirmación previa de que ya se guardó la copia. | `2-INSTALAR.bat` |
| 3 · Volver al original | Restaura los `.bin` que elijas de tu copia; deja el bootloader intacto. | `4-VOLVER-ORIGINAL.bat` |

Puntos importantes:

- **Chrome o Edge, con HTTPS.** En Firefox o por `http://` los botones aparecen desactivados.
- El navegador **no enumera puertos**: al pulsar un botón abre un selector y el usuario elige su cable. Si no aparece, instalar el driver CH340 y volver a conectar.
- Cada operación pide apagar la radio, pulsar el botón y **encenderla sin tocar teclas en 30 s** (pantalla negra y LED apagado mientras lee). Entre lecturas hay que apagarla de nuevo.
- La copia tarda **30–40 min** con batería cargada. La memoria de datos contiene la calibración única de cada radio: sin copia no se puede volver atrás.
- Los `.bin` generados son **idénticos a los de los `.bat`** (se leen desde la dirección 0), así que se pueden mezclar ambos métodos.
- Antes de grabar, `flasher.js` comprueba el SHA-256 de `opendmr.bin`, `cjk.bin` y `dumper.bin` contra `descargas/firmwares/sha256.txt`. Si un archivo está corrupto, se niega a escribir.
- La grabación **no sustituye al CPS**: el archivo de canales se sigue haciendo con `opendmr-cps.html`.
- **Actualizar** una radio que ya tiene OpenDMR se sigue haciendo con `fw_update.py` / `3-ACTUALIZAR.bat` (radio encendida). El grabador web cubre copia, primera instalación y restauración.

### Probar antes de enviárselo

Aquí **no hay radio** para validar el handshake ni los tiempos del puerto serie. Antes de dar la web por buena, pruébala tú con una DM-32:

1. Sube `public/` y abre la web por HTTPS en Chrome/Edge.
2. Pulsa **1 · Hacer copia de seguridad** y elige el puerto. Comprueba que aparece el aviso de encender la radio y que el registro avanza.
3. Al terminar, verifica que el log diga **“SON IGUALES”** para `program` y `data` y que se descargan cuatro `.bin`.
4. Sin borrar esa copia, pulsa **2 · Instalar OpenDMR**. Debe terminar con “Instalación terminada” y la radio debe arrancar en OpenDMR.
5. Comprueba el CPS y que el idioma se puede poner en *Espanol*.

Si algo falla, el registro de la sección **“Ver registro de la operación”** contiene el mensaje exacto. Los `.bat` siguen disponibles como alternativa.

## 6. Subir y probar

```bash
./web-instalador/deploy.sh usuario@IP_DEL_VPS:/var/www/opendmr
```

O, si ya está preparado: `rsync -avz web-instalador/public/ usuario@IP:/var/www/opendmr/`.

Comprueba en el navegador:

1. `https://instala.tudominio.com` carga la página.
2. El botón **Descargar paquete ZIP** baja el archivo (~5,5 MB).
3. **Abrir CPS online** abre el CPS en otra pestaña, sin error de certificado.
4. La sección de grabación dice **“Web Serial listo”** en Chrome/Edge por HTTPS.
5. Escribe `COM5` en el generador y descarga un `.bat`: abre el archivo con el Bloque de notas y comprueba que dice `set "PORT=COM5"`.
6. Selecciona el ZIP en **Verificar la descarga** y debe decir “Correcto”.

## 7. Actualizaciones

Cuando haya un ZIP nuevo:

1. Pon el ZIP nuevo en `dist/` y actualiza sus copias en `dist/ES/`.
2. Cambia el nombre del ZIP en `web-instalador/index.html`, en `app.js` (constante `HASH_FALLBACK`) y en `web-instalador/deploy.sh` (`ZIP_NAME`).
3. Vuelve a ejecutar `./web-instalador/deploy.sh …` con rsync.

El `sha256.txt` de `descargas/` y el de `descargas/firmwares/` se regeneran solos.

## 8. APRS por satélite (ISS)

La v0.6.0 y la 0.6.1 saben mandar una baliza APRS por el digipeater del satélite: en la pantalla del pase, con la
frecuencia de tipo **APRS**, el **PTT no transmite voz, manda una baliza** con la configuración APRS **cuyo nombre es el
del satélite** (`aprs.c`, `send_satellite_beacon()`).

### Frecuencia del ISS (cambió)

ARISS movió el packet del ISS de 145.825 a **437.825 MHz** (70 cm, simplex, 1200 bps, `RS0ISS`, path **`ARISS`**;
también aceptan `APRSAT` y `RS0ISS`). El preset del CPS 0.9.0 seguía en 145.825: la copia que publica esta web es la
**0.9.1**, que es la 0.9.0 con ese preset corregido.

### Qué tiene que tener la radio

| Cosa | Dónde |
|---|---|
| Keps (TLE) de los satélites | `tools/satellite/write_keps.py` o CPS → *Satellites* → *Write radio* |
| Frecuencias de cada satélite (voz / APRS / otra) | lo mismo: van en el bloque de keps del codeplug |
| **Reloj válido** (UTC ≥ 2025-01-01) | GPS con fix, o *Info radio → Hora / Fecha* |
| Posición | GPS con fix, o *Opciones → Opc. APRS → Ubicación* |
| Config APRS **`ISS`** con Vía 1 **`ARISS`** | CPS → pestaña *APRS*, o `tools/debug/aprs_config.py` |

El **nombre tiene que coincidir exacto** con el del satélite (máx. 8 caracteres, sin distinguir mayúsculas); si no,
la radio hace **tono de error** y no transmite. El path del digipeater va en **Vía 1** (SSID 0).

### Cargarlo por cable (sin CPS)

```bash
# keps + frecuencias (9 presets del CPS, con el APRS del ISS en 437.825)
py tools/satellite/write_keps.py COM6 --frequencies tools/satellite/satellites-iss.txt
# con TLE de un archivo, si celestrak falla:  --tle keps.txt (3 líneas por satélite)

# la config APRS del ISS (la crea si no existe, en el primer hueco)
py tools/debug/aprs_config.py COM6            # lista las 8 configuraciones
py tools/debug/aprs_config.py COM6 --set-iss  # crea 'ISS' con Vía 1 ARISS, icono '['
```

Ambos escriben por el *cable link* (las órdenes `R` / `X` del CPS) y releen lo escrito antes de terminar.
`aprs_config.py` toca solo el sector del codeplug donde viven las 8 configuraciones (`0x1588`, 64 bytes cada una): no
toca canales, ajustes ni la calibración.

### Operarlo

1. **Menú → Satélite**: la lista de pases (necesita reloj y posición válidos; si no, no calcula nada).
2. Elegir el **ISS** (tecla verde).
3. La **tecla verde** alterna el tipo de frecuencia: **Voz → APRS → CW Rx**. Dejarlo en **APRS** (437.825).
4. **PTT**: manda la baliza. La corrección Doppler la aplica la radio sola (en 70 cm es grande, ±10 kHz).
5. Si suena el tono de error: falta la config `ISS`, o falta la posición (GPS sin fix con *Ubicación = GPS*).

Ojo: el GPS **actualiza la ubicación de la radio** (`settings.location`) cuando se mueve, así que la "posición fija"
que se cargue a mano se reemplaza con la del GPS en el próximo fix; eso es lo normal y lo conveniente para el pase.

La propia release avisa que el APRS satélite **se probó solo en tierra**, no a través de un satélite real.

### Recompilar el CPS con el preset corregido (0.9.1)

El CPS se compila de forma reproducible: con el fuente del tag `v0.9.0` y sus dependencias:

```bash
py -m pip install rjsmin rcssmin minify-html zopfli
# en el fuente del CPS: js/codeplug/satellites.js, preset ISS -> aprsRx/aprsTx MHZ(437.825)
py release.py   # sale byte a byte igual que el asset publicado
```

El resultado se copia a `dist/cps/opendmr-cps.html` (y a `dist/opendmr-cps.html`) y `deploy.sh` lo publica como
`cps.html` y en `descargas/`.

## Notas

- La web no puede ver los puertos COM del visitante (los navegadores no permiten enumerarlos); por eso el generador pide escribir el número y el grabador abre el selector del navegador.
- El botón **Abrir CPS online** sirve el mismo `opendmr-cps.html` que está en el ZIP: la versión 0.9.1 (0.9.0 con el APRS del ISS corregido a 437.825 MHz), con la página de Temas (colores propios para la radio, necesita OpenDMR 0.6.0), interfaz en español y elección del idioma del menú de la radio, además de NAC/RAN de P25/NXDN, airband, radio IDs importados por CSV y borrado múltiple (P25 sigue usando el DMR ID como unit ID). Se puede usar también abriendo el archivo local, pero HTTPS es más fiable con Web Serial.
- No hay formularios que envíen datos al servidor: todo (scripts, verificación y grabación) se hace en el navegador.
- El protocolo está portado de `tools/backup/dm32_dump.py`; la lógica de escritura de sectores respeta el bootloader por debajo de `0xC000`.
