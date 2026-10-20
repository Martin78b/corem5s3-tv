# Handoff: revisión de código (2026-10-17)

Hallazgos de una revisión estática del firmware en la rama `feature/dual-display-support`
(commit `7947d0a`). **Nada está aplicado ni probado en hardware.** Cada ítem tiene un ID
para poder pedir "haz B1, B3 y P1" en una sesión futura.

Antes de aplicar un ítem, verificar que las líneas citadas siguen siendo las mismas
(el código puede haber cambiado desde esta revisión).

---

## B — Bugs reales

- [x] **B1. Rectángulo negro fijo sobre el video** — `firmware/src/main.cpp:494-497`
  Cuando el OSD de batería no se muestra, el `else` pinta `fillRect(3, 3, 34, 14, TFT_BLACK)`
  en cada frame. El frame ya redibuja toda la pantalla, así que solo deja un parche negro.
  **Fix:** eliminar el `else`.
  ✅ Aplicado (2026-10-19): `else` eliminado.

- [x] **B2. Escritura fuera del framebuffer en `JPEGDraw`** — `firmware/src/video_player.cpp:84-96`
  No se recorta `draw->x + iWidth` / `draw->y + iHeight` a `DISPLAY_WIDTH/HEIGHT`.
  El conversor genera 320×240 por defecto (`tools/convert_episodes.py:32`); en la Waveshare
  (240×240) eso corrompe PSRAM.
  **Fix:** recortar ancho/alto en `JPEGDraw`; además, que el script avise o tenga un flag
  `--board waveshare` que fije 240×240.
  ✅ Aplicado (2026-10-19): recorte en `JPEGDraw`; el script tiene `--board {m5stack,waveshare,generic}` y avisa si la resolución no coincide con ninguna placa.

- [x] **B3. Condición de carrera en audio** — `firmware/src/main.cpp:86-101`, `firmware/src/audio_player.cpp:60-71`
  - `audioTask` (core 0) llama `playFilled()` fuera del mutex, que puede hacer `_file.close()` (acceso SD sin lock).
  - `playEpisode()` (core 1) llama `playFile()` y reasigna `_playPtr/_fillPtr/_file` mientras la otra tarea los usa.
  - `s_playing` y `_playing` no son `volatile`/`std::atomic`.
  **Fix mínimo:** meter `playFilled()` + `fillNext()` dentro del mutex y marcar los flags como atómicos.
  **Fix ideal:** que solo `audioTask` toque `AudioPlayer`; `loop()` le manda comandos (play/stop) por una cola FreeRTOS.
  ✅ Aplicado (2026-10-19), variante intermedia: nuevo `s_audioMutex` que protege todo `AudioPlayer` (audioTask y `playEpisode`); orden de locks audio → SD. `playFilled()` ya no toca la SD (el cierre al EOF lo hace `fillNext()`/`playFile()` con el mutex SD). `isPlaying()` no lee `_file`. `s_playing` y `_playing` son `std::atomic<bool>`. `s_video.close()` del loop ahora va con el mutex SD. La cola de comandos (fix ideal) queda para P3.

- [x] **B4. Debounce roto en botones BOOT/+** — `firmware/src/main.cpp:379-404`
  Si se suelta antes de 50 ms, `bootPressMs`/`plusPressMs` no vuelven a 0 → la siguiente
  pulsación dispara al instante. El `while (digitalRead(...) == LOW) delay(10)` congela el video.
  **Fix:** máquina de estados no bloqueante (reset al soltar, disparo en flanco).
  ✅ Aplicado (2026-10-19): `DebouncedButton` + `buttonPressed()`, no bloqueante, dispara una vez por pulsación.

- [x] **B5. FPS / sample rate fijos en firmware pero configurables en el script**
  `config.h` fija `VIDEO_FPS 15` y `AUDIO_SAMPLE_RATE 44100`; el script acepta `--fps` y
  `--audio-rate` (y su ejemplo en la línea 10 usa `--fps 20`) → desincronía A/V.
  **Fix:** cabecera en el `.mjpeg` (magic, versión, fps, w, h, sample rate) leída en
  `VideoPlayer::openFile`, o quitar esos flags del script.
  ✅ Aplicado (2026-10-19): cabecera de 16 bytes `CMTV` (versión, fps, w, h, sample rate) leída en `VideoPlayer::readHeader()`; los archivos sin cabecera siguen funcionando (15 fps). El ritmo usa el fps de la cabecera. `--audio-rate` eliminado del script (fijo en 44100); si la cabecera trae otra tasa, el firmware no reproduce el audio.

