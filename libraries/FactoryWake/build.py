Import("env")
from pathlib import Path
base = Path(env.subst("$PROJECT_DIR")) / "libraries/FactoryWake"
env.Append(LIBS=[env.File(str(base / "lib/esp32s3" / ("lib" + name + ".a")))
                 for name in ("wakenet", "esp-sr", "dl_lib", "c_speech_features", "hufzip")])
