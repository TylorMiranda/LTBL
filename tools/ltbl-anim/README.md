# LTBL torch animation tools

How the Torch Bearers clips are built. Python 3 + numpy + matplotlib, plus serde-hkx's `hkxc` for HKX conversion.

Pipeline:
1. `retarget.py` maps CMU motion capture (BVH) onto the XPMSSE skeleton.
2. `author.py` layers keyed two-bone IK, hand orientation, finger curl and look-at on top.
3. `seq_light.py` / `seq_snuff.py` define the four sequences.
4. `hkxwrite.py` writes `hkaInterleavedUncompressedAnimation` XML; `package.py` converts it to SSE HKX with `hkxc` and lays out the OAR pack.
5. `spline.py` decodes vanilla spline-compressed clips. It is used only to read the engine's neutral and torch-carry poses from your own game files, so each clip starts and ends exactly where the engine blends.
6. `skel.render_frames` draws stick-figure contact sheets for checking every sequence.

Sources (all free for any use):
- CMU Graphics Lab Motion Capture Database, mocap.cs.cmu.edu (created with funding from NSF EIA-0196217). Clips 79_86 (draw from a back quiver), 115_06 (squat), 23_03 (kneel), 26_09 (bend).

Each clip's first and last frame are set to the engine's own neutral and torch-carry poses (read from the game's archives at build time) so the hand-offs blend without a pop. No other vanilla or third-party animation data is used, and none is stored in this folder.