## D — README vs código

- [x] **D1.** README: "Tap → Previous episode" (Waveshare). El código del tap está en
  `#ifndef WAVESHARE_154` dentro de `#elif defined(WAVESHARE_154)` (`main.cpp:612-619`) → nunca compila.
  Decidir: implementarlo o quitarlo del README.
  ✅ Resuelto (2026-10-19): el tap se quitó del README y se eliminó el bloque muerto en `handleTouch()`. README corregido también para BOOT = siguiente y + = anterior.
- [x] **D2.** README: mantener PWR 3 s. `loop()` usa 2 s (`main.cpp:368`); `checkPowerOff()` usa 3 s
  (`main.cpp:132`). Unificar en una sola función y una constante en `config.h`.
  ✅ Aplicado (2026-10-19): `loop()` usa `checkPowerOff()`; constante `PWR_LONG_PRESS_MS 3000` en `config.h`.
- [x] **D3.** README: estática de 2 s entre episodios. El código usa 1.5 s (`main.cpp:425`).
  ✅ Resuelto (2026-10-19): README dice 1.5 s; constantes `EPISODE_STATIC_MS` / `CHANNEL_STATIC_MS` en `config.h`.
- [x] **D4.** README menciona scanlines y ruido CRT; `applyScanlines`/`applyNoise` nunca se llaman.
  Decidir: activarlos (medir impacto en FPS) o quitarlos del README.
  ✅ Resuelto (2026-10-19): scanlines y ruido quitados del README (las funciones siguen en el código, ver L3).

## R — Robustez

- [x] **R1.** `GESTURE_DEBUG 1` por defecto (`config.h:106`) → `Serial.printf` en cada lectura táctil
  (cada 5–30 ms) aunque no haya dedo (`main.cpp:597`). Poner `0` para release.
  ✅ Aplicado (2026-10-19): `GESTURE_DEBUG 0`.
- [x] **R2. Batería** (`firmware/src/battery_monitor.h`):
  - Usar `analogReadMilliVolts()` (calibrado eFuse) y promediar 8–16 muestras.
  - Sustituir la curva lineal 3.0–4.2 V por una tabla LiPo de 5–6 puntos.
  - Reiniciar `s_lowBatteryWarned` al cargar (`main.cpp:505`).
  - Quitar el `delay(2000)` del aviso de batería baja (`main.cpp:511`); mostrarlo como OSD temporal.
  ✅ Aplicado (2026-10-19): `analogReadMilliVolts()` promediando 16 muestras; tabla LiPo de 7 puntos (3.3 V = 0 %, 4.2 V = 100 %) interpolada; `s_lowBatteryWarned` se rearma al cargar; el aviso es un OSD de 3 s (`LOW_BATTERY_OSD_MS`) sin `delay` ni estática.
- [x] **R3. Descarte de frames**: si el video va atrasado más de 1–2 frames respecto al audio,
  leer el frame sin decodificar (`main.cpp:454-472`).
  ✅ Aplicado (2026-10-19): si el video va más de `VIDEO_MAX_LAG_FRAMES` (2) atrás del audio, se leen y descartan frames sin decodificar, como máximo `VIDEO_MAX_SKIP_PER_LOOP` (8) por vuelta.
- [x] **R4. Offset de sincronía**: `totalSamples()` cuenta muestras encoladas, no reproducidas →
  video adelantado ~90–180 ms. Restar la latencia de DMA + chunk.
  ✅ Aplicado (2026-10-19): `AudioPlayer::playedSamples()`. Waveshare/genérica: muestras entregadas a I2S (contador atómico por bloque de 256 en `I2SSpeaker`) menos el anillo DMA (`AUDIO_DMA_BUF_COUNT * AUDIO_DMA_BUF_LEN`). M5: interpolación por tiempo dentro del chunk en curso. Además el reloj ahora avanza en pasos de ~6 ms en vez de 93 ms.
