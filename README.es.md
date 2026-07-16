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
    <a href="#-estructura-del-proyecto">Estructura</a> •
    <a href="#-licencia">Licencia</a>
  </p>
  <p>
    <a href="README.md">English Example</a> •
    <a href="./CONTRIBUTING.md">Contribuir</a>
  </p>
  <div align="center">
    <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT" /></a>
    <img src="https://img.shields.io/badge/Flutter-3.4+-02569B?logo=flutter" alt="Flutter 3.4+" />
    <img src="https://img.shields.io/badge/Android-API_27+-3DDC84?logo=android" alt="Android API 27+" />
  </div>
  <br>
</div>

---

## 📖 Acerca del Proyecto

**Arc Audio Engine (AAE)** es un plugin de Flutter de alto rendimiento que lleva la **decodificación y reproducción de audio multi-formato** al ecosistema Flutter en Android, utilizando **FFI** (Foreign Function Interface) para comunicarse directamente con código nativo C++.

Soporta **FLAC, WAV, MP3, AAC, y OGG** con salida de baja latencia mediante AAudio callback. Este plugin permite a los desarrolladores Flutter:

- **Reproducir** archivos FLAC, WAV, MP3, AAC y OGG nativamente
- Controles **Pausa / Reanudar / Buscar / Detener**
- Baja latencia con **AAudio callback** + **buffer ring SPSC** sin locks
- Señalización cross-thread mediante **eventfd** del kernel Linux
- Código C++ nativo puro — sin sobrecarga de puentes Java/Kotlin

> **Nota:** Usa `libFLAC` (decodificación), `AAudio` (salida audio), `AMediaCodec` (MP3/AAC/OGG), y parser WAV nativo — todo via **dart:ffi**!

---

## ✨ Características

<div align="center">

| Característica | Descripción |
|---|---|
| **Multi-formato** | FLAC (libFLAC), WAV (parser nativo), MP3/AAC/OGG (AMediaCodec) |
| **Reproducción Nativa** | AAudio callback con salida PCM float + buffer ring lock-free |
| **Controles** | Pausa / Reanudar / Buscar / Detener con señalización eventfd |
| **Puente FFI** | Comunicación directa C++ a Dart — sin platform channels |
| **Solo Android** | Optimizado para Android con AAudio y NDK (API 27+) |
| **Baja Latencia** | Buffer ring SPSC + AAudio callback para latencia mínima |

</div>

---

## ⚙️ Arquitectura

```
┌──────────────────────────────────────────────────────────────┐
│                     Flutter (Dart)                            │
│  ┌───────────────────────────────────────────────────────┐   │
│  │         AudioEngine (audio_engine.dart)                │   │
│  │  startAudio() │ stop() │ pause() │ resume() │ seek()   │   │
│  └───────────────┴────────┴─────────┴──────────┴────────┘   │
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
│  │          │         ▼ stop sig │  │mediaPlayback   │  │   │
│  └──────────┘  ┌──────────┐     │  │Thread          │  │   │
│                │aaudio_   │     │  └────────┬───────┘  │   │
│                │utils     │     │           │          │   │
│                │(create,  │     │           ▼          │   │
│                │ close)   │     │  ┌────────────────┐  │   │
│                └──────────┘     │  │  Ring Buffer   │  │   │
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

1. **Dart** llama a `AudioEngine.startAudio(ruta)` via FFI
2. **dispatcher.cpp** parsea la extensión y delega al handler de formato correspondiente
3. **Thread decodificador** (WAV/FLAC/Media) decodifica audio y envía samples float PCM al **buffer ring SPSC** (lock-free)
4. **AAudio callback** (`aaudioDataCallback`) se ejecuta en un thread de alta prioridad, obtiene samples del ring buffer y los envía al dispositivo
5. **Controles** (stop/pause/seek) usan señalización **eventfd** del kernel para comunicación cross-thread confiable

---

## 🚀 Empezando

### Requisitos

- Flutter SDK `>=3.4.0` (compatible con Dart `>=3.4.0`)
- Dispositivo o emulador Android con **API 27+** (requisito de AAudio)
- Android NDK (incluido con Android Studio)

### Instalación

Añade esto a tu `pubspec.yaml`:

```yaml
dependencies:
  audio_engine:
    git:
      url: https://github.com/FYSPA/<nombre-del-repo>.git
```

> Reemplaza `<nombre-del-repo>` con el nombre real del repositorio una vez creado.

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

> La app ejemplo busca un archivo FLAC en:
> `/storage/emulated/0/Android/data/com.example.audio_engine_example/files/test.flac`
>
> Puedes copiar uno con: `adb push test.flac /ruta/a/esa/carpeta/`

---

## 🎮 Uso

### Iniciar Reproducción

```dart
import 'package:audio_engine/audio_engine.dart';

