<div align="center">
  <br>
  <img src="https://raw.githubusercontent.com/github/explore/80688e429a7d4ef2fca1e82350fe8e3517d3494d/topics/flutter/flutter.png" alt="Flutter" width="80" />
  <img src="https://raw.githubusercontent.com/github/explore/80688e429a7d4ef2fca1e82350fe8e3517d3494d/topics/android/android.png" alt="Android" width="80" />
  <br>
  <br>
  <h1>Arc Audio Engine (AAE)</h1>
  <p>
    <strong>Plugin nativo de Flutter para reproducción multi-formato en Android</strong>
  </p>
  <p>
    <a href="#-acerca-del-proyecto">Acerca</a> •
    <a href="#-características">Características</a> •
    <a href="#-arquitectura">Arquitectura</a> •
    <a href="#-empezando">Empezando</a> •
    <a href="#-uso">Uso</a> •
    <a href="#-licencia">Licencia</a>
  </p>
  <p>
    <a href="README.md">Read in English</a> •
    <a href="./CONTRIBUTING.md">Contribuir</a>
  </p>
  <div align="center">
    <a href="LICENSE.md"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT" /></a>
    <img src="https://img.shields.io/badge/Flutter-3.44-02569B?logo=flutter" alt="Flutter 3.44" />
    <img src="https://img.shields.io/badge/Android-API_27+-3DDC84?logo=android" alt="Android API 27+" />
  </div>
  <br>
</div>

---

## 📖 Acerca del Proyecto

**Arc Audio Engine (AAE)** es un plugin de Flutter de alto rendimiento que lleva la **decodificación y reproducción de audio multi-formato** al ecosistema Flutter en Android, utilizando **FFI** (Foreign Function Interface) para comunicarse directamente con código nativo C++.

Soporta **FLAC, WAV, MP3, AAC, y OGG** con salida de baja latencia mediante AAudio callback. También soporta **streaming por URL** via Android MediaExtractor (API 29+) con fallback automático a descarga local en dispositivos más antiguos.

- **Reproducir** archivos FLAC, WAV, MP3, AAC y OGG nativamente
- **Streaming** desde URLs HTTP directas
- Controles **Pausa / Reanudar / Buscar / Detener**
- Baja latencia con **AAudio callback** + **buffer ring SPSC** sin locks
- Señalización cross-thread mediante **eventfd** del kernel Linux
- Código C++ nativo puro — sin sobrecarga de puentes Java/Kotlin

> **Nota:** Usa `libFLAC` (decodificación), `AAudio` (salida audio), `AMediaCodec` / `AMediaExtractor` (MP3/AAC/OGG streaming), y parser WAV nativo — todo via **dart:ffi**!

---

## ✨ Características

| Característica | Descripción |
|---|---|
| **Multi-formato** | FLAC (libFLAC), WAV (parser nativo), MP3/AAC/OGG (AMediaCodec) |
| **Streaming por URL** | Streaming HTTP nativo (API 29+) con fallback de descarga para dispositivos antiguos. Diálogo de URL configurable con barra de progreso y cancelación. |
| **Reproducción Nativa** | AAudio callback con salida PCM float + buffer ring lock-free |
| **Controles** | Pausa / Reanudar / Buscar / Detener con señalización eventfd |
| **PCM Stream a Dart** | Stream en tiempo real de samples PCM para visualización (VU meter, waveform) |
| **Selector de archivos** | Importa archivos de audio via SAF (FLAC, WAV, MP3, AAC, OGG, M4A) |
| **Puente FFI** | Comunicación directa C++ a Dart — sin platform channels |
| **Baja Latencia** | Buffer ring SPSC + AAudio callback para latencia mínima |

---

## ⚙️ Arquitectura