- [x] **R5. Overflow de `millis()`**: comparaciones como `nowMs < s_channelOsdEnd` (`main.cpp:486, 492`,
  `gesture_control.cpp:325-330`). Usar `(int32_t)(end - now) > 0`.
  ✅ Aplicado (2026-10-19): los OSD (canal, batería, batería baja, volumen/brillo) guardan el instante de inicio y comparan `now - start < duración`. Se usó eso en vez de `(int32_t)(end - now) > 0`, porque con fines viejos esta última vuelve a dar verdadero tras ~24 días.
- [x] **R6. Rutas** (`video_player.h:7-10`, `video_player.cpp:98-130`): `char[64]` trunca en silencio;
  `strncpy` sin terminador nulo garantizado; `hasAudio()` recalcula una ruta que ya está en `pcmPath`.
  Ordenar el escaneo alfabéticamente (hoy depende del orden FAT).
  ✅ Aplicado (2026-10-19): `EPISODE_PATH_MAX 96`; `snprintf` con verificación, y los nombres que no entran se saltan con un warning. `pcmPath` se arma desde el nombre base. `hasAudio(const VideoFile&)` usa `pcmPath`. El escaneo se ordena con `qsort`/`strcasecmp`.
- [x] **R7. Playlist** (`main.cpp:663-698`): al rebarajar, evitar que el primer episodio nuevo sea el último que sonó.
  ✅ Aplicado (2026-10-19): al rebarajar, si el primero coincide con el último que sonó, se intercambia con otra posición al azar.
- [ ] **R8. ES8311** (`firmware/src/es8311.cpp`): no se revisa ningún retorno de `write_reg`;
  `es8311_set_volume(255)` se llama dos veces (`main.cpp:269` y `:324`).
  ⏳ Parcial (2026-10-19, con P2): se eliminó la segunda llamada `es8311_set_volume(255)`, porque ahora pisaría el volumen inicial. Falta revisar los retornos de `write_reg`.
- [ ] **R9. Verificar** `BTN_PWR` (GPIO5): se configura como `OUTPUT HIGH` (`main.cpp:154-155`) y luego se lee
  con `gpio_get_level`. Confirmar en hardware/esquemático que la lectura es fiable (si el input no
  está habilitado podría leer siempre 0).

## P — Rendimiento

- [x] **P1. DMA en el display** (mayor ganancia): hoy `pushPixels` bloquea ~115 KB por frame.
  Usar `Display.initDMA()` + `pushImageDMA()` con doble framebuffer para decodificar N+1 mientras se envía N.
  Solo aplica a TFT_eSPI (Waveshare/genérica); M5Unified tiene su propio mecanismo.
  ✅ Aplicado (2026-10-19), solo Waveshare (`DISPLAY_DMA`, se desactiva con 0): dos `TFT_eSprite` de 240×240 en PSRAM (creados antes de `initDMA()`), decodificados en big-endian. El OSD se dibuja dentro del frame, sin parpadeo. El frame N se envía en bandas de 68 filas (límite de 0x4000 px por DMA de TFT_eSPI) mediante un bounce buffer interno; `JPEGDraw` alimenta la siguiente banda mientras decodifica N+1. El DMA nunca queda en vuelo al salir de `showFrameDMA()`. Genérica: queda bloqueante porque la SD podría compartir el host SPI. M5: sin cambios.
- [x] **P2. Audio mono** (`firmware/src/audio_hal.h:56-79`): evitar duplicar a estéreo
  (`I2S_CHANNEL_FMT_ONLY_LEFT` o mono en el ES8311) y mover el volumen al DAC (reg 0x32) o usar
  curva logarítmica en vez de `*v/255` lineal.
  ✅ Aplicado (2026-10-19): `AUDIO_I2S_MONO` (Waveshare = 1) usa `I2S_CHANNEL_FMT_ONLY_LEFT` y escribe el PCM directo sin copiar. En Waveshare el volumen va al ES8311 (reg 0x32), con 50 dB repartidos en pasos de 0.5 dB; el PCM sale sin escalar. Genérica: curva cuadrática por software. **2026-10-19: el mono rompió el audio en hardware** → `AUDIO_I2S_MONO 0` en Waveshare (estéreo duplicado, como antes). El volumen en el DAC se mantiene. Para reintentar el mono: probar `I2S_CHANNEL_FMT_ONLY_RIGHT`, o configurar el ES8311 (reg 0x09, bit 7) para leer el slot correcto.
