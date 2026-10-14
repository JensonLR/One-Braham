# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_interaction
===========================

Checks that the interaction surface is one surface: the classes that implement
it, the table in Docs/13 section 9, the gameplay tags, the hold and reach
constants those tables quote, and the montage contract in section 7.

WHY THIS EXISTS
    Revive and execute were implemented, tuned, logged, documented in three
    places - and impossible to reach.

    `UOneBrahamDownedComponent::ServerRevive` and `ServerExecute` had no caller
    anywhere in the project. The component's own comment said they were "called
    by the reviver's interact component", and the interact component never called
    them, because the downed player did not implement
    `IOneBrahamInteractionInterface` at all: `AOneBrahamCharacter` declared no
    interface, so `Cast<IOneBrahamInteractionInterface>(Hit.GetActor())` on a body
    on the carpet returned null and the prompt was never drawn. Two more layers
    were missing underneath that one. The player capsule's response to the
    `Interactable` channel was `Overlap`, so the trace could not even *block* on a
    colleague; and the hitbox profile blocked the channel by default, so a
    standing player hid the door behind them while a body on the floor could not
    be aimed at. Sixty seconds of bleed-out, a revive, an execution, an animation
    contract, a HUD bar and a voice line, all of it unreachable, and nothing in
    the build said a word about it.

    Every rule below is one of the things that was true-by-absence:

    1. A class that implements the interface is a row in Table 5, and a row names
       a class that implements it. Two directions, because either one alone is a
       list that agrees with itself.
    2. The verb rule lives in `GetInteractionFor()` and the montage a verb plays
       lives in one if/else chain. Both are compared against the table.
    3. Every number in Table 5 resolves to a constant or a property default in the
       code, and the two agree. "4.5 s (`OneBrahamConstants::ReviveSeconds`)" is
       checkable; "4.5 s" is a sentence.
    4. Every declared `OB.Interaction.*` tag is either claimed by a row or listed
       as declared-and-not-emitted, in exactly one of the two, and the second
       list's size is asserted - so a tag cannot arrive in silence, and a tag
       cannot quietly stop being emitted. `OB.Interaction.Revive` and
       `OB.Interaction.Execute` were declared in both the registry and the ini
       and emitted by nothing at all; that is exactly what rule 4 refuses.
    5. Every duration, distance and speed in the three tuning tables in Docs/04
       (movement, physics, the downed state) carries the name of the property
       that defines it, and the two agree. Docs/04 section 4 said a revive was
       4.0 s and left the player on 30 HP for most of this project's life while
       the game healed 40 and held for 4.5, and said the bleed-out window was
       6 s where the constant was 60. Three wrong numbers, in the file a
       designer reads first, and nothing compared them to anything. The rule
       covers durations (`s`), distances (`cm`), speeds (`cm/s`, `cm/s²`) and
       hit points (`HP`) - the units a wrong number crashes into. Angles,
       dimensionless multipliers and the engine's own config values are
       deliberately out of scope, and rows that state them say so.

WHAT IT IGNORES
    Tags inside comments and inside the `UE_DEFINE_GAMEPLAY_TAG` macros
    themselves, any table in Docs/13 other than the two this file names, and the
    prose in Docs/04 around the three tables it does name.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_interaction.py
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

python_root = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

SOURCE_ROOT = os.path.join(python_root, "Source", "OneBraham")
DOCS_13 = os.path.join(python_root, "Docs", "13_Characters_Clothing.md")
DOCS_04 = os.path.join(python_root, "Docs", "04_Character_System.md")
GAME_INI = os.path.join(python_root, "Config", "DefaultGame.ini")
CONSTANTS_H = os.path.join(SOURCE_ROOT, "OneBraham.h")
INTERACT_CPP = os.path.join(SOURCE_ROOT, "Interaction", "OneBrahamInteractComponent.cpp")

INTERFACE_NAME = "IOneBrahamInteractionInterface"

#: `class ONEBRAHAM_API AOneBrahamKeycard : public AActor, public IOneBrahamInteractionInterface`
IMPLEMENTER = re.compile(
    r'^class\s+(?:[A-Za-z0-9_]+_API\s+)?([A-Za-z0-9_]+)\s*:\s*public[^{;]*?'
    + INTERFACE_NAME,
    re.MULTILINE)

#: A row of Table 5: the actor cell is a single backticked class name.
TABLE5_ROW = re.compile(r'^\|\s*`([AU][A-Za-z0-9_]+)`\s*\|(.+?)\|\s*$', re.MULTILINE)

