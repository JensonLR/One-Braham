# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_audio_ids
=========================

Checks that every cue the game can ask for exists, is documented, and is audible.

WHY THIS EXISTS
    Four files describe the audio of this game and, until this checker existed,
    they disagreed:

      * `OneBrahamAudioBankAsset.cpp` - the core table: 126 rows, the shipping
        cue list.
      * `OneBrahamProceduralAudio.cpp` - what can be synthesised on a machine with
        no recorded content. Its whitelist named `SFX_HitMarker`,
        `SFX_KillConfirm` and `SFX_LowAmmo_Click`, which nothing plays, while the
        cues the game does play (`UI_HitMarker`, `UI_KillConfirm`,
        `UI_LowAmmo_Click`) were reported as silent cues.
      * `build_audio.py` - the beds and the mix, which the bank is meant to
        mirror: `AMBIENCE` and `MUSIC` are the briefs the bank's rows must match.
      * `Docs/12_Audio.md` - the shopping list. It named ten cues the bank has
        never contained (`AMB_Plaza`, `AMB_OpenPlan`, `AMB_Mezzanine`,
        `AMB_ServerRoom`, `AMB_Skydeck`, `AMB_Stairwell`, `AMB_LiftShaft`,
        `AMB_AlarmBed`, `SFX_Glass_Shatter`, `SFX_Meal_Deal`) - the same class of
        defect as three weapon naming schemes, in the audio system.

WHAT IT CHECKS
    1. Every bank row is either synthesised (a recipe, a branch, a bed, or a UI
       substring the synthesiser answers) or named in `Docs/12_Audio.md`. A cue
       that is neither is a cue that ships silent and unexplained.
    2. Every audio id literal in `Source/` is a bank row. Prefixes used to build
       surface-suffixed ids are listed explicitly, because they are stems, not
       cues.
    3. `AMBIENCE` and `MUSIC` in the builder are exactly the bank's `AMB_*` and
       `MUS_*` rows.
    4. The procedural bed recipe table covers every `AMB_*` row.
    5. The zone table's keys are the building layout's area ids plus the fallback
       zone - no orphan zone, no area without a bed.
    6. The music cue table covers every `EOneBrahamMusicState`.
    7. No duplicate rows.
    8. The document's ambience table lists the same ids as the bank.