```
┌──────────────────────────────────────────────────────────────┐
│                     Flutter (Dart)                            │
│  ┌───────────────────────────────────────────────────────┐   │
│  │         AudioEngine (audio_engine.dart)                │   │
│  │  startAudio() │ streamUrl() │ stop() │ pause()        │   │
│  │  resume() │ seek() │ startPcmStream()                  │   │
│  └───────────────┴────────────┴─────────┴────────────────┘   │
│                          │ dart:ffi                          │
└──────────────────────────┼──────────────────────────────────┘
                           ▼
┌──────────────────────────────────────────────────────────────┐
│              C++ Nativo — libaudio_engine.so                  │
│                                                              │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │dispatcher│─▶│engine_state   │  │  Threads Decodific.  │   │
│  │.cpp      │  │(gCtl, stop,  │  │  ┌────────────────┐  │   │
│  │          │  │ reset)       │  │  │wavPlaybackThread│  │   │
│  │start_    │  └──────┬───────┘  │  │flacPlayback    │  │   │
│  │audio()   │         │ eventfd  │  │Thread          │  │   │
│  │start_    │         ▼ stop sig │  │mediaPlayback   │  │   │
│  │media_    │  ┌──────────┐     │  │Thread          │  │   │
│  │stream()  │  │aaudio_   │     │  │mediaStream     │  │   │
│  └──────────┘  │utils     │     │  │PlaybackThread  │  │   │
│                │(create,  │     │  └────────┬───────┘  │   │
│                │ close)   │     │           │          │   │
│                └──────────┘     │           ▼          │   │
│                                 │  ┌────────────────┐  │   │
│                                 │  │  Ring Buffer   │  │   │
│                                 │  │ (SPSC lock-free)│  │   │
│                                 │  └────────┬───────┘  │   │
│                                 └───────────┼──────────┘   │
│                                             │              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  │              │
│  │wav_      │  │flac_     │  │media_    │  │              │
│  │handler   │  │handler   │  │handler   │  │              │
│  │(legacy)  │  │(legacy)  │  │(legacy)  │  │              │
│  └──────────┘  └──────────┘  └──────────┘  │              │
│                                             ▼              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │     AAudio Callback (audio_engine.cpp)                │  │
│  │  aaudioDataCallback() — lee del ring buffer           │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
                  Android Audio HAL
```

### Flujo de Datos

1. **Dart** llama a `AudioEngine.startAudio(ruta)` o `AudioEngine.streamUrl(url)` via FFI
2. **dispatcher.cpp** enruta al handler apropiado (archivo local por extensión, o stream URL via MediaExtractor)
3. **Thread decodificador** (WAV/FLAC/Media/Stream) decodifica audio y envía samples float PCM al **buffer ring SPSC** (lock-free)
4. **AAudio callback** (`aaudioDataCallback`) se ejecuta en un thread de alta prioridad, obtiene samples del ring buffer y los envía al dispositivo
5. **Controles** (stop/pause/seek) usan señalización **eventfd** del kernel para comunicación cross-thread confiable

---

## 🚀 Empezando

### Requisitos

- Flutter SDK `3.44.5` (compatible con Dart `>=3.12.2`)
- Dispositivo o emulador Android con **API 27+** (requisito de AAudio)
- Android NDK (incluido con Android Studio)

### Instalación

Añade esto a tu `pubspec.yaml`:

```yaml
dependencies:
  audio_engine:
    git:
      url: https://github.com/FYSPA/Arc-Engine.git
```

O si estás desarrollando localmente:

```yaml
dependencies:
  audio_engine:
    path: ./audio_engine
```

Luego:

```bash
flutter pub get
```

### Verificación

Para verificar que el plugin funciona correctamente, puedes usar la [aplicación de ejemplo](./example/):

```bash
cd example
flutter pub get
flutter run
```

> La app ejemplo usa `path_provider` para el directorio interno y `file_picker` (SAF) para importar archivos de audio, sorteando las restricciones de scoped storage en Android 11+.

---

## 🎮 Uso

### Iniciar Reproducción Local

```dart
import 'package:audio_engine/audio_engine.dart';

int resultado = AudioEngine.startAudio('/ruta/al/archivo.wav');
if (resultado == 0) print('Reproducción iniciada!');
```

### Streaming por URL

```dart
// Intentar streaming nativo (devuelve 0 si funciona, negativo si no)
int resultado = AudioEngine.streamUrl('https://ejemplo.com/audio.mp3');
if (resultado == 0) {
  print('Streaming...');
} else {
  print('Streaming no disponible, usar descarga local');
}
```

> El streaming nativo requiere Android API 29+. En dispositivos antiguos, `streamUrl()` devuelve un código de error.

### Stream PCM para Visualización

```dart
Stream<List<double>> pcmStream = AudioEngine.startPcmStream(
  interval: Duration(milliseconds: 50),
);

pcmStream.listen((samples) {
  actualizarVisualizador(samples);
});
```

### Controles

```dart
AudioEngine.stop();     // Detener reproducción
AudioEngine.pause();    // Pausar
AudioEngine.resume();   // Reanudar
AudioEngine.seek(ms);   // Buscar posición en milisegundos
```

### Consultas de Estado

```dart
bool reproduciendo = AudioEngine.isPlaying;
int pos = AudioEngine.getPosition();
int dur = AudioEngine.getDuration();
```

### Códigos de Error

