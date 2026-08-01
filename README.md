# Zetla

AI-powered Android app with on-device Python execution, voice chat, web search, and tool calling.

## Features

- **LLM Chat** - Multi-provider support (OpenCode Zen, DeepSeek, NVIDIA NIM) with SSE streaming
- **On-device Python** - Static CPython 3.14.6 binary with 93 modules, runs user code via `run_code` tool
- **Voice Chat** - Vosk offline speech recognition + Android TTS with waveform visualization
- **Web Search** - EXA AI MCP integration for real-time web search
- **Tool Calling** - Agent loop with max 20 iterations, reasoning streamed to UI
- **File Support** - Text, CSV, PDF, DOCX, PPTX, XLSX file parsing with preview chips
- **C++ Native Library** - Session management, encryption, SSE parsing, auto-compaction

## Architecture

```
Zetla/                  # Android project (Kotlin/Compose)
  app/                  # Application module, APK entry point
  domain/               # Models, repository interfaces
  data/                 # Repository implementations, JNI bridge, C++ CMake build
  ui/                   # Compose UI, ViewModels, voice chat
src/zetla/              # C++ source (DLL/shared library)
  api/                  # DLL API + JNI bridge
  session/              # Session manager with auto-compaction
  search/               # Web search (EXA MCP)
  storage/              # Local encrypted storage
  providers/            # LLM provider adapters
  file_handlers/        # File parsing (text, PDF, office)
  tools/                # Tool registry
```

## Prerequisites

- **Android Studio** with SDK 36 and NDK r28b
- **MinGW-w64** (for building libcurl/OpenSSL native deps)
- **WSL Ubuntu** (for building Python static binary)
- **Java 17+**

## Quick Start

```powershell
# Full setup (builds deps, downloads assets, builds APK)
.\scripts\setup.ps1

# Or step by step:
.\scripts\setup.ps1 -SkipDeps      # Skip native deps if already built
.\scripts\setup.ps1 -BuildApkOnly  # Just build the APK
```

## Build Steps

### 1. Native Dependencies (OpenSSL + libcurl)

```powershell
.\build_deps.bat
```

Builds static OpenSSL and libcurl for arm64-v8a and armeabi-v7a.

### 2. Python Runtime (WSL)

The Python binary is a static build of CPython 3.14.6 for Android, built using the reproducible build system at `~/zetla_android_build/`. See `docs/PYTHON_ANDROID_BUILD.md` for complete build documentation.

```bash
# In WSL Ubuntu - full build (deps + cpython + package)
wsl -d Ubuntu-26.04 bash -c 'cd ~/zetla_android_build && ./build_all.sh --clean'

# Or step by step:
wsl -d Ubuntu-26.04 bash -c 'cd ~/zetla_android_build && ./build_all.sh --step deps'
wsl -d Ubuntu-26.04 bash -c 'cd ~/zetla_android_build && ./build_all.sh --step cpython'
wsl -d Ubuntu-26.04 bash -c 'cd ~/zetla_android_build && ./build_all.sh --step package'
```

Output in `~/zetla_android_build/dist/bin/`:
| File | Size | Description |
|------|------|-------------|
| `python` | ~19MB | Static Python 3.14.6t binary |
| `python3.14t.zip` | ~27MB | Stdlib (zipimport) |
| `cacert.pem` | ~178KB | SSL CA certificates |

The build system (`scripts/setup.ps1`) picks up these files from WSL and copies them to:
- `Zetla/app/src/main/jniLibs/arm64-v8a/libpython.so`
- `Zetla/app/src/main/assets/python314t.zip`
- `Zetla/app/src/main/assets/cacert.pem`
- `Zetla/data/src/main/assets/python314t.zip`
- `Zetla/data/src/main/assets/cacert.pem`

### 3. Vosk Speech Model

Downloaded automatically by `scripts/setup.ps1`, or manually:

```powershell
# Download from https://alphacephei.com/vosk/models
# Extract to Zetla/app/src/main/assets/model-en-us/
```

### 4. Build APK

```powershell
cd Zetla
.\gradlew.bat assembleRelease
```

Output: `Zetla/app/build/outputs/apk/release/app-release.apk`

## API Keys

Set in `Zetla/ui/src/main/java/com/zetla/app/ZetlaApp.kt` or via the app's settings screen.

- `OPENCODE_API_KEY` - OpenCode Zen
- `DEEPSEEK_API_KEY` - DeepSeek
- `NVIDIA_API_KEY` - NVIDIA NIM

## TODO

- **Model capabilities not shown correctly** - vision, reasoning, and other capability flags from C++ providers are not reliably reflected in the Kotlin `ModelCapabilities` data class and ModelSelectionSheet UI.
- **Thinking level selection does not work** - `thinking_levels` are returned by providers but the reasoning effort chip selection in the UI does not apply correctly to API requests.
- **Local RAG Corpus** for multiple large document file support with tool definition for LLM to utilize the same. - DONE (See rag module in zetla cpp source)

## License

MIT - Copyright 2026 Aradhya Chakrabarti