#: A row of Table 6: the tag cell is a single backticked tag.
TABLE6_ROW = re.compile(r'^\|\s*`(OB\.[A-Za-z0-9_.]+)`\s*\|(.+?)\|\s*$', re.MULTILINE)

#: A montage row in section 7's Table 4.
MONTAGE_ROW = re.compile(r'^\|\s*`(AM_[A-Za-z0-9_]+)`\s*\|', re.MULTILINE)

#: `12 s (`UOneBrahamAccessZoneComponent::OverrideSeconds`)` - a number with its
#: source. The source is mandatory: a bare number in these columns is a failure,
#: because a number with no name is a number nobody can keep true.
NUMBER_WITH_SOURCE = re.compile(
    r'([0-9]+(?:\.[0-9]+)?)\s*(s|cm)\s*\(\s*`([A-Za-z0-9_]+(?:::[A-Za-z0-9_]+)?)`\s*\)')

#: Any number that looks like a duration or a distance, used to catch the ones
#: that are not followed by a source.
BARE_NUMBER = re.compile(r'[0-9]+(?:\.[0-9]+)?\s*(?:s|cm)\b')

#: `static constexpr float ReviveSeconds = 4.5f;` and friends. `%s` is the symbol,
#: formatted in at use: a `{0}` left in a compiled pattern is a quantifier, not a
#: placeholder, and Python says so with an unhelpful error about multiple repeats.
CONSTANT_DEF = r'\b(?:static\s+)?constexpr\s+float\s+%s\s*=\s*([0-9.]+)f?\s*;'
MEMBER_DEF = r'\bfloat\s+%s\s*=\s*([0-9.]+)f?\s*;'

#: `MaxStepHeight = 45.f;` - an assignment to an engine property in this project's
#: own constructor. Bare, with no type in front of it, which is exactly how it
#: reads in the file: the declaration belongs to the engine, the number to us.
ASSIGN_DEF = r'(?<![A-Za-z0-9_>.])%s\s*=\s*([0-9.]+)f?\s*;'

#: `SetWalkableFloorAngle(46.f);` - a number passed to an inherited setter in this
#: project's own constructor. The fourth shape: some of the engine's numbers are
#: only ever stated as an argument, and a citation that cannot see an argument
#: would report a correct table as stale.
CALL_DEF = r'(?<![A-Za-z0-9_>.])%s\s*\(\s*([0-9.]+)f?\s*\)'

#: The montage chain: `else if (TagString.Contains(TEXT("Terminal"))) { MontageName = TEXT("..."); }`
CHAIN_LINK = re.compile(
    r'Contains\s*\(\s*TEXT\s*\(\s*"([A-Za-z0-9_]+)"\s*\)\s*\)\s*\)?\s*\{?\s*'
    r'MontageName\s*=\s*TEXT\s*\(\s*"([A-Za-z0-9_]+)"\s*\)')
CHAIN_FALLBACK = re.compile(
    r'else\s*\{?\s*MontageName\s*=\s*TEXT\s*\(\s*"([A-Za-z0-9_]+)"\s*\)')

#: Declared interaction tags, from the ini and from the native registry.
INI_TAG = re.compile(r'\+GameplayTagList\s*=\s*\(\s*Tag\s*=\s*"(OB\.Interaction\.[A-Za-z0-9_.]+)"')
NATIVE_TAG = re.compile(
    r'UE_DEFINE_GAMEPLAY_TAG(?:_COMMENT)?\s*\(\s*[A-Za-z0-9_]+\s*,\s*"(OB\.Interaction\.[A-Za-z0-9_.]+)"')

#: How many tags Table 6 may list. Asserted so the list can only shrink on
#: purpose: the day a tag leaves it, somebody has built the thing it was waiting
#: for, and the day one arrives, somebody has decided it is not emit-able yet.
LEDGER_SIZE = 8

#: How many rows Table 5 must have. The six classes that implement the interface.
TABLE5_SIZE = 6

