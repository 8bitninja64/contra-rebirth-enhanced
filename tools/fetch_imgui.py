"""Download the pinned Dear ImGui release (F1 menu) into src/build-deps/imgui."""
from pathlib import Path
import hashlib
import io
import shutil
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.91.9b"
URL = f"https://github.com/ocornut/imgui/archive/refs/tags/v{VERSION}.zip"
SHA256 = "fd37507c8476a6d14cc7c4b352401f31bcbd0f0d995d35390811e968c466f46e"
# Library sources plus the SDL2 and OpenGL 3 backends; nothing else is used.
KEEP = ("imgui.cpp", "imgui.h", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp", "imgui_internal.h",
        "imconfig.h", "imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h", "LICENSE.txt",
        "backends/imgui_impl_sdl2.cpp", "backends/imgui_impl_sdl2.h", "backends/imgui_impl_opengl3.cpp",
        "backends/imgui_impl_opengl3.h", "backends/imgui_impl_opengl3_loader.h")


def main() -> int:
    target = ROOT / "src" / "build-deps" / "imgui"
    if all((target / name).exists() for name in KEEP):
        print("Dear ImGui already present in", target)
        return 0
    data = urllib.request.urlopen(URL).read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError("Dear ImGui archive hash mismatch")
    prefix = f"imgui-{VERSION}/"
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for name in KEEP:
            destination = target / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(prefix + name) as source, destination.open("wb") as out:
                shutil.copyfileobj(source, out)
    print("Dear ImGui", VERSION, "installed in", target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