- [ ] **P3. Simplificar `audioTask`**: bucle "leer SD → `i2s_write` bloqueante", eliminando el
  `isPlaying()` estimado por tiempo y el doble buffer manual. Combina bien con B3.

## L — Limpieza del repo

- [x] **L1.** Borrar o mover a `docs/reference/`: `scratch_bsp_config.h`, `scratch_es8311.c`,
  `scratch_esp32_s3_touch_lcd_1_54.c`, `scratch_esp32_s3_touch_lcd_1_54.h`.
  ✅ Aplicado (2026-10-19): movidos a `docs/reference/` (F3.1 cita uno de ellos).
- [x] **L2.** Borrar `firmware/boards/m5stack-cores3.json.bak` y `firmware/boards/tft_waveshare_154.h` (no se usa).
  ✅ Aplicado (2026-10-19): ambos borrados.
- [ ] **L3.** Código muerto: `i2c_scan()`, `triggerVolumeStep/BrightnessStep`, `getPlaybackTimeUs`,
  `applyCurvature`, `dimColor`, `randomColor`, `totalFrames`, `drawString` comentados,
  `#include "config.h"` duplicado (`main.cpp:1` y `:12`).
- [ ] **L4.** Números mágicos en `main.cpp`: GPIO `2` (retención de energía) → `PWR_HOLD_PIN`;
  `42/41` → `TOUCH_SDA/TOUCH_SCL`; `0x18` → `ES8311_ADDR`.
- [ ] **L5.** `flasher/*.bin` versionados (fecha junio): generarlos en CI/release o documentar de qué commit salen.
- [ ] **L6.** Fijar versiones exactas en `lib_deps` de `platformio.ini` (hoy `^`).
- [ ] **L7.** Dividir `main.cpp` (878 líneas) en `input.cpp`, `power.cpp`, `osd.cpp`, `playback.cpp`.
- [ ] **L8.** Unificar idioma de los comentarios.

---

## Orden sugerido

1. **Rápidos y seguros:** B1, B2, R1, D2, L1, L2.
2. **Estabilidad:** B3 (+P3), B4, R5.
3. **Calidad A/V:** B5, R3, R4.
4. **Rendimiento:** P1, P2.
5. **Refactor:** L3–L8.

Compilar los tres entornos tras cada bloque:
`pio run -e m5stack-cores3-se -e generic-esp32-s3-154 -e waveshare-s3-touch-lcd-154` (desde `firmware/`).

---

# Funcionalidades futuras (2026-10-17)

Propuestas de diseño, **sin implementar**. Los IDs `F*` siguen el mismo esquema que arriba.

### Decisiones tomadas (2026-10-17)

- [x] El menú **pausa video y audio**.
- [x] Objetivo principal: **Waveshare** (M5 solo si sale barato; no bloquear por M5).
- [x] WiFi, por prioridad: **1) hora NTP**, **2) OTA**, **3) subir capítulos a la SD** (opcional;
  el modo USB F6 cubre mejor este caso).
- [x] Agregar **modo USB para exponer la SD a la PC** (F6).
- [x] Apagado: **ambos**, sleep timer + apagado por inactividad (ver F2).

### Decisiones pendientes

Ninguna por ahora.

### Orden recomendado

F1 (menú + base) → F2 (apagado) → F6 (modo USB) → F3 (rotación) → F4 (WiFi) → F5.1 (NTP) → F5.2 (OTA) → F5.3 (subida WiFi).
F1 conviene hacerlo junto con **B3** y **L7**, porque requiere reestructurar `loop()`.
**F7** es independiente y pequeño: se puede hacer en cualquier momento, idealmente antes de F1 (Info) y de F5.2 (OTA).

---

## F1 — Menú en pantalla (base de todo lo demás)

