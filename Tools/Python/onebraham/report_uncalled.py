# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.report_uncalled
=========================

The uncalled-API report: every member function this module declares, defines and
never calls.

WHY THIS EXISTS
    A project of this size has three kinds of uncalled function, and they need
    three different answers:

      1. **A hole.** The method is the only implementation of a rule the game is
         supposed to have - a spawn loadout, a revive, an assist, a winch call -
         and nothing reaches it. `UOneBrahamExtractionComponent::ServerCallExtraction`
         spent weeks in this state: a complete state machine, four callers short of
         working. This is the kind that ships as a bug.
      2. **A Blueprint surface.** `UFUNCTION(BlueprintCallable)` on a component a
         Blueprint subclass is meant to call. Nothing in C++ will ever call it, and
         that is correct - but only once the Blueprint exists, which for this
         project is documented in Docs/08 and Docs/10 rather than committed.
      3. **An engine override.** `virtual ... override` on a method the engine
         calls. There is never a call site in the module and there never should be.
         No signal at all.

    Only textual evidence can separate them, and only a machine should look: the
    alternative is a hand audit of 107 headers, which is how the first ten of
    these were missed.

WHAT IT CHECKS
    1. Every non-virtual member function declared in a header under
       `Source/OneBraham`, whose name appears in no call position anywhere in the
       module: no `.Name(`, no `->Name(`, no `::Name(`, no `&Class::Name` delegate
       binding, no bare `Name(` in a file that is not the one declaring it.
    2. Each one is reported with the class that declares it, the file and line of
       its definition, and a triage hint: `[BP]` for a Blueprint-callable
       declaration, `[net]` for an RPC.

    A `[BP]` hint is not an excuse and the report never hides one. A method that is
    only ever meant to be called by a Blueprint is only *covered* once a Blueprint
    calls it, and this project keeps its Blueprints in Docs/08 and Docs/10 rather
    than in the repository - so a `[BP]` line with no Blueprint to match is a
    question about the content layer, and a `[BP]` line in a system whose
    Blueprints are all built by `build_ui.py` is a hole. The extraction state
    machine's four `Server*` methods are the worked example: every one of them was
    `BlueprintCallable`, and none of them was reachable, so the win condition of
    the game's main mode did not work.

WHAT IT IGNORES, DELIBERATELY
    * Definitions in the same file as the declaration: a private helper called
      once from the function below it is not interesting.
    * Anything named in `KNOWN_ENGINE_CALLBACKS` below - `BeginPlay`, `Tick`,
      `OnConstruction`, `GetLifetimeReplicatedProps` and the rest of the engine's
      own vocabulary. The engine calls those; no call site is expected.

HOW TO READ IT
    Work down the list from `[net]` and the extraction/weapon/interaction classes:
    a missing caller in the game's own systems is a bug, and the same report one
    commit later is the proof it is fixed. It is a report, not a gate - it exits 0
    whatever it finds, because "this method has no caller" is a question, and only
    a person can answer it.

