# PROJECT KNOWLEDGE BASE

**Generated:** 2025-08-10

## OVERVIEW
Project: **ttyd** - Share your terminal over the web
Stack: C (backend) + TypeScript/Preact (frontend) with CMake and Webpack

## STRUCTURE

```
C:/dev/ttyd/
|-- src/                   # C backend source code
|   |-- server.c/h         # WebSocket server (libwebsockets)
|   |-- pty.c/h            # PTY handling (libuv)
|   |-- http.c             # HTTP request handling
|   |-- protocol.c        # Terminal protocol (xterm)
|   |-- utils.c/h          # Utility functions
|   |-- compat.h           # Windows compatibility
|   |-- html.h             # Compiled frontend (auto-generated)
|
|-- html/                  # Frontend TypeScript/Preact code
|   |-- src/
|   |   |-- index.tsx      # Entry point
|   |   |-- components/    # Preact components
|   |   |   |-- app.tsx            # Main app with Panel layout
|   |   |   |-- modal/             # Modal dialogs
|   |   |   |-- terminal/          # xterm.js terminal component
|   |   |   |-- fileExplorer/      # File Explorer feature
|   |   |   |   |-- index.tsx      # FileExplorer container (uses react-resizable-panels)
|   |   |   |   |-- DirectoryTree.tsx
|   |   |   |   |-- FileItem.tsx
|   |   |   |   |-- FileEditor.tsx
|   |   |   |   |-- styles.scss    # Styles for resizable panels, explorer UI
|   |   |   |   |-- api.ts         # File read/write API
|   |   |   |   |-- types.ts       # TypeScript interfaces
|   |   |-- style/         # SCSS styles
|   |   |-- template.html  # HTML template
|   |-- package.json       # Frontend dependencies (yarn)
|   |-- webpack.config.js  # Webpack bundler config
|   |-- gulpfile.js        # Build/gzip tasks
|   |-- tsconfig.json      # TypeScript config (gts)
|
|-- cmake/                 # CMake modules
|   |-- GetGitVersion.cmake
|
|-- scripts/               # Build scripts
|   |-- cross-build.sh     # Cross-compilation for multiple architectures
|
|-- .github/workflows/     # CI/CD
|   |-- backend.yml        # C backend build (MSVC + cross-compile)
|   |-- frontend.yml       # TypeScript build/check
|   |-- docker.yml        # Docker image build
|   |-- release.yml        # Release automation
|
|-- CMakeLists.txt         # Main CMake build file
|-- Dockerfile             # Docker build
|-- man/ttyd.1             # Man page
```

## COMMANDS

### Backend (C)
| Action | Command |
|--------|---------|
| Configure | `cmake -S . -B build` |
| Build | `cmake --build build` |
| Install | `cmake --install build` |

### Frontend (html/)
| Action | Command |
|--------|---------|
| Install | `corepack enable && yarn install` |
| Dev server | `yarn start` |
| Build | `yarn build` |
| Inline assets | `yarn inline` |
| Lint/Check | `yarn run check` |
| Auto-fix | `yarn run fix` |

### Full Build (production)
1. Build frontend: `cd html && yarn build` (generates `src/html.h`)
2. Inline assets: `cd html && yarn inline`
3. Build backend: `cmake --build build`

### Windows Build (with vcpkg)

**Prerequisites:**
- Visual Studio 2022 Build Tools with "Desktop development with C++" workload
- vcpkg installed at `C:\vcpkg`

**Step 1: Install vcpkg dependencies**
```powershell
C:\vcpkg\vcpkg.exe install --triplet x64-windows-static ^
  libwebsockets libuv json-c zlib openssl getopt-win32 dirent
```

**Step 2: Configure CMake**
```powershell
# Load VS Developer Shell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64

# Configure
cmake -S . -B build ^
  -G "Visual Studio 17 2022" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
```

**Step 3: Build**
```powershell
cmake --build build --config Release
```

**Step 4: Run**
```powershell
# Kill existing process if any
Get-Process -Name ttyd -ErrorAction SilentlyContinue | Stop-Process -Force

# Run
Start-Process -FilePath 'build\Release\ttyd.exe' ^
  -ArgumentList '-p 8090','-w C:\Dev\ttyd','powershell' -NoNewWindow
```