- [ ] **F1.1 Máquina de estados de la app**: `PLAYING`, `MENU`, `STATIC`, `WIFI_SETUP`, `POWER_OFF`.
  Sustituye la lógica actual de flags sueltos en `loop()` (`s_playing`, `s_staticTransition`, ...).
- [ ] **F1.2 Módulo `settings`**: struct de configuración + guardado/carga en NVS con `Preferences`.
  Incluir un número de versión del esquema para migrar ajustes en el futuro.
  Candidatos a persistir: volumen, brillo, rotación, auto-apagado por inactividad, WiFi on/off.
- [ ] **F1.3 Cómo abrirlo**:
  - **Recomendado:** pulsación **corta** de PWR (hoy libre; la larga sigue apagando).
  - Alternativas: mantener el dedo 1 s en la pantalla, o doble tap.
- [ ] **F1.4 Navegación** (soportar ambas):
  - Botones: BOOT = bajar, `+` = cambiar valor, PWR corto = atrás/salir.
  - Táctil: swipe arriba/abajo = moverse, tap = seleccionar/cambiar.
  - Cerrar automáticamente tras ~10 s sin interacción.
- [ ] **F1.5 Render**: menú propio y ligero (tabla de ítems: `label`, `tipo` [enum/bool/acción],
  `opciones[]`, puntero al valor) dibujado con TFT_eSPI / M5GFX, estilo "menú de servicio" de TV
  antigua (verde CRT, igual que los OSD de `gesture_control.cpp`).
  **Descartado:** LVGL (pesado, choca con el pipeline de framebuffer y con M5).
- [ ] **F1.6 Al pausar**: detener el audio (`s_audio.stop()` o una pausa real) y guardar la posición
  de video/audio para retomar sin desincronía.
- [ ] **F1.7 Gestos según el estado (sin conflicto con volumen/brillo)**:

  | Estado | Swipe vertical | Swipe horizontal | Tap |
  |---|---|---|---|
  | Reproduciendo | Volumen | Brillo | Nada (o D1 si se implementa) |
  | Menú abierto | Moverse entre opciones | Cambiar valor (← →) | Seleccionar |
  | Aviso en pantalla (F2.4 / F6.3) | — | — | Cancelar / aceptar el aviso |

  - El menú se abre con **PWR corto**, independiente de los gestos. Con el menú abierto, el control de
    volumen/brillo por gestos se **desactiva**; ambos son editables como opciones del menú.
  - **Reiniciar `GestureController`** (estado `IDLE`) al entrar y salir del menú, para que un swipe a medio hacer
    no siga cambiando el volumen o el brillo.
  - **Concurrencia**: el estado de la app es una variable **atómica**. `touchTask` (core 0) solo **publica eventos**
    (swipe arriba/abajo/izq/der, tap) en una cola; `loop()` (core 1) los consume según el estado y es el único que dibuja.
    Así `gesture_control.cpp` deja de llamar directamente a `setVolume`/`Backlight` durante el menú.
  - **Tap que cancela un aviso**: debe **consumirse** y no propagarse (p. ej., que no cambie de episodio si se implementa D1).
  - **Alternativas para abrir el menú** si se descarta PWR: mantener el dedo 1 s quieto (<15 px, compatible con la lógica
    actual de `TRACKING`; riesgo con swipes lentos) o doble tap (añade latencia a cada tap).
  - Con F3, las coordenadas táctiles se rotan antes de clasificar el gesto → "arriba" siempre es arriba visualmente.

Estructura de menú propuesta:
```
CONFIGURACIÓN
  Apagado         Off / 15 / 30 / 60 / 120 / 240 min / Fin episodio
  Inactividad     Off / 1 h / 2 h / 4 h
  Rotación        Auto / 0° / 90° / 180° / 270°
  Volumen         0–100 %
  Brillo          10–100 %
  WiFi            Conectar… / Olvidar red / Estado
  Info            Versión, batería, IP, nº episodios
  Salir
```

## F2 — Temporizador de apagado

- [ ] **F2.1 Sleep timer** (estilo TV): cuenta desde que se activa. Valores: Off, 15, 30, 60, 120, 240 min.
  **No** se guarda entre reinicios.
- [ ] **F2.2 "Al terminar el episodio"**: apagar cuando termina el episodio actual
  (enganchar en la transición a `nextEpisode()` de `loop()`).
