"""Write LTBL torch animations to HKX (SSE) and lay out the OAR folder."""
import json
import os
import subprocess
import sys
import numpy as np
sys.path.insert(0, "/home/claude/anim/tools")
from hkxwrite import write_anim_xml

HKXC = "/home/claude/anim/serde-hkx/target/release/hkxc"
ROOT = sys.argv[1] if len(sys.argv) > 1 else "/home/claude/anim/pkg"
OAR = os.path.join(ROOT, "meshes/actors/character/animations/OpenAnimationReplacer/LTBL_TorchBearers")
HOST = "IdlePickup_Ground.hkx"  # vanilla single-play idle that ends in IdleStop

SUBMODS = [
    # folder, display name, marker value, npz, description
    ("LTBL_TorchLight", "Light torch", 7101, "light", "Reach over the left shoulder, draw the torch, strike flint at the head until it catches, raise to carry."),
    ("LTBL_TorchSnuffGround", "Snuff torch on ground", 7102, "ground", "Squat, grind the head into the ground until it dies, stow over the shoulder."),
    ("LTBL_TorchDouseShore", "Douse torch at shore or puddle", 7103, "water", "Kneel at the water's edge, plunge and swish the head, flick it dry, stow."),
    ("LTBL_TorchDouseBasin", "Douse torch in trough, barrel or well", 7104, "basin", "Lean over the basin, dip and stir the head, flick it dry, stow."),
]


def main():
    os.makedirs(OAR, exist_ok=True)
    json.dump({
        "name": "LTBL Torch Bearers",
        "author": "Let There Be Light",
        "description": "Torch lighting and putting-out sequences for NPCs. Played only when Let There Be Light marks the actor (Variable10). Built on CMU motion capture data, retargeted and keyed for Skyrim."
    }, open(os.path.join(OAR, "config.json"), "w"), indent=4)
    for folder, name, marker, npz, desc in SUBMODS:
        d = os.path.join(OAR, folder)
        os.makedirs(d, exist_ok=True)
        data = np.load(f"/home/claude/anim/out/{npz}.npz")
        xml = f"/home/claude/anim/out/{npz}.xml"
        write_anim_xml(xml, data["T"], data["Q"])
        out = os.path.join(d, HOST)
        subprocess.run([HKXC, "convert", "-i", xml, "-o", out, "-v", "amd64"], check=True)
        json.dump({
            "name": name,
            "description": desc,
            "priority": 1350000000 + marker,
            "conditions": [{
                "condition": "CompareValues",
                "requiredVersion": "1.0.0.0",
                "Value A": {"actorValue": 77, "actorValueType": "Value"},
                "Comparison": "==",
                "Value B": {"value": float(marker)},
            }],
        }, open(os.path.join(d, "config.json"), "w"), indent=4)
        print(folder, os.path.getsize(out), "bytes", flush=True)


if __name__ == "__main__":
    main()
