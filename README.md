# bgshader

`bgshader` is a small native Windows desktop shader experiment. It creates an
OpenGL-powered ribbon effect behind the desktop icons, with a notification-area
icon for shutdown, optional external GLSL overrides, and a utility mode for
reparenting an existing window into the desktop layer.

The project is intentionally minimal: one C source file, Win32 APIs, legacy
OpenGL calls, and no runtime framework.

## Features

- Runs a native animated desktop ribbon behind the Windows desktop icons.
- Uses GLSL programs for the background, ribbon, and point particles.
- Falls back to embedded shaders when no external shader file is found.
- Supports a custom shader file with named shader sections.
- Adds a tray icon named `Desktop Shader`; click it to open a shutdown menu.
- Can attach an existing top-level window to the desktop layer by HWND.
- Handles virtual desktop bounds for multi-monitor setups.

## Repository layout

```text
.
|-- main.c     # Win32/OpenGL renderer and CLI entry point
|-- app.exe    # Existing local build
`-- README.md
```

## Requirements

- Windows.
- A GPU/driver exposing OpenGL 2.0 shader functions.
- A C compiler for rebuilding, such as MinGW GCC.
- Standard Windows system libraries: `opengl32`, `gdi32`, `user32`, and
  `shell32`.

This program uses Windows desktop internals such as `Progman`, `WorkerW`, and
`SHELLDLL_DefView`. Those APIs are practical for desktop shader experiments, but
they are not a formal wallpaper plugin API and can vary across Windows versions
or shell configurations.

## Build

With MinGW GCC:

```powershell
gcc main.c -O2 -Wall -Wextra -o win32-desktop-layer.exe -lopengl32 -lgdi32 -luser32 -lshell32
```

For a quick syntax-only check:

```powershell
gcc -fsyntax-only main.c
```

The current source uses a normal `main`, so it builds as a console subsystem
program by default. Keeping the console is useful while developing because shader
compile errors and desktop attachment status are printed to standard output or
standard error.

## Quick start

Run the existing build:

```powershell
.\app.exe
```

Or run a fresh build:

```powershell
.\win32-desktop-layer.exe
```

When the native renderer starts successfully, it prints a line like:

```text
native-ribbon hwnd=0000000000123456 parent=0000000000ABCDEF size=3840x2160
```

Use the `Desktop Shader` tray icon to stop it:

1. Click the tray icon.
2. Choose `Shutdown Desktop Shader`.

If you cannot find the tray icon, check the hidden notification icons flyout.

## Command line

```text
win32-desktop-layer.exe [--shader <file.glsl>] [hwnd]
```

### Run the built-in desktop shader

```powershell
.\win32-desktop-layer.exe
```

No arguments starts the native ribbon renderer. The program first looks for an
external shader file named `win32-desktop-layer.glsl` next to the executable. If
that file is missing or incomplete, it uses the embedded fallback shaders from
`main.c`.

### Run with an explicit shader file

```powershell
.\win32-desktop-layer.exe --shader .\shaders\aurora.glsl
```

The equals form is also supported:

```powershell
.\win32-desktop-layer.exe --shader=.\shaders\aurora.glsl
```

The short form is supported too:

```powershell
.\win32-desktop-layer.exe -s .\shaders\aurora.glsl
```

### Attach an existing window to the desktop layer

The positional argument is interpreted as an HWND. Decimal and `0x` hexadecimal
values are accepted.

```powershell
.\win32-desktop-layer.exe 0x00123456
```

PowerShell example using Notepad:

```powershell
notepad
Start-Sleep -Milliseconds 500
$hwnd = (Get-Process notepad | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1).MainWindowHandle
.\win32-desktop-layer.exe $hwnd
```

This mode mutates the target window style and calls `SetParent`. Use it with
disposable test windows first. Some applications do not behave correctly after
being reparented into the desktop layer, and the program does not restore the
original window parent or style.

## Custom shader files

External shader files are plain text files containing six named sections. Each
section starts with `/// <name>` and ends with `/// </name>`.

Required sections:

- `bg_vertex`
- `bg_fragment`
- `ribbon_vertex`
- `ribbon_fragment`
- `point_vertex`
- `point_fragment`

The loader requires all six sections. If any section is missing, the file is
ignored and the embedded shaders are used instead. If the file loads but a shader
does not compile or link, the program prints the OpenGL error log and falls back
to the embedded shaders.

### Shader section template

Save this as `win32-desktop-layer.glsl` next to the executable to have it load
automatically, or pass it with `--shader`.