WHAT IT IGNORES
    Ids inside comments. Prefix strings that build an id (rule 2's allow-list).

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_audio_ids.py
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

python_root = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

SOURCE_ROOT = os.path.join(python_root, "Source")
BANK = os.path.join(python_root, "Source", "OneBraham", "Audio", "OneBrahamAudioBankAsset.cpp")
PROCEDURAL = os.path.join(python_root, "Source", "OneBraham", "Audio", "OneBrahamProceduralAudio.cpp")
MANAGER = os.path.join(python_root, "Source", "OneBraham", "Audio", "OneBrahamAudioManagerComponent.cpp")
LAYOUT = os.path.join(python_root, "Source", "OneBraham", "Data", "OneBrahamBuildingLayoutAsset.cpp")
TYPES = os.path.join(python_root, "Source", "OneBraham", "Audio", "OneBrahamAudioTypes.h")
BUILDER = os.path.join(python_root, "Tools", "Python", "onebraham", "build_audio.py")
DOCS_12 = os.path.join(python_root, "Docs", "12_Audio.md")

#: A bank row: Row(TEXT("SFX_Foo"), Bus, Category, ...)
ROW = re.compile(r'Row\(\s*TEXT\("([A-Za-z0-9_]+)"\)')

#: An explicit synthesis recipe: Id == TEXT("SFX_Foo")
BRANCH = re.compile(r'Id == TEXT\("([A-Za-z0-9_]+)"\)')

#: An ambience bed recipe: { TEXT("AMB_Foo"), { ... } }
BED = re.compile(r'\{\s*TEXT\("(AMB_[A-Za-z0-9_]+)"\)\s*,')

#: A literal whitelist entry inside the Known set.
KNOWN_ENTRY = re.compile(r'TEXT\("([A-Za-z0-9_]+)"\)')

#: Any audio-family id literal, used to read the source.
AUDIO_ID = re.compile(r'\b((?:SFX|UI|AMB|MUS)_[A-Za-z0-9_]+)\b')

#: UI ids the substring synthesiser answers, kept in step with MakeUISound.
UI_SUBSTRINGS = ["Hover", "Accept", "Confirm", "Deny", "Error",
                 "ScoreTick", "LowAmmo", "HitMarker", "Kill", "Click"]

#: Id stems that build a surface-suffixed id. Not cues; the built ids are.
STEMS = {
    "SFX_Footstep": "SFX_Footstep_<Surface>",
    "SFX_Bullet_Impact": "SFX_Bullet_Impact_<Surface>",
}

#: One row of the builder's AMBIENCE / MUSIC lists. The bed id is the first
#: element of an AMBIENCE row and the second of a MUSIC row, so this matches any
#: quoted bed id inside a tuple rather than a fixed position.
PY_ROW = re.compile(r'\(\s*(?:"[^"]*",\s*)?"((?:AMB|MUS)_[A-Za-z0-9_]+)"')

#: A zone table entry: { TEXT("Lobby"), TEXT("AMB_Lobby") }
ZONE_ENTRY = re.compile(r'\{\s*TEXT\("([A-Za-z0-9_]+)"\),\s*TEXT\("(AMB_[A-Za-z0-9_]+)"\)\s*\}')

#: An area id the layout declares: AddFloor(0, "Ground_Plaza", ...)
AREA_ID = re.compile(r'AddFloor\(\s*\d+\s*,\s*(?:TEXT\(\s*)?"([A-Za-z0-9_]+)"')

#: The music cue table.
MUSIC_ENTRY = re.compile(r'MusicCueByState\.Add\(EOneBrahamMusicState::(\w+),\s*FName\(TEXT\("(MUS_[A-Za-z0-9_]+)"\)\)\)')

#: A Docs/12 ambience row: | `AMB_Foo` | zone | what |
DOC_AMB_ROW = re.compile(r'^\|\s*`(AMB_[A-Za-z0-9_]+)`', re.MULTILINE)


def read(path):
    if not os.path.isfile(path):
        return ""
    return io.open(path, encoding="utf-8", errors="ignore").read()


def bank_rows():
    return ROW.findall(read(BANK))


def synthesised_ids():
    """Ids the procedural module can produce."""
    text = read(PROCEDURAL)

    known = set()

    if "static const TSet<FName> Known" in text:
        block = text[text.index("static const TSet<FName> Known"):]
        block = block[:block.index("};")]
        known = set(KNOWN_ENTRY.findall(block))

    branches = set(BRANCH.findall(text))
    beds = set(BED.findall(text))

    return known | branches | beds


def is_synthesised(sound_id, known):
    """True when the synthesiser answers for this id.

    The UI set is answered by substring - MakeUISound branches on what the cue
    does (UI_HitMarker_Headshot, UI_KillConfirm, UI_Voice_Connected) - rather than
    by name, so a new UI cue is covered without touching the whitelist. This
    mirrors HasSynthesisedSound, which is the function that decides.
    """
    if sound_id in known:
        return True

    if sound_id.startswith("UI_") or sound_id.startswith("SFX_"):
        for substring in UI_SUBSTRINGS:
            if substring in sound_id:
                return True

    return False


def documented_ids():
    """Every audio id named in Docs/12."""
    return set(AUDIO_ID.findall(read(DOCS_12)))


def source_audio_ids(source_root=None):
    """{id: [(rel_path, line)]} for every audio id literal in Source/."""
    root = source_root or SOURCE_ROOT
    out = {}

    if not os.path.isdir(root):
        return out

    for dirpath, _dirs, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith((".h", ".cpp")):
                continue

            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, python_root)

            # strip_comments, not strip_comments_and_strings: the ids are
            # literals, and a checker that blanks literals finds nothing at all
            # and passes.
            clean = C.strip_comments(read(path))

            for number, line in enumerate(clean.splitlines(), 1):
                for match in AUDIO_ID.finditer(line):
                    sound_id = match.group(1)

                    # The stems that build a surface-suffixed id are not cues.
                    if sound_id in STEMS:
                        continue

                    # A format string (SFX_Bullet_Impact_%s) reads as an id with a
                    # trailing underscore; it is the stem, and the next character
                    # says so.
                    if sound_id.endswith("_"):
                        continue

                    if line[match.end():match.end() + 1] == "%":
                        continue

                    out.setdefault(sound_id, []).append((rel, number))

    return out


