# Arc Audio Engine (AAE) — Roadmap

---

## ✅ Completado

### Fases principales (1–12)
| Fase | Descripción |
|------|-------------|
| **1** | **FFI Pipeline** — Dart ↔ C++ bridge funcional |
| **2** | **FLAC Decoder** — Decodificación FLAC a PCM (get_flac_info + play_flac) |
| **3** | **AAudio Playback** — Reproducción por altavoz vía AAudio |
| **4a** | **Soporte WAV** — WAV PCM nativo (8/16/24/32bit) sin dependencias |
| **4b** | **Soporte MP3/AAC/Ogg** — AMediaCodec para formatos comprimidos |
| **5** | **Controles de reproducción** — Play/Pause/Stop/Seek con slider UI |
| **6** | **Buffer ring (AAudio callback)** — Ring buffer SPSC lock-free + eventfd signaling |
| **7** | **PCM Stream a Dart** — Stream de samples crudos para visualización (VU meter, waveform) |
| **Fixes** | Heap corruption, 440Hz tone test, closeAAudioStream, memory leak en Media error path |
| **8** | **Ecualizador / Efectos** — DSP C++: 10 bandas EQ con filtros biquad + UI de sliders |
| **9** | **Streaming por URL** — AMediaExtractor (API 29+) + fallback download-then-play + diálogo persistente |
| **—** | **Acceso a archivos externos** — Scoped storage resuelto vía FilePicker SAF + copia a directorio interno |
| **10** | **Multi-track Mixer** — 4 pistas simultáneas con volumen/pan independiente vía FFI |
| **11** | **API Dart completa** — TrackPlayer (streams estado/posición), AudioEngine singleton, backward compat |
| **12** | **Pruebas / Benchmarks** — 30 tests Dart, 22 tests C++, benchmarks RingBuffer y DSP |
| **10.1** | **Guard contra archivos eliminados** — try-catch + existsSync() para evitar pantalla roja |
| **P1.3b** | **Crossfade entre tracks** — Fundido cruzado durante transición gapless con early trigger, silence scan en fadeHistory y preBuf |

### P0 — Alta prioridad
| # | Área | Mejora | Archivos clave |
|---|------|--------|----------------|
| P0.1 | **EQ** | Q ajustable por banda | `dsp_processor.h`, `eq_dialog.dart` |
| P0.2 | **EQ** | Tipo de filtro por banda (Peak/LS/HS/LP/HP) | `dsp_processor.h:24-28`, `eq_dialog.dart:82-89` |
| P0.3 | **Mixer** | Mute / Solo por track | `engine_state.h:45-47`, `audio_engine.cpp:259-271`, `track_player.dart:104-119` |
| P0.4 | **Pipeline** | Limitador post-mezcla (hard-clipper) | `limiter.cpp`, `audio_mixer.dart:391-406` |
| P0.5 | **Mixer** | Loop por track | `engine_threads.cpp:91-93` (4 threads), `track_player.dart:117-119` |
| P0.6 | **Android** | Audio Focus (ducking, pausa por notif) | `AudioEnginePlugin.kt`, `audio_focus.dart`, `audio_mixer.dart:25-164` |

### P1 — Media prioridad
| # | Área | Mejora | Archivos clave |
|---|------|--------|----------------|
| P1.1 | **EQ** | Presets (Flat/Rock/Pop/Jazz/Classical/Custom) | `eq_dialog.dart:99-107` |
| P1.2 | **EQ** | Curva de respuesta frecuencia vs ganancia | `eq_dialog.dart:631-773` (CustomPaint, 200 puntos) |
| P1.3 | **Mixer** | Fade-out en stop (256 samples, 4 threads) | `engine_threads.cpp:77-86` |
| P1.5 | **UI** | Waveform por track (ring buffer PCM) | `home_screen.dart` (WaveformWidget en cada track card) |