- [ ] **F2.3 Apagado por inactividad**: se reinicia con cada toque, swipe o botón. **Sí** se guarda en NVS.
  Valores: Off / 1 h / 2 h / 4 h, con **2 h por defecto**. Son valores largos a propósito: ver la TV es pasivo,
  y con 15–30 min se apagaría en mitad de un episodio aunque alguien esté mirando.
  Propósito: proteger la batería si se queda olvidado.
- [ ] **F2.4 Aviso previo** (común a los dos): OSD 60 s antes, "Apagando en 1 min — toca para cancelar".
  - Si lo dispara la **inactividad**: tocar cancela y reinicia el contador.
  - Si lo dispara el **sleep timer**: tocar desactiva el timer (o lo extiende 15 min; decidir al implementarlo).
- [ ] **F2.7 Convivencia**: ambos corren en paralelo y gana el que vence primero. En modo USB (F6) o con el cargador
  conectado, pausar el contador de inactividad. El sleep timer se respeta siempre.
- [ ] **F2.8 Indicador**: con el sleep timer activo, mostrar el tiempo restante (p. ej. "⏾ 25m") junto al OSD
  de canal/batería, y en el menú.
- [ ] **F2.5 Ejecución**: reutilizar `showCRTOffAnimation()` + `digitalWrite(PWR_HOLD, LOW)` +
  `esp_deep_sleep_start()`. Hacerlo de paso con D2/L4 (unificar el apagado en una sola función).
- [ ] **F2.6** Usar comparaciones seguras ante overflow de `millis()` (ver R5).

## F3 — Rotación automática por acelerómetro

- [ ] **F3.1 Verificar hardware primero**:
  - Waveshare: `HARDWARE_REFERENCE.md` dice que hay un QMI8658 en I2C `0x6B` (bus compartido SDA42/SCL41),
    pero `docs/reference/scratch_esp32_s3_touch_lcd_1_54.h:61` dice `BSP_CAPS_IMU 0`. Confirmar llamando a
    `i2c_scan()` (ya existe en `main.cpp`) y leyendo el WHO_AM_I del QMI8658.
  - M5 CoreS3 **SE**: probablemente **no** tiene IMU (la CoreS3 normal sí, una BMI270 vía `M5.Imu`).
    Confirmar. Si no hay IMU, solo rotación manual desde el menú.
- [ ] **F3.2 Driver mínimo del QMI8658**: solo acelerómetro (el giroscopio no hace falta), ODR bajo, unos 10 Hz.
  Leerlo **dentro de `touchTask`** para no competir por el bus I2C.
- [ ] **F3.3 Lógica de orientación**: eje dominante de la gravedad, con histéresis (ángulo claro, ~35°),
  estabilidad de ~0.7 s antes de girar y sin girar si el dispositivo está plano (Z dominante).
- [ ] **F3.4 Aplicar**: `Display.setRotation(n)` (rotación por hardware vía MADCTL, sin coste de CPU).
  Hacerlo desde el hilo de `loop()` (no desde `touchTask`) para evitar conflictos de SPI: la tarea
  solo publica la orientación deseada.
- [ ] **F3.5 Rotar coordenadas táctiles** según la rotación actual, para que los gestos de volumen y brillo
  sigan la orientación visual.
- [ ] **F3.6 Probar** posibles desplazamientos del ST7789 240×240 en las rotaciones 2 y 3 (rowstart/colstart).
- [ ] **F3.7 M5 (320×240)**: o solo 0°/180°, o en vertical encoger el video con barras negras.
- [ ] **F3.8 Ajuste en el menú**: Auto / fija 0° / 90° / 180° / 270°, guardado en NVS.

## F4 — WiFi fácil con celular + QR

Opción elegida (recomendada): **A. Punto de acceso propio + QR + portal cautivo**. No necesita app y funciona en iOS y Android.