| Código | Descripción                          |
|:------:|--------------------------------------|
| `0`    | Éxito                                |
| `-1`   | Archivo no encontrado / sin extensión |
| `-2`   | Cabecera WAV RIFF inválida / Streaming no soportado |
| `-3`   | Chunk WAV fmt inválido               |
| `-4`   | Formato WAV no soportado             |
| `-5`   | WAV sin chunk fmt                    |
| `-6`   | Error de lectura datos WAV           |
| `-7`   | WAV sin chunk data                   |
| `-8`   | Error al crear eventfd (señalización)|

---

## 🛠️ Stack Tecnológico

| Capa | Tecnología | Propósito |
|------|------------|-----------|
| UI | **Flutter** | Framework UI multiplataforma |
| Puente | **dart:ffi** | Invocación directa a código nativo |
| Decodificación | **libFLAC** (Xiph.Org) | Códec de audio FLAC |
| Bitstream | **libogg** | Contenedor Ogg (dependencia de FLAC) |
| Salida de Audio | **AAudio** (Android NDK) | Modo callback de baja latencia |
| Buffer Ring | **SPSC Personalizado** | Lock-free single-producer single-consumer |
| Señalización | **eventfd** | Señalización cross-thread via kernel |
| Codecs Media | **AMediaCodec** (NDK) | Reproducción MP3, AAC, OGG |
| Streaming URL | **AMediaExtractor** (NDK) | Streaming HTTP de audio (API 29+) |
| Navegación archivos | **file_picker** | Importación de archivos via SAF |
| Preferencias | **shared_preferences** | Persistencia de URL de streaming |
| Almacenamiento | **path_provider** | Directorio interno de documentos |

---

## ❓ FAQ

### Por qué usar eventfd en lugar de std::atomic para señalización?

En ciertos dispositivos Android (ej. Moto E6 Play con MediaTek MT6739), la barrera de memoria del constructor de `std::thread` no garantiza que las escrituras del thread creador sean visibles para el nuevo thread. Ni `std::atomic`, `volatile`, ni `std::atomic_thread_fence` funcionaron de forma confiable entre threads. eventfd es una syscall del kernel que garantiza un ordenamiento de memoria correcto, haciendo la señalización cross-thread confiable independientemente de bugs en el modelo de memoria de la plataforma.

### Por qué AAudio callback en lugar de escrituras bloqueantes?

El modo callback se ejecuta en un thread de audio de alta prioridad, reduciendo la latencia y previniendo underruns. El buffer ring SPSC lock-free desacopla el thread decodificador del thread de audio, permitiendo reproducción estable incluso cuando la decodificación toma tiempo variable. `AAudioStream_write()` bloqueante se detendría si el decodificador no puede mantener el ritmo.

### Qué es el ring buffer?

Un buffer ring Single-Producer Single-Consumer (SPSC) lock-free con capacidad de 65536 muestras. El thread decodificador escribe muestras PCM float en el buffer, y el callback de AAudio las lee. No se necesitan mutexes ni operaciones atómicas en el hot path, solo ordenamiento de memoria cuidadoso con semántica acquire/release.

### Qué formatos están soportados?

FLAC (via libFLAC), WAV (parser nativo), MP3, AAC, y OGG (via AMediaCodec). WAV usa un parser zero-copy personalizado, FLAC usa el decodificador libFLAC de referencia, y los formatos comprimidos usan la API NDK AMediaCodec de Android.

### El streaming por URL funciona en todos los dispositivos?

El streaming nativo via `AMediaExtractor_setDataSource()` requiere **Android API 29+**. En dispositivos antiguos o donde el streaming nativo falla, la app ejemplo descarga el archivo y lo reproduce localmente. Puedes implementar tu propio flujo de descarga detectando el código de error.

### Qué nivel de API de Android se requiere?

Se requiere API 27+ para AAudio. El plugin usa el modo callback de AAudio que fue estabilizado en Android 8.1 (API 27).

### Cómo agrego archivos de audio desde mi dispositivo?

Usa el botón **Pick Files** (tarjeta inferior) para abrir el selector de archivos del sistema. En Android 11+, esto usa SAF (Storage Access Framework) que sortea las restricciones de scoped storage. Los archivos seleccionados se copian al directorio interno de la app para reproducción. Puedes seleccionar múltiples archivos a la vez.

---

## 📄 Licencia

Distribuido bajo la licencia MIT. Consulta [`LICENSE.md`](LICENSE.md) para más información.

---

<div align="center">
  <br>
  <p>
    Hecho por <strong>FYSPA</strong> usando <strong>Flutter</strong> + <strong>C++</strong> + <strong>FLAC</strong>
  </p>
  <p>
    <a href="README.md">Ver README en Inglés</a> •
    <a href="./CONTRIBUTING.md">Cómo Contribuir</a> •
    <a href="./ROADMAP.md">Roadmap</a>
  </p>
  <br>
</div>