```glsl
/// <bg_vertex>
#version 120
varying vec2 vUv;

void main() {
    vUv = gl_Vertex.xy * 0.5 + 0.5;
    gl_Position = gl_Vertex;
}
/// </bg_vertex>

/// <bg_fragment>
#version 120
uniform vec2 uResolution;
uniform float uTime;
varying vec2 vUv;

void main() {
    vec2 p = (vUv - 0.5) * vec2(uResolution.x / max(1.0, uResolution.y), 1.0);
    float vignette = smoothstep(1.2, 0.15, length(p));
    vec3 color = mix(vec3(0.01, 0.04, 0.07), vec3(0.04, 0.01, 0.06), vUv.y);
    gl_FragColor = vec4(color, vignette * 0.35);
}
/// </bg_fragment>

/// <ribbon_vertex>
#version 120
varying vec2 vUv;
varying vec3 vColor;

void main() {
    vUv = gl_MultiTexCoord0.xy;
    vColor = gl_Color.rgb;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
/// </ribbon_vertex>

/// <ribbon_fragment>
#version 120
uniform float uTime;
uniform float uGrungeGlow;
varying vec2 vUv;
varying vec3 vColor;

void main() {
    float d = abs(vUv.y - 0.5) * 2.0;
    float core = exp(-d * d * 5.0);
    float halo = exp(-d * d * 1.2);
    float shimmer = 0.8 + 0.2 * sin(vUv.x * 80.0 + uTime * 2.0);
    float alpha = (halo * 0.18 + core * 0.45) * shimmer * (1.0 + uGrungeGlow * 0.15);
    gl_FragColor = vec4(vColor, alpha);
}
/// </ribbon_fragment>

/// <point_vertex>
#version 120
uniform float uWake;
varying vec3 vColor;
varying float vAlpha;

void main() {
    vColor = gl_Color.rgb * (1.0 + uWake * 0.5);
    vAlpha = gl_Color.a;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    gl_PointSize = gl_Color.a * (3.0 + uWake * 3.0);
}
/// </point_vertex>

/// <point_fragment>
#version 120
varying vec3 vColor;
varying float vAlpha;

void main() {
    vec2 uv = gl_PointCoord - 0.5;
    float d = dot(uv, uv);
    float alpha = exp(-d * 14.0) * vAlpha * 0.35;
    gl_FragColor = vec4(vColor, alpha);
}
/// </point_fragment>
```

### Uniforms exposed by the renderer

Background shader:

- `uniform vec2 uResolution`
- `uniform float uTime`

Ribbon shader:

- `uniform float uTime`
- `uniform float uGrungeGlow`

Point shader:

- `uniform float uWake`

The renderer uses OpenGL 2.0 / GLSL 1.20 style built-ins such as `gl_Vertex`,
`gl_Color`, `gl_MultiTexCoord0`, `gl_ModelViewProjectionMatrix`, and
`gl_PointCoord`.

## Usage examples

### Use the default embedded effect

```powershell
.\app.exe
```

### Keep a shader file next to the executable

```powershell
Copy-Item .\examples\soft-ribbon.glsl .\win32-desktop-layer.glsl
.\win32-desktop-layer.exe
```

The executable automatically attempts to load `win32-desktop-layer.glsl` from
its own directory before using the embedded fallback.

### Run different shader variants

```powershell
.\win32-desktop-layer.exe --shader .\shaders\blue-ribbon.glsl
.\win32-desktop-layer.exe --shader .\shaders\warm-ribbon.glsl
.\win32-desktop-layer.exe --shader .\shaders\minimal-lines.glsl
```

### Capture shader compile errors

```powershell
.\win32-desktop-layer.exe --shader .\shaders\broken.glsl 2> shader-errors.txt
Get-Content .\shader-errors.txt
```

### Attach a browser window by HWND

This example finds the first visible Microsoft Edge window and attaches it to
the desktop layer:

```powershell
$edge = Get-Process msedge | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
.\win32-desktop-layer.exe $edge.MainWindowHandle
```

If the command prints `Invalid HWND`, the process either has no current top-level
window or the selected window handle is stale.

### Check whether the process is still running

```powershell
Get-Process app, win32-desktop-layer -ErrorAction SilentlyContinue
```

Stop it from PowerShell if needed:

```powershell
Stop-Process -Name app -ErrorAction SilentlyContinue
Stop-Process -Name win32-desktop-layer -ErrorAction SilentlyContinue
```

## Runtime behavior

- The native renderer creates a child window under the behind-icons desktop
  `WorkerW` window when available.
- The render size follows the virtual desktop bounds, so it can cover multiple
  monitors.
- Pointer movement influences the ribbon wake and particles.
- Clicking near the ribbon perturbs its phase and briefly intensifies the wake.
- The render loop caps large frame deltas and sleeps briefly when frames arrive
  too quickly.

## Troubleshooting

### The program exits immediately

Run it from PowerShell or Command Prompt so you can see stderr:

```powershell
.\win32-desktop-layer.exe 2> error.log
Get-Content .\error.log
```

Common causes are:

- The Windows shell desktop parent could not be found.
- OpenGL context creation failed.
- Required OpenGL 2.0 shader entry points are unavailable.

### The shader file is ignored

Check that all six required section markers are present and spelled exactly:

```text
/// <bg_vertex>
/// </bg_vertex>
```

The parser is simple and searches for exact marker text. It does not understand
JSON, INI, YAML, or nested sections.

### A custom shader compiles but draws nothing

Check alpha values first. The renderer uses blending, so a fragment shader that
returns alpha `0.0` will be invisible. Also confirm that point shaders set
`gl_PointSize` to a visible value.

### The tray icon is missing

Open the hidden notification icons flyout. If the tray icon failed to register,
the program continues running and prints:

```text
Could not add notification area icon.
```

In that case, stop the process from Task Manager or PowerShell.

### Desktop icons or shell behavior looks wrong

Restart Explorer from Task Manager, or from PowerShell:

```powershell
Stop-Process -Name explorer
Start-Process explorer.exe
```

Use this only when needed; restarting Explorer closes shell windows and resets
some desktop state.

## Development notes

- `SAMPLE_COUNT` controls the ribbon centerline samples.
- `PARTICLE_COUNT` controls the number of rendered point particles.
- The fallback shader strings live directly in `main.c`.
- External shader files are limited to 1 MiB by the loader.
- The program uses immediate-mode OpenGL for simplicity and compatibility with
  the current code shape.

## License

No license file is currently included. Add one before publishing this repository
if you want others to have explicit permission to use, modify, or redistribute
the code.
