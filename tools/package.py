#!/usr/bin/env python3
"""Assembles a release WITHOUT any game code: the runtime plus everything the first start needs
to build the game module from the player's own WAD (tools/make_module.py: the recompiler, its
tables, the module-side runtime sources, Zig, and on Windows an embeddable Python with pip).
Build the runtime first (tools/build.ps1, or CMake with -DUSE_SDL=ON on Linux).

usage: python tools/package.py [--zip] [--out dist-release] [--bin <runtime build dir>]
  writes <out>/ContraReBirthEnhancedXD and, with --zip, the release archive:
  ContraReBirthEnhancedXD-windows-x64.zip / ContraReBirthEnhancedXD-linux-x64.tar.xz

Downloads (cached in src/build-deps/package, each checked against its pinned SHA-256):
Zig 0.17.0 (ziglang.org), Python 3.12.10 embeddable (python.org, Windows) and pip (PyPI).
ZIG_DIR=<unpacked zig> uses a local copy instead of downloading Zig.
"""
import hashlib, os, shutil, subprocess, sys, tarfile, urllib.request, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = 'ContraReBirthEnhancedXD'
WIN = os.name == 'nt'
CACHE = os.path.join(ROOT, 'src', 'build-deps', 'package')

ZIG = ('https://ziglang.org/download/0.17.0/zig-x86_64-windows-0.17.0.zip',
       'b5663f69581dcf391293fbf16c06cb80d81d806545ce618b4d0bab7f0eb8c428') if WIN else \
      ('https://ziglang.org/download/0.17.0/zig-x86_64-linux-0.17.0.tar.xz',
       '1cbe9df9f27e6b78d14ccbca43b6703a404ef79ef1c463de901d7f088d4e2026')
PYTHON = ('https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip',
          '4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3')
PIP = ('https://files.pythonhosted.org/packages/3f/06/3e4420302363fbb71be3c7f35ce042f54bfca330e3e66b3a2e3c4cca579f/'
       'pip-26.2.2-py3-none-any.whl', 'fd7f4dfa8190e014013b9f08f3e973f4e4c26ffd179a204895533a7254813a70')

# release path -> repository path
FILES = {
    'tools/make_module.py': 'tools/make_module.py',
    'tools/prepare_wad.py': 'tools/prepare_wad.py',
    'tools/lz11.py': 'tools/lz11.py',
    'tools/recomp/recomp.py': 'src/recomp.py',
    'tools/recomp/dol.py': 'src/dol.py',
    'tools/recomp/ppcdec.py': 'src/ppcdec.py',
    'tools/recomp/hle.txt': 'src/hle.txt',
    'tools/recomp/hooks.txt': 'src/hooks.txt',
    'tools/config/symbols.txt': 'config/symbols.txt',
    'tools/runtime/ppc.h': 'src/runtime/ppc.h',
    'tools/runtime/module_abi.h': 'src/runtime/module_abi.h',
    'tools/runtime/module_rt.cpp': 'src/runtime/module_rt.cpp',
    'README.md': 'README.md',
    'LICENSE': 'LICENSE',
    'THIRD_PARTY_NOTICES.md': 'THIRD_PARTY_NOTICES.md',
    'gamecontrollerdb.txt': 'thirdparty/gamecontrollerdb.txt',
}


def fetch(url, sha):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, url.rsplit('/', 1)[1])
    if not os.path.exists(path):
        print('downloading', url)
        tmp = path + '.part'
        urllib.request.urlretrieve(url, tmp)
        os.replace(tmp, path)
    got = hashlib.sha256(open(path, 'rb').read()).hexdigest()
    if got != sha:
        os.remove(path)
        sys.exit(f'error: {url} has SHA-256 {got}, expected {sha}')
    return path


def zig_tree():
    """An unpacked Zig: ZIG_DIR, or the pinned download."""
    if os.environ.get('ZIG_DIR'):
        return os.environ['ZIG_DIR']
    archive = fetch(*ZIG)
    dest = archive[:-4] if WIN else archive[:-7]
    if not os.path.exists(dest):
        if WIN:
            zipfile.ZipFile(archive).extractall(CACHE)
        else:
            tarfile.open(archive).extractall(CACHE, filter='data')
    return dest


