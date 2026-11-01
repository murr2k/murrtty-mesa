# murrtty-mesa

[Mesa](https://mesa3d.org)'s llvmpipe, a software OpenGL, built for
Windows x64 and ARM64 for [murrtty](https://murrtty.org)'s fallback on
machines whose graphics driver offers no OpenGL 4.3: virtual machines,
Remote Desktop without a GPU, and Windows on ARM laptops.

Each release holds `opengl32.dll` and `libgallium_wgl.dll` per
architecture, built by this repository's workflow on GitHub's hosted runners
(natively on x64 and ARM64) from:

- Mesa's source release, pinned by SHA-256 in [`versions.json`](versions.json);
- LLVM's source release, also pinned, whose libraries are built here for
  the target's architecture and linked statically;
- one pinned MSVC toolset for both, with a static C runtime, so the DLLs
  need nothing but Windows. LLVM is built here, not taken from its
  prebuilt Windows release: that release's libraries are compiled against
  a newer MSVC C++ library, and linked into Mesa they corrupted the heap
  linking any program whose fragment shader reads `gl_FragCoord.y` (and
  failed to link on ARM64). Building LLVM takes most of a run's hour or
  so.

Only llvmpipe and desktop OpenGL are built (no GLES, EGL, Vulkan, video or
OpenCL). Before packaging, [`probe/wglprobe.c`](probe/wglprobe.c) checks
each build the way murrtty uses it: it loads `opengl32.dll` by full path,
creates an OpenGL 4.3 core context, clears and reads the pixel back, then
links and draws a program like murrtty's (a fragment shader reading
`gl_FragCoord.y` and a uniform block) and checks that pixel too.
`BUILDINFO.txt` and `PROBE.txt` in each zip record the inputs and the result.
A manually started run with `diagnose` builds x64 with AddressSanitizer
instead and runs the probe under it; nothing from it is released.

Each release zip carries a build provenance attestation. To check that a
zip came from this repository's workflow:

```powershell
gh attestation verify murrtty-mesa-26.2.6-x64.zip -R murr2k/murrtty-mesa
```

To build locally, from a Visual Studio developer prompt for the target
architecture and the toolset `versions.json` pins (`vcvarsall x64
-vcvars_ver=14.44`), with CMake, Python (meson, ninja, mako) and
win_flex_bison on PATH:

```powershell
.\build.ps1 -Arch x64
```

The build scripts are MIT licensed ([LICENSE](LICENSE)). The licenses of
Mesa (its overview and its `licenses` folder) and LLVM are in each zip's
`licenses` folder. murrtty-mesa is not affiliated with the Mesa
or LLVM projects.
