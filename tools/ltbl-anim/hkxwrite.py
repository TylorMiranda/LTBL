"""Write a Skyrim SE animation (hkaInterleavedUncompressedAnimation) as Havok XML packfile."""
import numpy as np

NUM_TRACKS = 99


def write_anim_xml(path, local_t, local_q, fps=30.0, annotations=None):
    """local_t: (F, N, 3), local_q: (F, N, 4) xyzw. Only the first 99 bones become tracks.
    annotations: list of (time, text) placed on track 0."""
    F = local_t.shape[0]
    duration = (F - 1) / fps
    annotations = annotations or []
    out = []
    w = out.append
    w('<?xml version="1.0" encoding="ascii"?>')
    w('<hkpackfile classversion="8" contentsversion="hk_2010.2.0-r1" toplevelobject="#0001">')
    w('')
    w('\t<hksection name="__data__">')
    w('')
    w('\t\t<hkobject name="#0003" class="hkaInterleavedUncompressedAnimation" signature="0x930af031">')
    w('\t\t\t<hkparam name="type">HK_INTERLEAVED_ANIMATION</hkparam>')
    w(f'\t\t\t<hkparam name="duration">{duration:.6f}</hkparam>')
    w(f'\t\t\t<hkparam name="numberOfTransformTracks">{NUM_TRACKS}</hkparam>')
    w('\t\t\t<hkparam name="numberOfFloatTracks">0</hkparam>')
    w('\t\t\t<hkparam name="extractedMotion">null</hkparam>')
    w(f'\t\t\t<hkparam name="annotationTracks" numelements="{NUM_TRACKS}">')
    for i in range(NUM_TRACKS):
        w('\t\t\t\t<hkobject>')
        w('\t\t\t\t\t<hkparam name="trackName">&#9216;</hkparam>')
        ann = annotations if i == 0 else []
        if ann:
            w(f'\t\t\t\t\t<hkparam name="annotations" numelements="{len(ann)}">')
            for t, text in ann:
                w('\t\t\t\t\t\t<hkobject>')
                w(f'\t\t\t\t\t\t\t<hkparam name="time">{t:.6f}</hkparam>')
                w(f'\t\t\t\t\t\t\t<hkparam name="text">{text}</hkparam>')
                w('\t\t\t\t\t\t</hkobject>')
            w('\t\t\t\t\t</hkparam>')
        else:
            w('\t\t\t\t\t<hkparam name="annotations" numelements="0"></hkparam>')
        w('\t\t\t\t</hkobject>')
    w('\t\t\t</hkparam>')
    w(f'\t\t\t<hkparam name="transforms" numelements="{F * NUM_TRACKS}">')
    for f in range(F):
        for b in range(NUM_TRACKS):
            t = local_t[f, b]
            q = local_q[f, b]
            w(f'\t\t\t\t({t[0]:.6f} {t[1]:.6f} {t[2]:.6f})({q[0]:.6f} {q[1]:.6f} {q[2]:.6f} {q[3]:.6f})(1.000000 1.000000 1.000000)')
    w('\t\t\t</hkparam>')
    w('\t\t\t<hkparam name="floats" numelements="0"></hkparam>')
    w('\t\t</hkobject>')
    w('')
    w('\t\t<hkobject name="#0004" class="hkaAnimationBinding" signature="0x66eac971">')
    w('\t\t\t<hkparam name="originalSkeletonName">NPC Root [Root]</hkparam>')
    w('\t\t\t<hkparam name="animation">#0003</hkparam>')
    w('\t\t\t<hkparam name="transformTrackToBoneIndices" numelements="0"></hkparam>')
    w('\t\t\t<hkparam name="floatTrackToFloatSlotIndices" numelements="0"></hkparam>')
    w('\t\t\t<hkparam name="blendHint">NORMAL</hkparam>')
    w('\t\t</hkobject>')
    w('')
    w('\t\t<hkobject name="#0002" class="hkaAnimationContainer" signature="0x8dc20333">')
    w('\t\t\t<hkparam name="skeletons" numelements="0"></hkparam>')
    w('\t\t\t<hkparam name="animations" numelements="1">')
    w('\t\t\t\t#0003')
    w('\t\t\t</hkparam>')
    w('\t\t\t<hkparam name="bindings" numelements="1">')
    w('\t\t\t\t#0004')
    w('\t\t\t</hkparam>')
    w('\t\t\t<hkparam name="attachments" numelements="0"></hkparam>')
    w('\t\t\t<hkparam name="skins" numelements="0"></hkparam>')
    w('\t\t</hkobject>')
    w('')
    w('\t\t<hkobject name="#0001" class="hkRootLevelContainer" signature="0x2772c11e">')
    w('\t\t\t<hkparam name="namedVariants" numelements="1">')
    w('\t\t\t\t<hkobject>')
    w('\t\t\t\t\t<hkparam name="name">Merged Animation Container</hkparam>')
    w('\t\t\t\t\t<hkparam name="className">hkaAnimationContainer</hkparam>')
    w('\t\t\t\t\t<hkparam name="variant">#0002</hkparam>')
    w('\t\t\t\t</hkobject>')
    w('\t\t\t</hkparam>')
    w('\t\t</hkobject>')
    w('')
    w('\t</hksection>')
    w('')
    w('</hkpackfile>')
    open(path, "w").write("\n".join(out) + "\n")
