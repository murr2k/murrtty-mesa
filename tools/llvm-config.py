"""llvm-config for Meson, around the real one in LLVM's Windows release.

That release names, in --system-libs, libraries from its own build machine
that its archive doesn't carry: zlib as zs.lib, zstd by an absolute path on
that machine, and libxml2 as xml2s.lib. This wrapper puts the zlib and zstd
that build.ps1 builds (MURRTTY_ZLIB_LIB, MURRTTY_ZSTD_LIB) in their place and
leaves libxml2 out: only LLVM's Windows manifest code uses it, which
llvmpipe doesn't link. The real llvm-config is MURRTTY_LLVM_CONFIG.
"""
import os
import subprocess
import sys

result = subprocess.run([os.environ['MURRTTY_LLVM_CONFIG'], *sys.argv[1:]], capture_output=True, text=True)
out = result.stdout
if '--system-libs' in sys.argv[1:]:
    words = []
    for word in out.split():
        name = word.replace('\\', '/').rsplit('/', 1)[-1].lower()
        if name in ('zs.lib', 'z.lib', 'zlib.lib', 'zlibstatic.lib'):
            words.append(os.environ['MURRTTY_ZLIB_LIB'])
        elif name in ('zstd_static.lib', 'zstd.lib'):
            words.append(os.environ['MURRTTY_ZSTD_LIB'])
        elif name in ('xml2s.lib', 'libxml2s.lib', 'xml2.lib'):
            continue
        else:
            words.append(word)
    out = ' '.join(words) + '\n'
sys.stdout.write(out)
sys.stderr.write(result.stderr)
sys.exit(result.returncode)