- [ ] **F4.1 Flujo**:
  1. Menú → WiFi → Conectar.
  2. Arrancar el SoftAP `CoreTV-XXXX` (sufijo de la MAC) con **contraseña aleatoria nueva en cada intento**.
  3. Mostrar **QR 1**: `WIFI:T:WPA;S:CoreTV-XXXX;P:<pass>;;`. La cámara del teléfono se conecta sola.
  4. Portal cautivo (DNSServer + WebServer) con un escaneo de redes, un desplegable y un campo de contraseña.
  5. Si el portal no se abre solo, mostrar **QR 2** con `http://192.168.4.1`.
  6. Probar la conexión, guardarla en NVS y mostrar "Conectado ✓ + IP". Apagar el AP.
  7. Timeout de 5 min sin uso → salir del modo de configuración.
- [ ] **F4.2 Librerías**: `ricmoo/QRCode` (generar el QR y dibujarlo con `fillRect`, escalado para 240 px),
  y WiFiManager (tzapu) **o** un portal propio con `WebServer` + `DNSServer` (más control sobre el estilo).
- [ ] **F4.3 Recursos**: el WiFi usa unos 60 KB de RAM interna, gasta batería y compite con la reproducción.
  Encenderlo **solo bajo demanda**; pausar el video durante la configuración.
- [ ] **F4.4 Menú**: Conectar… / Olvidar red / Estado (SSID, IP, RSSI).
- [ ] **F4.5 Complemento opcional**: **Improv WiFi Serial**, para configurar desde el navegador justo después
  de flashear con el flasher web (ESP Web Tools).

Alternativas descartadas o secundarias:
- **Espressif Provisioning (BLE/SoftAP)**: el QR lo lee una app oficial. Seguro, pero obliga a instalar una app y BLE consume más memoria.
- **SmartConfig / ESP-Touch**: poco fiable y necesita app.

## F5 — Extras que habilita el WiFi (decidir cuál primero)

- [ ] **F5.1 Hora NTP** → guardarla en el RTC (PCF85063 en Waveshare, `0x51`; BM8563 en M5).
  Permite un reloj en el OSD/menú y apagar a una hora ("apagar a las 23:00").
- [ ] **F5.2 OTA**: requiere una tabla de particiones con `ota_0/ota_1`. **Ojo:** el entorno
  `waveshare-s3-touch-lcd-154` usa `default_8MB.csv` y `flash_mode = dio`, aunque
  `HARDWARE_REFERENCE.md` indica 16 MB de flash. Verificar el tamaño real antes de cambiarlo.
- [ ] **F5.3 Subir episodios desde el navegador** (lento por SD + WiFi; útil para pocos archivos).
  **Prioridad baja:** F6 (modo USB) cubre mejor este caso, porque la conversión ya se hace en la PC.
- [ ] **F5.4 Streaming desde NAS/Jellyfin** (ya en el roadmap del README). Lo más exigente. No priorizado.

## F6 — Modo USB: exponer la SD a la PC (Mass Storage)

El ESP32-S3 tiene USB OTG nativo; con TinyUSB se presenta como **unidad de almacenamiento USB (MSC)**.
El entorno `waveshare-s3-touch-lcd-154` ya usa `ARDUINO_USB_MODE=0` (TinyUSB), requisito para MSC.
El USB será **compuesto** (CDC para logs/flasheo + MSC).

- [ ] **F6.1 Spike técnico primero** (lo de mayor riesgo):
  - Clase `USBMSC` del core Arduino 2.x (`#include "USB.h"`, `"USBMSC.h"`; callbacks `onRead`, `onWrite`, `onStartStop`).
  - La SD de la Waveshare va por **SDMMC 4-bit** (`SD_MMC`). En el core 2.x, `SD_MMC` probablemente no expone
    lectura/escritura de sectores en crudo → usar la API IDF (`sdmmc_read_sectors` / `sdmmc_write_sectors`)
    con el `sdmmc_card_t*`, o inicializar la SD por IDF. En el core 3.x `SD_MMC` sí podría tener `readRAW`/`writeRAW`;
    verificar antes de decidir si conviene migrar de core.
  - Comprobar que el flasheo y el monitor serie siguen funcionando con USB compuesto
    (puede requerir el botón BOOT para flashear).
- [ ] **F6.2 Detección de PC vs cargador**: usar los eventos USB de TinyUSB (`ARDUINO_USB_STARTED_EVENT`,
  `ARDUINO_USB_STOPPED_EVENT`, `ARDUINO_USB_SUSPEND_EVENT`). Solo una PC enumera el dispositivo; un cargador no.