**One-liner for rebuild (after changes):**
```powershell
Get-Process -Name ttyd -ErrorAction SilentlyContinue | Stop-Process -Force
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64
cmake --build build --config Release
Start-Process -FilePath 'build\Release\ttyd.exe' -ArgumentList '-p 8090','-w C:\Dev\ttyd','powershell' -NoNewWindow
```

## CODING STANDARDS

### Backend (C)
- **Style**: Google C++ style (via `.clang-format`)
- **Format config**: ColumnLimit: 120, IndentWidth: 2, UseTab: Never
- **Standard**: C99
- **Key headers**: `libwebsockets.h`, `uv.h`, `json.h`
- **Notable patterns**:
  - `container_of()` macro pattern for linked lists
  - `xmalloc()`/`xrealloc()` for allocation with NULL checks
  - `#ifdef _WIN32` for cross-platform compatibility

### Frontend (TypeScript/Preact)
- **Style**: ESLint (gts: Google TypeScript Style) + Prettier
- **JSX factory**: `h` (Preact's hyperscript pragma, not React)
- **Config files**:
  - `.eslintrc.json`: Extends gts with Preact overrides
  - `.prettierrc.js`: TabWidth 4, printWidth 120
- **Patterns**:
  - Functional components with hooks
  - SCSS for styling
  - Uses xterm.js for terminal emulation

## DEPENDENCIES

### Backend Libraries (vcpkg: x64-windows-static)
| Library | Purpose | Min Version |
|---------|---------|-------------|
| libuv | Async I/O, PTY | 1.x |
| libwebsockets | WebSocket server | 3.2.0 |
| json-c | JSON parsing | 0.x |
| zlib | Compression | system |
| OpenSSL | SSL/TLS | 3.x |
| getopt-win32 | Command-line parsing | 2.x |
| dirent | Directory entries (Windows) | 1.x |

### Frontend Key Packages
| Package | Purpose |
|---------|---------|
| @xterm/xterm | Terminal emulator |
| @xterm/addon-* | xterm extensions (canvas, webgl, clipboard, etc.) |
| preact | UI framework |
| trzsz | File transfer (ZMODEM alternative) |
| zmodem.js | ZMODEM protocol |
| react-resizable-panels | Resizable panel layout | 4.x |

## NOTES

- **Embedded frontend**: The compiled frontend is embedded into C via `src/html.h` (generated by gulp inline task)
- **Cross-platform**: Windows (MSVC), Linux (GCC/Clang), macOS, FreeBSD, OpenWrt, etc.
- **Windows specifics**: Uses vcpkg with `x64-windows-static` triplet, `_WIN32_WINNT=0xa00` (Windows 10+)
- **Windows compatibility**: `src/compat.h` provides shims for `strcasecmp`, `strncasecmp`, `rmdir`
- **Static assets**: Sixel images, ZMODEM file transfer, clipboard integration
- **Git versioning**: Version extracted from git tags (SEM_VER) and commit hash

## DEVELOPMENT RULES

### Port Assignment
- **Dev port: 8090** - Always use this port for local development
- **Never assign new ports** without explicit user approval
- When port is occupied, kill the process first (via `taskkill //F //PID <pid>`) before reusing

### Build & Run Workflow (Windows)

**Complete rebuild cycle:**
```powershell
# 1. Kill existing process
Get-Process -Name ttyd -ErrorAction SilentlyContinue | Stop-Process -Force

# 2. Build frontend (if changed)
cd html; yarn build; yarn inline; cd ..

# 3. Build backend
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64
cmake --build build --config Release

# 4. Run
Start-Process -FilePath 'build\Release\ttyd.exe' -ArgumentList '-p 8090','-w C:\Dev\ttyd','powershell' -NoNewWindow
```

**Linux/macOS:**
```bash
# Build frontend
cd html && yarn build && yarn inline && cd ..

# Build backend
cmake --build build

# Run
./build/ttyd -p 8090 -w "$(pwd)" bash
```

**Windows note**: On Windows, use `powershell` (or `cmd`) as the command - `bash` exits immediately with code 1 because it's a WSL/bash process that cannot run in a PTY.
