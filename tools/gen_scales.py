#!/usr/bin/env python3
"""Generate src/scales_gen.inc: the named scales added after the 17 built in to layouts.c.

Modes of the harmonic minor, melodic minor and harmonic major scales are made by rotating the parent scale, so
each mode's notes follow from its parent. The rest are written out from standard references. Each scale has a name
and a short description (kept to about 40 characters, so it fits the Info line on the MPC screen).
Scales already in the built-in table are skipped by name.

usage: gen_scales.py <out.inc>
"""
import itertools
import sys

BUILT_IN = {  # names already in layouts.c; a generated entry with one of these names is skipped
    "Major", "Natural minor", "Harmonic minor", "Melodic minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
    "Locrian", "Major pentatonic", "Minor pentatonic", "Blues", "Major blues (gospel)", "Lydian dominant",
    "Phrygian dominant", "Whole tone", "Diminished (half-whole)",
}


def rotate(parent, degree):
    """the mode that starts on the given degree (0 = the parent itself), as pitch classes from its own tonic"""
    root = parent[degree]
    return sorted((p - root) % 12 for p in parent)


HARMONIC_MINOR = [0, 2, 3, 5, 7, 8, 11]
MELODIC_MINOR = [0, 2, 3, 5, 7, 9, 11]
HARMONIC_MAJOR = [0, 2, 4, 5, 7, 8, 11]

MODES = [  # (parent, degree, name, description)
    (HARMONIC_MINOR, 1, "Locrian natural 6", "Dark and tense, with a bright 6th"),
    (HARMONIC_MINOR, 2, "Ionian sharp 5", "Dreamy, bright, floating"),
    (HARMONIC_MINOR, 3, "Ukrainian Dorian", "Minor with a sharp 4th, folk-like"),
    (HARMONIC_MINOR, 5, "Lydian sharp 2", "Bright and exotic, unresolved"),
    (HARMONIC_MINOR, 6, "Ultralocrian", "Very dark, tense, unstable"),
    (MELODIC_MINOR, 1, "Dorian flat 2", "Dark minor with a flat 2nd, moody"),
    (MELODIC_MINOR, 2, "Lydian augmented", "Dreamy and otherworldly"),
    (MELODIC_MINOR, 4, "Mixolydian flat 6", "Dominant with a flat 6th, exotic"),
    (MELODIC_MINOR, 5, "Locrian natural 2", "Half-diminished, soft, unstable"),
    (MELODIC_MINOR, 6, "Altered", "Dominant tension, jazz, unresolved"),
    (HARMONIC_MAJOR, 0, "Harmonic major", "Major with a flat 6th, lush"),
    (HARMONIC_MAJOR, 1, "Dorian flat 5", "Minor with a flat 5th, dark jazz"),
    (HARMONIC_MAJOR, 2, "Phrygian flat 4", "Dark and Spanish-leaning"),
    (HARMONIC_MAJOR, 3, "Lydian flat 3", "Bright, unusual, cinematic"),
    (HARMONIC_MAJOR, 4, "Mixolydian flat 2", "Dominant with a flat 2nd, tense"),
    (HARMONIC_MAJOR, 5, "Lydian augmented sharp 2", "Bright, dreamy, very tense"),
    (HARMONIC_MAJOR, 6, "Locrian flat 7", "Dark, unstable, Locrian with a flat 7"),
]

OTHER = [  # (name, pitch classes, description)
    ("Bebop dominant", [0, 2, 4, 5, 7, 9, 10, 11], "Dominant with a passing 7th, bebop"),
    ("Bebop major", [0, 2, 4, 5, 7, 8, 9, 11], "Major with a passing sharp 5th"),
    ("Hungarian major", [0, 3, 4, 6, 7, 9, 10], "Gypsy major, exotic and dramatic"),
    ("Hungarian minor", [0, 2, 3, 6, 7, 8, 11], "Minor with a raised 4th, dramatic"),
    ("Double harmonic", [0, 1, 4, 5, 7, 8, 11], "Byzantine and Bhairav, very exotic"),
    ("Neapolitan minor", [0, 1, 3, 5, 7, 8, 11], "Minor with a flat 2nd, dramatic"),
    ("Neapolitan major", [0, 1, 3, 5, 7, 9, 11], "Major with a flat 2nd, classical"),
    ("Persian", [0, 1, 4, 5, 6, 8, 11], "Persian, dark and exotic"),
    ("Enigmatic", [0, 1, 4, 6, 8, 10, 11], "Strange and shifting, chromatic"),
    ("Augmented hexatonic", [0, 3, 4, 7, 8, 11], "Symmetrical, dreamy, unresolved"),
    ("Prometheus", [0, 2, 4, 6, 9, 10], "Mystic and bright, Scriabin-style"),
    ("Diminished (whole-half)", [0, 2, 3, 5, 6, 8, 9, 11], "Dominant diminished, jazz tension"),
    ("Spanish octatonic", [0, 1, 3, 4, 5, 6, 8, 10], "Spanish and flamenco, very tense"),
    ("Lydian minor", [0, 2, 4, 6, 7, 8, 10], "Dreamy minor with a bright 4th"),
    ("Dominant pentatonic", [0, 2, 4, 7, 10], "Open dominant, rock and blues"),
    ("Malkauns", [0, 3, 5, 8, 10], "Raga Malkauns: deep and meditative"),
    ("Hirajoshi", [0, 2, 3, 7, 8], "Japanese, bright and sad"),
    ("In sen", [0, 1, 5, 7, 10], "Japanese, dark and mysterious"),
    ("Iwato", [0, 1, 5, 6, 10], "Japanese, dark and tense"),
    ("Kumoi", [0, 2, 3, 7, 9], "Japanese, soft and reflective"),
    ("Yo", [0, 2, 5, 7, 9], "Japanese, open and bright"),
    ("Ryukyu", [0, 4, 5, 7, 11], "Okinawan, bright folk"),
    ("Egyptian pentatonic", [0, 2, 5, 7, 10], "Suspended, open, no 3rd"),
    ("Todi", [0, 1, 3, 6, 7, 8, 11], "Raga Todi: deep and emotional"),
    ("Marwa", [0, 1, 4, 6, 7, 9, 11], "Raga Marwa: dusk, restless"),
    ("Purvi", [0, 1, 4, 6, 7, 8, 11], "Raga Purvi: dusk, reflective"),
]