#: `60 cm/s (`UOneBrahamCharacterMovementComponent::DownedCrawlSpeed`)`: a number,
#: its unit, and the name of the property that defines it. `\u00b2` is written as
#: an escape so that this file stays ASCII; the regex engine is what turns it
#: into the character.
#: `60 cm/s (`UOneBrahamCharacterMovementComponent::DownedCrawlSpeed`)`, and its
#: unitless cousin `0.14 (`...::GroundAirControl`)`, and `46\u00b0 (`...`)`.
#: A number in one of the three Docs/04 tables states its unit when it has one and
#: always states the property that defines it. `\u00b2` and `\u00b0` are written as
#: escapes so that this file stays ASCII; the regex engine is what turns them into
#: the characters.
DOC04_CLAIM = re.compile(
    r'([0-9]+(?:\.[0-9]+)?)\s*(cm/s\u00b2|cm/s|cm|s|HP)?\s*\u00b0?\s*\(\s*`([A-Za-z0-9_]+(?:::[A-Za-z0-9_]+)?)`\s*\)')

#: Anything in these tables that is not part of a code span and not immediately
#: followed by a source. A number with no name is a number nobody can keep true,
#: which is exactly how Docs/04 section 4 said 4.0 s and 30 HP for a game that
#: held for 4.5 s and healed 40.
DOC04_DIGIT = re.compile(r'[0-9]')

#: A code span: a number inside one is part of an expression (`SprintSpeed * 0.98`),
#: and the expression is the citation.
CODE_SPAN = re.compile(r'`[^`]*`')
#: The three Docs/04 tables: (what to call it, first line, last line, how many
#: sourced numbers it must have). The size is asserted for the same reason Table
#: 5's is: a source can only leave a table when somebody decides it should, which
#: is the day they decide the number no longer needs one. Gravity is a row with
#: no numbers at all - the project does not set it, so there is nothing to cite.
DOC04_TABLES = (
    (u"section 2 movement speeds",
     u"| Movement | Speed |",
     u"Every row is a `UOneBrahamCharacterMovementComponent`", 7),
    (u"section 2 physics",
     u"| Physics | Value |",
     u"Two rows here are deliberately unitless", 7),
    (u"section 4 downed state",
     u"| State | Duration |",
     u"This table described a different game", 6),
)


def read(path):
    if not os.path.isfile(path):
        return u""

    return io.open(path, encoding="utf-8", errors="ignore").read()


def strip_comments(text):
    """Source with comments removed, so a tag named in a comment is not a claim."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    return re.sub(r'//[^\n]*', '', text)


def docs_text():
    return read(DOCS_13)


def slice_of(text, start_marker, end_marker):
    """The text between two markers, or '' when either is missing."""
    start = text.find(start_marker)

    if start == -1:
        return u""

    end = text.find(end_marker, start)

    return text[start:end if end != -1 else len(text)]


def docs_slice(start_marker, end_marker):
    """The text between two markers in Docs/13, or '' when either is missing."""
    return slice_of(docs_text(), start_marker, end_marker)


def implementers():
    """class name -> file, for every class that declares the interface."""
    out = {}

    for root, _dirs, files in os.walk(SOURCE_ROOT):
        for name in sorted(files):
            if not name.endswith(".h"):
                continue

            path = os.path.join(root, name)

            if name == "OneBrahamInteractionInterface.h":
                continue

            for match in IMPLEMENTER.finditer(strip_comments(read(path))):
                out[match.group(1)] = path

    return out


def table5_rows():
    """actor -> list of cells, for Table 5 in Docs/13 section 9."""
    block = docs_slice("**Table 5", "**Table 6")
    out = {}

    for match in TABLE5_ROW.finditer(block):
        cells = [cell.strip() for cell in match.group(2).split("|")]

        if len(cells) >= 5:
            out[match.group(1)] = cells

    return out


def table6_rows():
    """tag -> list of cells, for Table 6 in Docs/13 section 9."""
    block = docs_slice("**Table 6", "The two HUD rows are the interesting ones")
    out = {}

    for match in TABLE6_ROW.finditer(block):
        cells = [cell.strip() for cell in match.group(2).split("|")]

        if len(cells) >= 2:
            out[match.group(1)] = cells

    return out


def table4_montages():
    """Every montage in section 7's montage table."""
    block = docs_slice("**Table 4", "**A missing montage is not an error")
    return set(MONTAGE_ROW.findall(block))


def claimed_tags():
    """Every interaction tag a Table 5 row says it emits, in table order."""
    out = []

    for _actor, cells in table5_rows().items():
        for tag in re.findall(r'`(OB\.Interaction\.[A-Za-z0-9_.]+)`', cells[1]):
            if tag not in out:
                out.append(tag)

    return out


