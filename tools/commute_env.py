# PlatformIO pre-script: bakes the commute defaults from commute.env into the
# firmware, so personal stops stay out of git (commute.env is gitignored).
#
# commute.env holds KEY=VALUE lines (see commute.env.example). Every key is
# optional; without the file the defaults are blank and the stops are set in
# the web UI (Commute tab). The Västtrafik key is never baked in.
Import("env")
import os

KEYS = {   # file key -> C macro
    "COMMUTE_FROM_ID": "COMMUTE_DEF_FROM_ID",
    "COMMUTE_FROM_NAME": "COMMUTE_DEF_FROM_NAME",
    "COMMUTE_TO_ID": "COMMUTE_DEF_TO_ID",
    "COMMUTE_TO_NAME": "COMMUTE_DEF_TO_NAME",
    "COMMUTE_WALK_MIN": "COMMUTE_DEF_WALK_MIN",
}

path = os.path.join(env.subst("$PROJECT_DIR"), "commute.env")
found = []
if os.path.isfile(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = (s.strip() for s in line.split("=", 1))
            v = v.strip('"').strip("'")
            if k not in KEYS or not v:
                continue
            if k == "COMMUTE_WALK_MIN":
                env.Append(CPPDEFINES=[(KEYS[k], int(v))])
            else:
                env.Append(CPPDEFINES=[(KEYS[k], env.StringifyMacro(v))])
            found.append(k)
print("commute.env: " + (", ".join(found) if found else "not used (blank defaults)"))