def put_zig(dest):
    """Zig trimmed to the one target the module is built for (x86_64 Windows/MinGW or Linux/glibc)."""
    keep_libc = {'mingw'} if WIN else {'glibc'}
    keep_inc = {'any-windows-any'} if WIN else {'any-linux-any', 'generic-glibc', 'x86_64-linux-gnu', 'x86-linux-any', 'x86_64-linux-any'}

    def ignore(d, names):
        rel = os.path.relpath(d, src).replace('\\', '/')
        skip = {'doc', 'docs', 'libtsan', 'build-web', 'lldb'} & set(names) if rel in ('.', 'lib') else set()
        if rel == 'lib/libc':
            skip |= {n for n in names if n != 'include' and n not in keep_libc and os.path.isdir(os.path.join(d, n))}
        if rel == 'lib/libc/include':
            skip |= {n for n in names if n not in keep_inc}
        return skip
    src = zig_tree()
    shutil.copytree(src, dest, ignore=ignore)


def main():
    args = sys.argv[1:]
    out = args[args.index('--out') + 1] if '--out' in args else os.path.join(ROOT, 'dist-release')
    bindir = args[args.index('--bin') + 1] if '--bin' in args else os.path.join(ROOT, 'src', 'build-win' if WIN else 'build-linux')
    exe = os.path.join(bindir, 'contrarebirth.exe' if WIN else 'contrarebirth')
    if not os.path.exists(exe):
        sys.exit(f'error: build the runtime first ({exe} is missing)')
    pkg = os.path.join(out, NAME)
    shutil.rmtree(pkg, ignore_errors=True)
    os.makedirs(pkg)

    def put(rel, src):
        d = os.path.join(pkg, rel)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(src, d)

    put(os.path.basename(exe), exe)
    if WIN:
        sdl = shutil.which('SDL2.dll') or os.path.join(os.path.dirname(shutil.which('g++') or ''), 'SDL2.dll')
        if not os.path.exists(sdl):
            sys.exit('error: SDL2.dll not found (put MSYS2 mingw64/bin on PATH)')
        put('SDL2.dll', sdl)
    for rel, src in FILES.items():
        put(rel, os.path.join(ROOT, src))
    put_zig(os.path.join(pkg, 'tools', 'zig'))
    if WIN:
        zipfile.ZipFile(fetch(*PYTHON)).extractall(os.path.join(pkg, 'tools', 'python'))
        put(os.path.join('tools', os.path.basename(PIP[0])), fetch(*PIP))
    # the build-inputs stamp, from the package's own files (make_module.py --stamp in its layout)
    stamp = subprocess.run([sys.executable, '-I', os.path.join(pkg, 'tools', 'make_module.py'), '--stamp'],
                           capture_output=True, text=True, check=True).stdout.strip()
    open(os.path.join(pkg, 'tools', 'build_inputs.stamp'), 'w').write(stamp + '\n')

    # Nothing translated from the game, nor game data, may be in the package.
    bad = []
    for d, _, names in os.walk(pkg):
        for n in names:
            low = n.lower()
            if (low.startswith('code_') and low.endswith('.cpp')) or low in ('main.dol', 'tmd.bin', 'ticket.bin') or \
               low.startswith('contrarebirth_game') or low.endswith(('.app', '.wad', '.dol')):
                bad.append(os.path.join(d, n))
    if bad:
        sys.exit('error: game-derived files in the package: ' + ', '.join(bad))
    size = sum(os.path.getsize(os.path.join(d, n)) for d, _, ns in os.walk(pkg) for n in ns)
    print(f'packaged {pkg} ({size // (1 << 20)} MB), stamp {stamp[:12]}')

    if '--zip' in args:
        if WIN:
            arc = os.path.join(out, f'{NAME}-windows-x64.zip')
            with zipfile.ZipFile(arc, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
                for d, _, ns in os.walk(pkg):
                    for n in ns:
                        p = os.path.join(d, n)
                        z.write(p, os.path.relpath(p, out))
        else:
            arc = os.path.join(out, f'{NAME}-linux-x64.tar.xz')
            with tarfile.open(arc, 'w:xz') as t:
                t.add(pkg, NAME)
        print(f'archive {arc} ({os.path.getsize(arc) // (1 << 20)} MB)')


if __name__ == '__main__':
    main()