def montage_chain():
    """(substring, montage) pairs and the fallback, from the interact component."""
    text = strip_comments(read(INTERACT_CPP))

    links = [(match.group(1), match.group(2)) for match in CHAIN_LINK.finditer(text)]
    fallback = CHAIN_FALLBACK.search(text)

    return links, (fallback.group(1) if fallback else u"")


def montage_for(tag, links, fallback):
    """The montage the component would load for a tag. Contains(), in order, then the fallback."""
    for substring, montage in links:
        if substring in tag:
            return montage

    return fallback


def class_files(class_name):
    """Every file of a class: the header that declares it and the .cpp that defines it.

    A forward declaration (`class UOneBrahamDownedComponent;` in a header that only
    needs the pointer) is not a declaration of the class: it has no members, and a
    table row that resolved to one would look up defaults in a file that has none.
    The search therefore requires the class body to start, and prefers the header
    that is named after the class when more than one file has the body.
    """
    body = re.compile(r'^class\s+(?:[A-Za-z0-9_]+_API\s+)?' + class_name +
                      r'\s*(?:final\s*)?(?::|\{)', re.MULTILINE)
    candidates = []

    for root, _dirs, files in os.walk(SOURCE_ROOT):
        for name in sorted(files):
            if not name.endswith(".h"):
                continue

            path = os.path.join(root, name)

            if body.search(read(path)):
                candidates.append(path)

    header = u""

    for path in candidates:
        if os.path.basename(path) == class_name[1:] + ".h" or \
                os.path.basename(path) == class_name + ".h":
            header = path
            break

    if not header and candidates:
        header = candidates[0]

    out = []

    for path in (header, header[:-2] + ".cpp" if header else u""):
        if path and os.path.isfile(path):
            out.append(path)

    return out


def declared_tags():
    """Every OB.Interaction.* tag this project declares, native or ini."""
    tags = set(INI_TAG.findall(read(GAME_INI)))
    tags.update(NATIVE_TAG.findall(strip_comments(read(os.path.join(
        SOURCE_ROOT, "Core", "OneBrahamGameplayTags.cpp")))))

    return tags


def resolve_symbol(symbol):
    """(values, problem). The values every definition of a symbol gives it.

    Three shapes, all of them used by Table 5:
      * `OneBrahamConstants::ReviveSeconds` - a constant in OneBraham.h
      * `AOneBrahamAccessZone::OBDoorInteractionRange` - a file-local constant in
        that class's .cpp, named so the table can quote it
      * `AOneBrahamDownedComponent::ExecuteSeconds` - a UPROPERTY default in that
        class's header
      * `UOneBrahamCharacterMovementComponent::MaxStepHeight` - a property this
        project inherited from the engine and assigns in its own constructor
        (`MaxStepHeight = 45.f;`)
      * `UOneBrahamCharacterMovementComponent::SetWalkableFloorAngle` - a setter
        it inherited and calls with a literal (`SetWalkableFloorAngle(46.f);`)

    The last two shapes exist because that is where those numbers actually live.
    A citation that could not see them would report a correct table as stale, and
    a checker that reports a correct table is a checker somebody switches off.
    """
    if "::" in symbol:
        class_name, member = symbol.split("::", 1)

        if class_name == "OneBrahamConstants":
            # The project's constants namespace lives in Source/OneBraham/OneBraham.h
            # and is not a class: it is where the numbers that several systems share
            # are written down once.
            files = [CONSTANTS_H]
        else:
            files = class_files(class_name)

        if not files:
            return [], u"the table quotes {0}, and there is no class {1} in Source/".format(
                symbol, class_name)
    else:
        member = symbol
        files = [CONSTANTS_H, INTERACT_CPP]

    values = []

    for path in files:
        text = strip_comments(read(path))

        for pattern in (CONSTANT_DEF, MEMBER_DEF, ASSIGN_DEF, CALL_DEF):
            for value in re.findall(pattern % re.escape(member), text):
                values.append(float(value))

    if not values:
        return [], u"the table quotes {0}, and nothing in {1} defines it".format(
            symbol, ", ".join(os.path.basename(p) for p in files) or "the project")

    if len(set(values)) > 1:
        return [], u"the table quotes {0}, and its definitions disagree: {1}".format(
            symbol, ", ".join(str(v) for v in values))

    return values, None