def run(source_root=None):
    """Returns (checks, problems)."""
    problems = []
    checks = 0

    rows = bank_rows()
    known = synthesised_ids()
    documented = documented_ids()

    checks += 1

    if not rows:
        return checks, [(u"the bank's core table was not found or has moved", u"bank")]

    row_set = set(rows)

    # 1. every row is audible or documented
    for sound_id in rows:
        checks += 1

        if not is_synthesised(sound_id, known) and sound_id not in documented:
            problems.append((
                u"the bank has '{0}' and it is neither synthesised nor named in "
                u"Docs/12 - a cue that ships silent and unexplained".format(sound_id),
                u"silent cue"))

    # 7. duplicates (before the set-based rules, so a duplicate is reported once)
    seen = set()
    duplicates = []

    for sound_id in rows:
        if sound_id in seen:
            duplicates.append(sound_id)
        seen.add(sound_id)

    checks += 1

    if duplicates:
        problems.append((
            u"the bank lists {0} more than once".format(
                u", ".join(sorted(set(duplicates)))), u"duplicate row"))

    # 2. every id literal in the source is a row
    for sound_id, where in sorted(source_audio_ids(source_root).items()):
        checks += 1

        if sound_id not in row_set:
            problems.append((
                u"{0}:{1} asks for '{2}' and the bank has no such row".format(
                    where[0][0], where[0][1], sound_id), u"unknown cue"))

    # 3. the builder's beds are the bank's beds
    builder_text = read(BUILDER)
    builder_beds = set(PY_ROW.findall(builder_text))
    bank_beds = set(sound_id for sound_id in rows if sound_id.startswith("AMB_"))
    bank_music = set(sound_id for sound_id in rows if sound_id.startswith("MUS_"))

    checks += 1

    if not builder_beds:
        problems.append((u"build_audio.py's AMBIENCE / MUSIC lists were not found",
                         u"builder"))
    else:
        builder_amb = set(i for i in builder_beds if i.startswith("AMB_"))
        builder_mus = set(i for i in builder_beds if i.startswith("MUS_"))

        checks += 1

        if builder_amb != bank_beds:
            problems.append((
                u"the builder's ambience beds and the bank's AMB_ rows differ: "
                u"bank-only {0}, builder-only {1}".format(
                    sorted(bank_beds - builder_amb) or u"none",
                    sorted(builder_amb - bank_beds) or u"none"), u"builder drift"))

        checks += 1

        if builder_mus != bank_music:
            problems.append((
                u"the builder's music beds and the bank's MUS_ rows differ: "
                u"bank-only {0}, builder-only {1}".format(
                    sorted(bank_music - builder_mus) or u"none",
                    sorted(builder_mus - bank_music) or u"none"), u"builder drift"))

    # 4. every AMB row has a recipe
    beds = set(BED.findall(read(PROCEDURAL)))

    for sound_id in sorted(bank_beds):
        checks += 1

        if sound_id not in beds:
            problems.append((
                u"'{0}' is a bank row with no bed recipe, so the room it names is "
                u"silent until someone records a wave".format(sound_id),
                u"unsynthesised bed"))

    # 5. the zone table is the layout's areas
    bank_text = read(BANK)
    zone_entries = ZONE_ENTRY.findall(bank_text)
    zones = dict(zone_entries)
    area_ids = set(AREA_ID.findall(read(LAYOUT)))

    layout_text = io.open(LAYOUT, encoding="utf-8").read() if os.path.isfile(LAYOUT) else ""

    checks += 1

    if not area_ids:
        problems.append((u"the building layout's area ids were not found", u"layout"))
    elif not zones:
        problems.append((u"the bank has no ambience zone table", u"zone table"))
    else:
        for area_id in sorted(area_ids):
            checks += 1

            if area_id not in zones:
                problems.append((
                    u"the layout has area '{0}' and the bank's zone table has no "
                    u"bed for it, so that floor plays the fallback".format(area_id),
                    u"zone table"))

        fallback_match = re.search(r'AmbienceFallbackZone\s*=\s*FName\(TEXT\("(\w+)"\)\)', read(TYPES))
        fallback = fallback_match.group(1) if fallback_match else u""

        for zone in sorted(zones):
            checks += 1

            if zone not in area_ids and zone != fallback:
                problems.append((
                    u"the bank's zone table has '{0}', which is neither an area id "
                    u"nor the fallback zone '{1}'".format(zone, fallback),
                    u"zone table"))

        checks += 1

        if fallback and fallback not in zones:
            problems.append((
                u"the fallback zone '{0}' is not in the zone table, so a floor with "
                u"no area resolves to the generic bed".format(fallback), u"zone table"))

    # 6. the music cue table covers every state
    # The enumerators carry no explicit values and each sits on its own line with
    # a UMETA tag, so they are read line by line rather than by "= n".
    states = re.findall(r'enum class EOneBrahamMusicState[^{]*\{(.*?)\};',
                        read(TYPES), re.S)
    state_names = []

    if states:
        body = C.strip_comments(states[0])

        for line in body.splitlines():
            match = re.match(r'\s*(\w+)\s*(?:UMETA|,|$)', line.strip())

            if match and match.group(1) not in ("UMETA",):
                state_names.append(match.group(1))
    music_entries = dict(MUSIC_ENTRY.findall(bank_text))

    checks += 1

    if not state_names:
        problems.append((u"EOneBrahamMusicState's enumerators were not found", u"music"))
    else:
        for state in state_names:
            checks += 1

            if state not in music_entries:
                problems.append((
                    u"music state '{0}' has no cue in the bank's table, so the "
                    u"director would keep playing whatever it had".format(state),
                    u"music table"))
                continue

            checks += 1

            if music_entries[state] not in row_set:
                problems.append((
                    u"music state '{0}' plays '{1}', which is not a bank row".format(
                        state, music_entries[state]), u"music table"))

    # 8. the document's ambience table is the bank's
    doc_beds = set(DOC_AMB_ROW.findall(read(DOCS_12)))

    checks += 1

    if not doc_beds:
        problems.append((u"Docs/12's ambience table was not found", u"documentation"))
    elif doc_beds != bank_beds:
        problems.append((
            u"Docs/12 lists {0} ambience beds and the bank has {1}: doc-only {2}, "
            u"bank-only {3}".format(
                len(doc_beds), len(bank_beds),
                sorted(doc_beds - bank_beds) or u"none",
                sorted(bank_beds - doc_beds) or u"none"), u"documentation"))

    return checks, problems


class _AudioReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} audio-id checks, {1} problems ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} audio-id checks, every cue audible or documented".format(self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()
    rows = bank_rows()
    known = synthesised_ids()
    audible = sum(1 for r in rows if is_synthesised(r, known))

    C.log(u"")
    C.log(u"  audio ids: {0} checks ({1} rows, {2} synthesised, {3} waiting on a "
          u"recording and documented)".format(checked, len(rows), audible, len(rows) - audible))

    if not problems:
        C.log(u"  the bank, the synthesiser, the builder and Docs/12 agree. OK")
    else:
        for message, kind in problems:
            C.log(u"      [{0}] {1}".format(kind, message))

    return _AudioReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - the cue list")
    C.log(u"=" * 70)
    C.log(u"  checks: {0}".format(checked))

    if not problems:
        C.log(u"  every cue the game can ask for exists and is heard or briefed. OK")
        return 0

    C.log(u"  PROBLEMS: {0}".format(len(problems)))
    for message, kind in problems:
        C.log(u"  [{0}] {1}".format(kind, message))

    return 1


if __name__ == "__main__":
    sys.exit(main())
