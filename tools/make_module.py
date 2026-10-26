#!/usr/bin/env python3
"""Builds the game module (contrarebirth_game.dll / .so) from the player's WAD: the step the
first start performs on the player's machine (runtime/first_start.cpp runs it), so a release
ships no game code.

usage: make_module.py <wad> <user dir>
  1. libWiiPy (WAD decryption) from PyPI into <user dir>/pylib, unless Python already has it
  2. the WAD -> <user dir>/game (tmd, ticket, decrypted contents, main.dol)   [prepare_wad.py]
  3. main.dol -> C++ in <user dir>/module_build/gen                           [recomp.py]
  4. C++ -> <user dir>/contrarebirth_game.dll|.so with Zig (bundled in tools/zig, or ZIG=...)
  5. <user dir>/contrarebirth_game.stamp = this release's build-inputs stamp

Works from a release (tools/ next to the exe) and from the repository (tools/ and src/).
Progress for the first-start screen: lines "# step <n>/5 <text>" and "compiled <i>/<n>".
"""
import concurrent.futures, glob, hashlib, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEPS = 5


def layout():
    """Where the recompiler, its tables and the module-side runtime sources are."""
    if os.path.exists(os.path.join(HERE, 'recomp', 'recomp.py')):   # release: everything under tools/
        return {'recomp': os.path.join(HERE, 'recomp'), 'symbols': os.path.join(HERE, 'config', 'symbols.txt'),
                'runtime': os.path.join(HERE, 'runtime'), 'zig': os.path.join(HERE, 'zig')}
    root = os.path.dirname(HERE)                                         # repository
    return {'recomp': os.path.join(root, 'src'), 'symbols': os.path.join(root, 'config', 'symbols.txt'),
            'runtime': os.path.join(root, 'src', 'runtime'), 'zig': None}


def step(n, text):
    print(f'# step {n}/{STEPS} {text}', flush=True)


def build_inputs_stamp(L):
    """Hash of everything that decides the module's contents: when an update changes any of it,
    the runtime rebuilds the module (it compares this with tools/build_inputs.stamp)."""
    h = hashlib.sha256()
    files = sorted(glob.glob(os.path.join(glob.escape(L['recomp']), '*.py')) + [os.path.join(L['recomp'], 'hle.txt'),
                   os.path.join(L['recomp'], 'hooks.txt'), L['symbols'], os.path.abspath(__file__)] +
                   [os.path.join(L['runtime'], n) for n in ('ppc.h', 'module_abi.h', 'module_rt.cpp')])
    for f in files:
        h.update(os.path.basename(f).encode() + b'\0')
        h.update(open(f, 'rb').read().replace(b'\r\n', b'\n'))
    return h.hexdigest()


def find_zig(L):
    exe = 'zig.exe' if os.name == 'nt' else 'zig'
    for c in [os.environ.get('ZIG'), L['zig'] and os.path.join(L['zig'], exe), shutil.which('zig')]:
        if c and os.path.exists(c):
            return c
    sys.exit('error: no Zig compiler (the release bundles one in tools/zig; set ZIG=... otherwise)')


def ensure_libwiipy(user):
    pylib = os.path.join(user, 'pylib')
    sys.path.insert(0, pylib)
    try:
        import libWiiPy  # noqa: F401
        return
    except ImportError:
        pass
    print('installing libWiiPy from PyPI (WAD decryption)', flush=True)
    pip = [sys.executable, '-m', 'pip']
    wheel = glob.glob(os.path.join(glob.escape(HERE), 'pip-*.whl'))
    if wheel:   # the release bundles pip (embeddable Python has none)
        pip = [sys.executable, os.path.join(wheel[0], 'pip')]
    r = subprocess.run(pip + ['install', '--disable-pip-version-check', '--no-warn-script-location',
                              '--target', pylib, 'libWiiPy'])
    if r.returncode:
        sys.exit('error: could not install libWiiPy (is the computer online?)')
    import importlib
    importlib.invalidate_caches()
    import libWiiPy  # noqa: F401