def entries():
    out = []
    for parent, degree, name, desc in MODES:
        out.append((name, rotate(parent, degree), desc))
    out.extend(OTHER)
    return out


BUILT_IN_PCS = [  # the built-in 17, as pitch classes from the tonic (their set classes are not generated again)
    [0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 3, 5, 7, 8, 11], [0, 2, 3, 5, 7, 9, 11],
    [0, 2, 3, 5, 7, 9, 10], [0, 1, 3, 5, 7, 8, 10], [0, 2, 4, 6, 7, 9, 11], [0, 2, 4, 5, 7, 9, 10],
    [0, 1, 3, 5, 6, 8, 10], [0, 2, 4, 7, 9], [0, 3, 5, 7, 10], [0, 3, 5, 6, 7, 10], [0, 2, 3, 4, 7, 9],
    [0, 2, 4, 6, 7, 9, 10], [0, 1, 4, 5, 7, 8, 10], [0, 2, 4, 6, 8, 10], [0, 1, 3, 4, 6, 7, 9, 10],
]


def normal(pcs):
    """the rotation of a set that starts at 0 and is smallest when read as a list"""
    return min(tuple(sorted((p - r) % 12 for p in pcs)) for r in pcs)


def prime(pcs):
    """one name for a set under transposition and inversion"""
    return min(normal(pcs), normal([(-p) % 12 for p in pcs]))


INTERVAL = ["0", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7"]
MAJOR = {0, 2, 4, 5, 7, 9, 11}


def auto_description(pcs):
    """a description from the notes alone: a symmetric set says what it repeats; otherwise the mood the 3rd and 6th
    give, then the notes that are not in the major scale"""
    st = set(pcs)
    for k in range(1, 12):
        if {(p + k) % 12 for p in st} == st and 12 % k == 0:
            return ("symmetric: repeats every %d, %d notes" % (k, len(st)))[:40]
    if 3 in st and 8 in st: mood = "dark"
    elif 4 in st and 9 in st: mood = "bright"
    else: mood = "mixed"
    extra = [INTERVAL[p] for p in sorted(st - MAJOR)]
    text = "%s: %s" % (mood, ", ".join(extra)) if extra else "%s, major notes" % mood
    return text[:40]


def set_classes(covered):
    """every 4 to 8 note set class not already covered, as a prime form (one entry per class)"""
    out = []
    for n in range(4, 9):
        seen = set()
        for c in itertools.combinations(range(12), n):
            if 0 not in c:
                continue
            p = prime(c)
            if p in seen or p in covered:
                continue
            seen.add(p)
            out.append(list(p))
    return out


def main():
    rows = []
    seen = set(BUILT_IN)
    named_primes = set()
    for name, pcs, desc in entries():
        if name in seen:
            continue
        seen.add(name)
        assert len(desc) <= 40, (name, len(desc))
        assert 4 <= len(pcs) <= 8 and pcs[0] == 0, name
        rows.append((name, pcs, desc))
        named_primes.add(prime(pcs))
    built_primes = {prime(b) for b in BUILT_IN_PCS}
    count = {}
    for pcs in set_classes(named_primes | built_primes):
        n = len(pcs)
        count[n] = count.get(n, 0) + 1
        rows.append(("%d-note set %d" % (n, count[n]), pcs, auto_description(pcs)))
    lines = ["/* generated by tools/gen_scales.py: scales 17 and up of VC_SCALES. Do not edit by hand. */"]
    for name, pcs, desc in rows:
        iv = ", ".join(str(x) for x in pcs)
        lines.append('    {"%s", %d, {%s}, "%s"},' % (name, len(pcs), iv, desc))
    with open(sys.argv[1], "w") as f:
        f.write("\n".join(lines) + "\n")
    print("%d scales generated, %d built in, %d in total" % (len(rows), len(BUILT_IN), len(rows) + len(BUILT_IN)))


if __name__ == "__main__":
    main()