- [ ] **F6.3 Flujo**:
  1. PC detectada → OSD "PC detectada · ¿Modo USB? (tap = sí)", o entrar desde Menú → *Modo USB*.
  2. Al aceptar: pausar la reproducción, cerrar archivos y **desmontar la SD** en el firmware (`SD_MMC.end()`),
     y luego marcar el medio como presente para la PC.
  3. Pantalla fija "MODO USB · No desconectar · Expulsa desde la PC".
  4. Al expulsar (`onStartStop` con `load_eject`) o desconectar el cable: marcar el medio como no presente,
     **volver a montar la SD**, reescanear episodios (`VideoPlayer::scanEpisodes`), rehacer la playlist y retomar.
- [ ] **F6.4 Regla de oro**: **nunca** acceso simultáneo PC + firmware (corrompe FAT). Mientras no esté en modo USB,
  el MSC reporta "medio no presente" (unidad vacía) para que la PC no monte nada.
- [ ] **F6.5 Expectativas**: USB Full Speed (12 Mbps) → ~0.6–1 MB/s reales; un capítulo de ~100 MB tarda 2–3 min.
  Documentarlo en el README.
- [ ] **F6.6 Batería**: con el cable conectado el equipo carga; mantener el backlight bajo durante el modo USB.
- [ ] **F6.7 (opcional)**: en `tools/convert_episodes.py`, permitir `--output` directo a la unidad montada y avisar
  si el tamaño de salida no coincide con la placa (ver B2).
- [ ] **F6.8 M5**: su entorno usa `ARDUINO_USB_MODE=1` (USB-Serial/JTAG por hardware) y la SD comparte el bus SPI
  con el LCD. Fuera de alcance salvo que sea barato.

## F7 — Versión del firmware y URL del repo en la pantalla de boot

El logo (`logo_240.h` y `logo_320.h`, ambos de 240 px de alto) **ya incluye el nombre** "corem5s3-tv" dibujado en la imagen.
Hay una franja negra libre en las **filas 212–239 (28 px)** en ambos logos → caben 2 líneas de fuente 1 (8 px).

- [ ] **F7.1 Texto** (centrado, fuente 1, gris o verde tenue para no competir con el logo):
  ```
          v0.3.0 · 7947d0a
   github.com/Martin78b/corem5s3-tv
  ```
  Posición aproximada: línea 1 en y≈216, línea 2 en y≈228. La URL tiene 32 caracteres ≈ 192 px, así que entra en 240 px.
  Dibujar con `setTextDatum(TC_DATUM)` (o calcular `textWidth`) en `showBootAnimation()` (`main.cpp:773-778`),
  justo después de pintar el logo y antes del `delay(1500)`.
- [ ] **F7.2 Versión automática** (opción C: semver por tag + hash de git):
  - Script `firmware/scripts/version.py` con `extra_scripts = pre:scripts/version.py` en **los tres** entornos de `platformio.ini`.
  - Ejecuta `git describe --tags --always --dirty` y define `-DFW_VERSION=\"...\"` y `-DFW_GIT_HASH=\"...\"`.
    Ejemplo de salida: `v0.3.0` · `7947d0a`, o `v0.3.0-2-g1a2b3c4-dirty`.
  - Si no hay git disponible (p. ej., zip descargado), usar `FW_VERSION "dev"` como valor por defecto en `config.h`
    con `#ifndef`.
  - Definir `FW_REPO_URL "github.com/Martin78b/corem5s3-tv"` en `config.h`.
- [ ] **F7.3 Crear el primer tag** de versión semántica cuando se decida (p. ej. `git tag v0.3.0`).
  Hoy no hay tags, así que mientras tanto solo se mostraría el hash.
- [ ] **F7.4 Reutilizar la versión**:
  - Imprimirla en el log serie al arrancar (`=== BOOT === v0.3.0 (7947d0a)`).
  - Mostrarla en Menú → Info (F1).
  - Usarla para comparar versiones en OTA (F5.2).
  - Nombrar los binarios de `flasher/` con ella (L5).