### P2 — Baja prioridad
| # | Área | Mejora | Archivos clave |
|---|------|--------|----------------|
| P2.2 | **Pipeline** | Gap-less playback (encadenar tracks sin silencio) | `engine_threads.cpp` (flac_gapless, seek-to-end), `track_player.dart` (gapLessVersion polling) |
| P2.4 | **DSP** | Compresor (threshold, ratio, knee, attack, release, makeup) | `compressor.cpp`, `audio_mixer.dart:257-316` |
| P2.4 | **DSP** | Reverb (4 comb + 2 all-pass, pre-delay) | `reverb.cpp`, `audio_mixer.dart:318-370` |

### Configuración de publicación
- **Paquete renombrado:** `audio_engine` → `arc_engine`
- **Versión:** `0.1.0`
- **Repositorio:** github.com/FYSPA/Arc-Engine (privado)
- **Validación:** `dart format`, `flutter analyze`, `flutter test`, `dart pub publish --dry-run` — todo OK
- **minSdk:** `27` (AAudio callback)

---

## 📋 Pendientes

### P1 — Media prioridad

| # | Área | Mejora | Descripción |
|---|------|--------|-------------|
| P1.4 | **UI** | Drag & drop reorder | 🚫 Saltado — no se implementará |

### P2 — Baja prioridad

| # | Área | Mejora | Descripción |
|---|------|--------|-------------|
| P2.1 | **DSP** | EQ individual por track | Cada pista con su propio `DspProcessor` (actualmente EQ es global: `gCtl.dsp`) |
| P2.3 | **Pipeline** | Exportar mezcla a WAV | Mezclar todas las pistas activas y guardar como archivo WAV |
| P2.4b | **DSP** | Delay / Echo | Efecto de delay standalone (el reverb usa líneas internas pero no hay delay independiente) |
| P2.4c | **DSP** | Chorus | Efecto de chorus |
| P2.5 | **DSP** | Sidechain | Compresión sidechain para ducking automático entre pistas |
| P2.6 | **Grabación** | Entrada de micrófono | Pista en vivo desde el micrófono, mezclada con pistas locales |
| P2.7 | **Mixer** | Silence threshold refinado | Subir threshold de 1e-3 a 5e-2 para que preBufStart apunte directamente a audio real (actualmente sin(0) compensa el ruido residual, pero mayor precisión mejora diagnósticos) |

---

## 🔮 Visión a futuro (V2)

| # | Área | Mejora | Descripción |
|---|------|--------|-------------|
| V2.1 | **Pipeline** | Sample Rate Conversion (SRC) | Resampleo de tracks a la frecuencia del stream de salida |
| V2.2 | **Pipeline** | ReplayGain | Normalización de loudness (EBU R128) entre pistas |
| V2.3 | **Pipeline** | Dithering | Noise shaping al convertir a 16-bit si se exporta a WAV |
| V2.4 | **Grabación** | Loopback / Mezcla a WAV | Renderizar todas las pistas a un archivo WAV en disco |
| V2.5 | **Grabación** | Multitrack recording | Grabar cada pista por separado a archivos individuales |
| V2.6 | **IO** | Salida multicanal (5.1/7.1) | Soporte para más de 2 canales con downmix automático a stereo |
| V2.7 | **IO** | Bluetooth LE Audio | Soporte para codec LC3 en dispositivos BT compatibles |
| V2.8 | **IO** | USB Audio (UAC2) | Salida por interfaz de audio USB |
| V2.9 | **Formatos** | Opus | Decodificación Opus vía libopus |
| V2.10 | **Formatos** | DSD | Decodificación DSD (DSF/DFF) para audiófilos |
| V2.11 | **Formatos** | Listas de reproducción | Soporte M3U/M3U8/PLS |
| V2.12 | **Mixer** | Automatización | Envelopes de volumen/pan por track (grabables y editables) |
| V2.13 | **Mixer** | Buses / Grupos | Agrupar tracks en buses con procesamiento compartido |
| V2.14 | **Mixer** | VCA Faders | Faders de control que agrupan varios tracks sin pasar audio |
| V2.15 | **Mixer** | Sincronización BPM / tempo | Detectar y alinear tempo entre pistas |