def run():
    """Returns (checks, problems)."""
    problems = []
    checks = 0

    classes = implementers()
    rows = table5_rows()
    ledger = table6_rows()

    # --- 1. The implementers and the table --------------------------------
    if not classes:
        problems.append((u"no class in Source/ declares {0}".format(INTERFACE_NAME),
                         u"parse failure"))

    if not rows:
        problems.append((u"Table 5 in Docs/13 section 9 was not found or has moved",
                         u"parse failure"))

    checks += 1

    if len(rows) != TABLE5_SIZE:
        problems.append((
            u"Table 5 has {0} rows and {1} classes implement the interface".format(
                len(rows), TABLE5_SIZE),
            u"table size"))

    for class_name in sorted(classes):
        checks += 1

        if class_name not in rows:
            problems.append((
                u"{0} implements {1} and is not in Table 5, so nothing says what it "
                u"does when a player looks at it".format(class_name, INTERFACE_NAME),
                u"undocumented interactable"))

    for actor in sorted(rows):
        checks += 1

        if actor not in classes:
            problems.append((
                u"Table 5 documents '{0}' as an interactable and it does not implement "
                u"{1}".format(actor, INTERFACE_NAME),
                u"missing implementation"))

    # --- 2. Every number in the table has a source, and it agrees --------
    for actor in sorted(rows):
        cells = rows[actor]

        for column, cell in ((u"Hold", cells[2]), (u"Reach", cells[3])):
            sourced = set()

            for match in NUMBER_WITH_SOURCE.finditer(cell):
                checks += 1
                number, unit, symbol = float(match.group(1)), match.group(2), match.group(3)
                sourced.add(match.group(0))

                values, problem = resolve_symbol(symbol)

                if problem:
                    problems.append((u"{0}'s {1}: {2}".format(actor, column, problem),
                                     u"unresolvable source"))
                elif abs(values[0] - number) > 0.001:
                    problems.append((
                        u"{0}'s {1} says {2} {3} and {4} is {5}".format(
                            actor, column, number, unit, symbol, values[0]),
                        u"stale number"))

            stripped = cell

            for piece in sourced:
                stripped = stripped.replace(piece, u"")

            for match in BARE_NUMBER.finditer(stripped):
                checks += 1
                problems.append((
                    u"{0}'s {1} quotes \"{2}\" with no source: a number in this table "
                    u"must name the constant it is".format(actor, column, match.group(0)),
                    u"unsourced number"))

    # --- 3. Every tag is claimed, or ledgered, and never both ------------
    declared = declared_tags()
    claimed = claimed_tags()

    if not declared:
        problems.append((u"no OB.Interaction.* tag is declared anywhere",
                         u"parse failure"))

    for tag in sorted(declared):
        checks += 1

        in_table = tag in claimed
        in_ledger = tag in ledger

        if in_table and in_ledger:
            problems.append((
                u"{0} is claimed by Table 5 and listed in Table 6".format(tag),
                u"tag in both tables"))
        elif not in_table and not in_ledger:
            problems.append((
                u"{0} is declared and neither claimed by Table 5 nor listed in "
                u"Table 6, so nothing emits it and nothing says so".format(tag),
                u"unclaimed tag"))

    for tag in sorted(claimed) + sorted(ledger):
        checks += 1

        if tag not in declared:
            problems.append((
                u"Docs/13 names {0}, which Config/DefaultGame.ini and the native "
                u"registry do not declare".format(tag),
                u"undeclared tag"))

    checks += 1

    if len(ledger) != LEDGER_SIZE:
        problems.append((
            u"Table 6 lists {0} declared-and-not-emitted tags and the checker expects "
            u"{1}: a tag leaves this table by acquiring an emitter".format(
                len(ledger), LEDGER_SIZE),
            u"ledger size"))

    # --- 4. The montage a verb plays is the montage the table says -------
    links, fallback = montage_chain()
    families = set([name for _sub, name in links] + ([fallback] if fallback else []))

    if not links or not fallback:
        problems.append((u"the montage chain in PlayInteractionMontage was not found",
                         u"parse failure"))

    documented_montages = table4_montages()

    for montage in sorted(families):
        checks += 1

        if montage not in documented_montages:
            problems.append((
                u"the interact component can play {0} and Docs/13 table 4 does not "
                u"describe it".format(montage),
                u"undocumented montage"))

    for montage in sorted(documented_montages):
        checks += 1

        if montage not in families:
            problems.append((
                u"Docs/13 table 4 describes {0} and the interact component can never "
                u"play it".format(montage),
                u"unreachable montage"))

    for actor in sorted(rows):
        cells = rows[actor]
        tags = re.findall(r'`(OB\.Interaction\.[A-Za-z0-9_.]+)`', cells[1])
        documentated = set(re.findall(r'`(AM_[A-Za-z0-9_]+)`', cells[4]))

        for tag in tags:
            checks += 1
            actual = montage_for(tag, links, fallback)

            if actual not in documentated:
                problems.append((
                    u"{0} says {1} plays {2} and the component plays {3}".format(
                        actor, tag, u"/".join(sorted(documentated)) or u"nothing", actual),
                    u"wrong montage"))

    # --- 5. Docs/04's three tuning tables -------------------------------
    # Section 4 of Docs/04 said a revive was 4.0 s and left the player on 30 HP
    # while the game healed 40 and held for 4.5, and called a 60 s bleed-out a
    # "6-second bleed-out window". Nothing compared the table to the code. This is
    # that comparison. Two rules per table: every number states the property that
    # defines it (or is part of an expression inside a code span, where the
    # expression is the citation), and the two must agree.
    doc04 = read(DOCS_04)

    if not doc04:
        problems.append((u"Docs/04_Character_System.md was not found",
                         u"parse failure"))
    else:
        for label, start, end, expected in DOC04_TABLES:
            block = slice_of(doc04, start, end)
            checks += 1

            if not block:
                problems.append((
                    u"Docs/04 {0}: the table was not found or has moved".format(label),
                    u"parse failure"))
                continue

            found = 0
            masked = list(block)

            def mask(lo, hi):
                for i in range(lo, min(hi, len(masked))):
                    masked[i] = u" "

            for match in DOC04_CLAIM.finditer(block):
                checks += 1
                found += 1
                mask(*match.span())
                number, unit, symbol = (float(match.group(1)), match.group(2),
                                        match.group(3))

                values, problem = resolve_symbol(symbol)

                if problem:
                    problems.append((u"Docs/04 {0}: {1}".format(label, problem),
                                     u"unresolvable source"))
                elif abs(values[0] - number) > 0.001:
                    problems.append((
                        u"Docs/04 {0} says {1} {2} and {3} is {4}".format(
                            label, number, unit or u"", symbol, values[0]),
                        u"stale number"))

            # Code spans are part of the citation (`SprintSpeed * 0.98`); the digits
            # inside them are not claims of their own.
            for match in CODE_SPAN.finditer(block):
                mask(*match.span())

            bare = DOC04_DIGIT.search(u"".join(masked))

            if bare:
                checks += 1
                context = u"".join(masked)
                line = context[max(0, bare.start() - 40):bare.start() + 24].strip()
                problems.append((
                    u"Docs/04 {0} quotes a number with no source (near \"{1}\")".format(
                        label, line),
                    u"unsourced number"))

            checks += 1

            if found != expected:
                problems.append((
                    u"Docs/04 {0} quotes {1} numbers with a source and the checker "
                    u"expects {2}: a number leaves a table by acquiring a name, and a "
                    u"row leaves by somebody deciding its number no longer needs one"
                    .format(label, found, expected),
                    u"table size"))

    return checks, problems


class _InteractionReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} interaction checks, {1} problem(s) ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])

        return u"{0} interaction checks, {1} interactables, {2} tags claimed, "\
               u"{3} declared and waiting".format(
                   self.checked, len(table5_rows()), len(claimed_tags()), len(table6_rows()))


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()

    C.log(u"")
    C.log(u"  interaction checks: {0} ({1} interactables, {2} tags claimed, {3} "
          u"declared and waiting)".format(
              checked, len(table5_rows()), len(claimed_tags()), len(table6_rows())))

    if not problems:
        C.log(u"  the classes, the two tables, the tag registry, the hold constants "
              u"and the montage chain agree. OK")
    else:
        for message, kind in problems:
            C.log(u"      [{0}] {1}".format(kind, message))

    return _InteractionReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - the interaction surface")
    C.log(u"=" * 70)
    C.log(u"  checks: {0}".format(checked))

    if not problems:
        C.log(u"  every interactable is documented, every verb is reachable, every "
              u"number has a source. OK")
        return 0

    C.log(u"  PROBLEMS: {0}".format(len(problems)))
    for message, kind in problems:
        C.log(u"  [{0}] {1}".format(kind, message))

    return 1


if __name__ == "__main__":
    sys.exit(main())
