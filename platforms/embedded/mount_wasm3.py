import json
import shutil
from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))

src = (project_dir / "../../../source").resolve()
dst = project_dir / "lib" / "wasm3"

if dst.exists():
    shutil.rmtree(dst)

shutil.copytree(src, dst / "src")

library_json = {
    "name": "Wasm3",
    "build": {
        "flags": "-Os -fomit-frame-pointer -fno-stack-check -fno-stack-protector -Wfatal-errors -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers",
        "srcFilter": ["+<*>", "-<extensions/*>"],
        "libArchive": False,
    },
}

(dst / "library.json").write_text(
    json.dumps(library_json, indent=2) + "\n",
    encoding="utf-8",
)

print(f"Prepared wasm3 library from: {src}")