// Iniciar reproducción (no bloqueante)
int resultado = AudioEngine.startAudio('/ruta/al/archivo.wav');
if (resultado == 0) print('Reproducción iniciada!');
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
| `-2`   | Cabecera WAV RIFF inválida           |
| `-3`   | Chunk WAV fmt inválido               |
| `-4`   | Formato WAV no soportado             |
| `-5`   | WAV sin chunk fmt                    |
| `-6`   | Error de lectura datos WAV           |
| `-7`   | WAV sin chunk data                   |
| `-8`   | Error al crear eventfd (señalización)|

---

## 🛠️ Stack Tecnológico

<div align="center">

| Capa | Tecnología | Propósito |
|------|------------|-----------|
| UI | **Flutter** | Framework UI multiplataforma |
| Puente | **dart:ffi** | Invocación directa a código nativo |
| Decodificación | **libFLAC** | Códec de audio FLAC |
| Bitstream | **libogg** | Contenedor Ogg (dependencia de FLAC) |
| Salida de Audio | **AAudio** (Android NDK) | Modo callback de baja latencia |
| Buffer Ring | **SPSC Personalizado** | Lock-free single-producer single-consumer |
| Señalización | **eventfd** | Señalización cross-thread via kernel |
| Codecs Media | **AMediaCodec** (NDK) | Reproducción MP3, AAC, OGG |

</div>

---

## 📁 Estructura del Proyecto

```
audio_engine/
├── lib/
│   └── audio_engine.dart            # Bindings FFI desde Dart
├── android/src/main/cpp/
│   ├── CMakeLists.txt               # Configuración de build nativo
│   ├── audio_engine.cpp             # AAudio callback + exports FFI
│   ├── dispatcher.cpp/.h            # Dispatch de formatos (start_audio)
│   ├── engine_state.cpp/.h          # Estado global (gCtl) + stopEngine
│   ├── engine_threads.cpp/.h        # Threads decodificadores
│   ├── aaudio_utils.cpp/.h          # Creación/gestión de streams AAudio
│   ├── ring_buffer.h                # Buffer ring SPSC lock-free
│   ├── wav_handler.cpp/.h           # Legacy WAV playback
│   ├── flac_handler.cpp/.h          # Legacy FLAC playback
│   ├── media_handler.cpp/.h         # Legacy media playback
│   ├── common.h                     # Macros y tipos compartidos
│   └── libs/                        # Librerías precompiladas
│       ├── include/                 #   Headers FLAC/Ogg
│       ├── arm64-v8a/               #   libFLAC.a + libogg.a (64-bit)
│       └── armeabi-v7a/             #   libFLAC.a + libogg.a (32-bit)
├── example/                          # App de ejemplo Flutter
├── LICENSE.md                        # Licencia MIT
├── CONTRIBUTING.md                   # Guía de contribución
├── ROADMAP.md                        # Roadmap de desarrollo
└── README.es.md                      # Este archivo
```

---

## ❓ FAQ

### Por qué usar eventfd en lugar de std::atomic para señalización?

En ciertos dispositivos Android (ej. Moto E6 Play con Snapdragon 427), la barrera de memoria del constructor de `std::thread` no garantiza que las escrituras del thread creador sean visibles para el nuevo thread. Ni `std::atomic`, `volatile`, ni `std::atomic_thread_fence` funcionaron de forma confiable entre threads. eventfd es una syscall del kernel que garantiza un ordenamiento de memoria correcto, haciendo la señalización cross-thread confiable independientemente de bugs en el modelo de memoria de la plataforma.

### Por qué AAudio callback en lugar de escrituras bloqueantes?

El modo callback se ejecuta en un thread de audio de alta prioridad, reduciendo la latencia y previniendo underruns. El buffer ring SPSC lock-free desacopla el thread decodificador del thread de audio, permitiendo reproducción estable incluso cuando la decodificación toma tiempo variable. `AAudioStream_write()` bloqueante se detendría si el decodificador no puede mantener el ritmo.

### Qué es el ring buffer?

Un buffer ring Single-Producer Single-Consumer (SPSC) lock-free con capacidad de 65536 muestras. El thread decodificador escribe muestras PCM float en el buffer, y el callback de AAudio las lee. No se necesitan mutexes ni operaciones atómicas en el hot path, solo ordenamiento de memoria cuidadoso con semántica acquire/release.

### Qué formatos están soportados?

FLAC (via libFLAC), WAV (parser nativo), MP3, AAC, y OGG (via AMediaCodec). WAV usa un parser zero-copy personalizado, FLAC usa el decodificador libFLAC de referencia, y los formatos comprimidos usan la API NDK AMediaCodec de Android.

### Qué nivel de API de Android se requiere?

Se requiere API 27+ para AAudio. El plugin usa el modo callback de AAudio que fue estabilizado en Android 8.1 (API 27).

### Cómo pruebo la reproducción?

Copia un archivo de prueba al dispositivo y presiona el botón de formato correspondiente en la app ejemplo:

```bash
adb push test.flac /storage/emulated/0/Android/data/com.example.audio_engine_example/files/
```

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
    <a href="./CONTRIBUTING.md">Cómo Contribuir</a>
  </p>
  <br>
</div>