USAGE
    cd One-Braham
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/report_uncalled.py
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/report_uncalled.py --json
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/report_uncalled.py --include-virtual
"""

from __future__ import print_function

import io
import json
import os
import re
import sys

from onebraham import common as C

SOURCE_ROOT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "Source", "OneBraham"))

#: A member function declaration in a class body: return type, name, parameter
#: list, optional `const`, optional `override`/`final`, semicolon.
DECLARATION = re.compile(
    r'^\s*(?P<spec>virtual\s+|static\s+|explicit\s+|inline\s+)*'
    r'(?:[A-Za-z_][A-Za-z0-9_:<>\*&\s,]*?[\s\*&])'
    r'(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\([^;{]*\)\s*'
    r'(?P<tail>(?:const\s*)?(?:override\s*)?(?:final\s*)?);\s*$')

#: A `UCLASS`/`USTRUCT` body line that is not a function at all.
NOT_A_FUNCTION = (
    "UPROPERTY", "UFUNCTION", "GENERATED_BODY", "DECLARE_", "UE_DECLARE_",
    "ECLARE_", "return", "if", "while", "for", "switch", "case", "else", "do",
)

#: Statement keywords whose name would otherwise parse as a declaration.
STATEMENT_KEYWORDS = (
    "return", "if", "while", "for", "switch", "case", "else", "do", "delete",
    "new", "sizeof", "catch", "static_assert", "check", "ensure",
)

#: Members the engine calls. There is no call site in the module and there should
#: not be one.
KNOWN_ENGINE_CALLBACKS = {
    "BeginPlay", "EndPlay", "Tick", "TickComponent", "BeginDestroy",
    "PostInitializeComponents", "OnConstruction", "GetLifetimeReplicatedProps",
    "GetReplicatedCustomConditionState", "GetSubobjectReplicationList",
    "PostNetReceive", "PreReplication", "IsSupportedForNetworking",
    "ShouldCreateSubsystem", "Initialize", "Deinitialize", "GetStatId",
    "IsTickable", "IsTickableInEditor", "IsTickableWhenPaused", "GetStatGroup",
    "SetupPlayerInputComponent", "PossessedBy", "UnPossessed", "OnRep_Owner",
    "NotifyActorBeginOverlap", "NotifyActorEndOverlap", "FellOutOfWorld",
    "ReceiveDestroyed", "Destroyed", "SetActorTickEnabled",
    "CopyProperties", "ClientInitialize", "OverrideWith", "HandleStartingNewPlayer",
    "PlayerCanRestart", "GetNetPriority", "GetNetEntityId",
    # UHT generates these pairs for RPCs and rep notifies.
    "OnRep_", "_Implementation", "_Validate",
}

#: Anything whose name starts with one of these is UHT-generated or a macro, not
#: a member function worth reporting.
GENERATED_PREFIXES = ("OnRep_", "K2_", "StaticClass", "Z_Construct", "exec")


def _headers_and_sources():
    """Every header and source file in the module, keyed by absolute path."""
    paths = []

    for dirpath, _dirs, names in os.walk(SOURCE_ROOT):
        for name in sorted(names):
            if name.endswith((".h", ".cpp")):
                paths.append(os.path.join(dirpath, name))

    return paths


def _read(paths):
    texts = {}

    for path in paths:
        try:
            texts[path] = io.open(path, encoding="utf-8", errors="ignore").read()
        except IOError:
            continue

    return texts


def _relative(path):
    return os.path.relpath(path, os.path.dirname(SOURCE_ROOT)).replace("\\", "/")


def _declarations(texts):
    """(name, class, path, line, hints) for every member function declared."""
    found = {}

    for path, text in texts.items():
        if not path.endswith(".h"):
            continue

        # A class body only: a forward declaration or a free function is not a
        # member, and a header with no class in it has nothing to report.
        if re.search(r'^\s*(class|struct)\s+\w+', text, re.MULTILINE) is None:
            continue

        # An interface's events are never called by name: the engine's generated
        # code calls `Execute_X(Obj, ...)` and an implementer calls
        # `X_Implementation(...)`. Both are call sites, and both are counted as
        # such below, so the interface's own declarations are not findings.
        if "UINTERFACE(" in text:
            continue

        class_name = os.path.basename(path)[:-2]
        lines = text.splitlines()

        for number, line in enumerate(lines, 1):
            stripped = line.strip()

            if stripped.startswith(("//", "*", "/*", "#")):
                continue

            if any(stripped.startswith(token) for token in NOT_A_FUNCTION):
                continue

            match = DECLARATION.match(line)

            if match is None:
                continue

            name = match.group("name")

            if name in STATEMENT_KEYWORDS:
                continue

            if name.startswith(GENERATED_PREFIXES):
                continue

            if name == class_name:
                # A constructor: called by the engine's spawner, never by name.
                continue

            is_virtual = "virtual" in (match.group("spec") or "") or \
                "override" in (match.group("tail") or "")

            # The nearest UFUNCTION / UPROPERTY block above the declaration tells
            # us whether this exists for Blueprint to call.
            hints = []
            for back in range(1, 4):
                if number - 1 - back < 0:
                    break
                probe = lines[number - 1 - back].strip()
                if probe.startswith("UFUNCTION"):
                    if "Blueprint" in probe:
                        hints.append("BP")
                    if "Server" in probe or "Client" in probe or "NetMulticast" in probe:
                        hints.append("net")
                    break
                if probe.startswith("UPROPERTY") or probe.startswith("//"):
                    continue
                break

            found.setdefault(name, []).append({
                "name": name, "class": class_name, "header": _relative(path),
                "line": number, "virtual": is_virtual, "hints": hints})

    return found


def _definition_line(name, class_name, texts):
    """Where the definition is, and whether the definition is in the header."""
    definition = u""

    for path, text in texts.items():
        if not path.endswith(".cpp"):
            continue

        lines = text.splitlines()

        for number, line in enumerate(lines, 1):
            stripped = line.strip()

            if stripped.endswith(";"):
                continue

            if re.match(r'^[\w:<>,\*&\s]*\b' + re.escape(class_name) + r'::' + re.escape(name) + r'\s*\(', stripped):
                return u"{0}:{1}".format(_relative(path), number)

    return definition


def _call_sites(name, texts, declaring_path):
    """Every line that calls `name`, anywhere in the module."""
    sites = []

    for path, text in texts.items():
        for number, line in enumerate(text.splitlines(), 1):
            if name not in line:
                continue

            stripped = line.strip()

            if stripped.startswith(("//", "*")):
                continue

            # `&UClass::Name` - a delegate binding, which is a call in every way
            # that matters: deleting the method breaks the binding.
            if re.search(r'&\s*[\w:]*::' + re.escape(name) + r'\b', stripped):
                sites.append((path, number))
                continue

            # A declaration in the header that declares it.
            if path == declaring_path and DECLARATION.match(line):
                continue

            # A definition: `ReturnType Class::Name(` with a body, not a call.
            if re.match(r'^[\w:<>,\*&\s]*\b\w+::' + re.escape(name) + r'\s*\(', stripped) and \
                    not stripped.endswith(";"):
                continue

            # A call: `.Name(`, `->Name(`, `::Name(`, a bare `Name(`, an
            # interface dispatch (`Execute_Name(`) or an implementer's own
            # `Name_Implementation(`. All three are somebody reaching the method.
            if re.search(r'(?:[.>]|::|\b)' + re.escape(name) + r'\s*\(', stripped) \
                    or re.search(r'\bExecute_' + re.escape(name) + r'\s*\(', stripped) \
                    or re.search(r'\b' + re.escape(name) + r'_Implementation\s*\(', stripped):
                sites.append((path, number))

    return sites


def run(include_virtual=False):
    """(findings, texts) for every declared member with no call site."""
    paths = _headers_and_sources()
    texts = _read(paths)
    declarations = _declarations(texts)

    findings = []

    for name in sorted(declarations):
        for entry in declarations[name]:
            if name in KNOWN_ENGINE_CALLBACKS:
                continue

            if entry["virtual"] and not include_virtual:
                continue

            # `header` is relative to the Source/ directory the report walks.
            sites = _call_sites(name, texts, os.path.join(
                os.path.dirname(SOURCE_ROOT), entry["header"]))

            if sites:
                continue

            entry = dict(entry)
            entry["definition"] = _definition_line(name, entry["class"], texts)
            findings.append(entry)

    return findings, texts


def build_all(include_virtual=False):
    """Entry point used by run_all.py (only if this is ever wired in)."""
    findings, _texts = run(include_virtual=include_virtual)

    C.log(u"  members declared, defined and never called: {0}".format(len(findings)))
    C.log(u"  this is a report, not a gate: every one of them is a question.")

    return findings


def main():
    include_virtual = "--include-virtual" in sys.argv
    as_json = "--json" in sys.argv

    findings, _texts = run(include_virtual=include_virtual)

    if as_json:
        print(json.dumps(findings, indent=2, sort_keys=True))
        return 0

    C.log(u"")
    C.log(u"ONE BRAHAM - declared, defined, never called")
    C.log(u"=" * 70)
    C.log(u"")

    by_class = {}

    for entry in findings:
        by_class.setdefault(entry["class"], []).append(entry)

    for class_name in sorted(by_class):
        entries = sorted(by_class[class_name], key=lambda item: item["name"])
        C.log(u"  {0}  ({1})".format(class_name, entries[0]["header"]))

        for entry in entries:
            tags = u" ".join(u"[{0}]".format(hint) for hint in entry["hints"])
            C.log(u"      {0:<44} {1:<28} {2}".format(
                entry["name"], entry["definition"] or u"(inline)", tags))

        C.log(u"")

    C.log(u"  {0} member(s) with no call site".format(len(findings)))
    C.log(u"  [BP] exists for a Blueprint to call; [net] is an RPC only a client can send.")
    C.log(u"  Everything else is either wired up in this commit or a hole in the game.")
    C.log(u"")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