def compile_module(L, gen, out):
    zig = find_zig(L)
    target = 'x86_64-windows-gnu' if os.name == 'nt' else 'x86_64-linux-gnu.2.31'
    objdir = os.path.join(os.path.dirname(gen), 'obj')
    shutil.rmtree(objdir, ignore_errors=True)
    os.makedirs(objdir)
    srcs = sorted(glob.glob(os.path.join(glob.escape(gen), '*.cpp'))) + [os.path.join(L['runtime'], 'module_rt.cpp')]
    flags = ['-target', target, '-O2', '-fno-strict-aliasing', '-w', '-std=c++17', '-DCONTRA_MODULE=1',
             '-fvisibility=hidden', '-I', L['runtime'], '-I', gen]
    if os.name != 'nt':
        flags.append('-fPIC')

    def cc(src):
        obj = os.path.join(objdir, os.path.basename(src)[:-4] + '.o')
        r = subprocess.run([zig, 'c++', '-c'] + flags + [src, '-o', obj], capture_output=True, text=True)
        return src, obj, r

    objs = []
    jobs = max(1, min(8, (os.cpu_count() or 2) - 1))
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for i, (src, obj, r) in enumerate(ex.map(cc, srcs), 1):
            if r.returncode:
                sys.stdout.write(r.stdout + r.stderr)
                sys.exit(f'error: compiling {os.path.basename(src)} failed')
            objs.append(obj)
            print(f'compiled {i}/{len(srcs)} {os.path.basename(src)}', flush=True)
    tmp = out + '.tmp'
    r = subprocess.run([zig, 'c++', '-target', target, '-shared', '-s', '-o', tmp] + objs, cwd=objdir)
    if r.returncode:
        sys.exit('error: linking the game module failed')
    os.replace(tmp, out)
    # link leftovers (folder names are not patterns: every glob here escapes its directory part)
    for junk in glob.glob(glob.escape(tmp) + '.pdb') + glob.glob(glob.escape(out) + '.pdb') + \
            glob.glob(os.path.join(glob.escape(os.path.dirname(out)), 'code_*.lib')):
        os.remove(junk)
    shutil.rmtree(objdir, ignore_errors=True)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    wad, user = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    L = layout()
    os.makedirs(user, exist_ok=True)
    sys.path.insert(0, HERE)
    sys.path.insert(0, L['recomp'])

    game = os.path.join(user, 'game')
    dol = os.path.join(game, 'main.dol')
    step(1, 'Preparing WAD decryption')
    ensure_libwiipy(user)
    import prepare_wad   # needs libWiiPy
    if sys.argv[1] == '-':   # rebuild after an update: the game is already unpacked
        if not os.path.exists(dol):
            sys.exit('error: no unpacked game to rebuild from; choose the WAD again')
        step(2, 'Using the unpacked game')
    else:
        step(2, 'Unpacking your WAD')
        prepare_wad.main(wad, game)
    if hashlib.sha1(open(dol, 'rb').read()).hexdigest() != prepare_wad.EXPECT_DOL_SHA1:
        sys.exit('error: this WAD is not Contra ReBirth (USA, WKTE) as this release supports')

    step(3, 'Translating the game code')
    gen = os.path.join(user, 'module_build', 'gen')
    shutil.rmtree(gen, ignore_errors=True)   # chunks from another version must not be compiled in
    # in this process: the embeddable Python's ._pth file keeps a script's own folder off sys.path,
    # so recomp.py run as a child would not find dol.py / ppcdec.py
    import recomp
    sys.argv = ['recomp.py', dol, L['symbols'], gen, '300']
    try:
        recomp.main()
    except SystemExit as e:
        if e.code:
            sys.exit(f'error: the recompiler failed ({e.code})')

    step(4, 'Compiling the game code (a few minutes)')
    module = os.path.join(user, 'contrarebirth_game.dll' if os.name == 'nt' else 'contrarebirth_game.so')
    compile_module(L, gen, module)

    step(5, 'Finishing')
    open(module[:module.rfind('.')] + '.stamp', 'w').write(build_inputs_stamp(L))
    shutil.rmtree(os.path.join(user, 'module_build'), ignore_errors=True)
    print('done', flush=True)


if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] == '--stamp':   # package.ps1: the release's stamp
        print(build_inputs_stamp(layout()))
    else:
        main()
