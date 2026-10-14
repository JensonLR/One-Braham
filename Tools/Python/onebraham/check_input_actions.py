# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_input_actions
============================

Checks that the input pipeline is one pipeline: every action that is generated,
every field the config declares, every key the ini binds, and every field the
controller reads.

WHY THIS EXISTS
    Three input actions - `IA_Map`, `IA_ToggleHUD`, `IA_VoiceLineMenu` - were
    generated as assets by the content pipeline, bound in
    `Config/DefaultInput.ini`, documented in `Docs/08_UI.md`, and had:

      * no field in `UOneBrahamInputConfig` to point at them, and
      * no handler anywhere in C++.

    So they were three keys that did nothing. Then, when handlers were written,
    the code read `InputConfig->Map`, `->ToggleHUD` and `->VoiceLineMenu` - three
    fields that did not exist. That is a compile error rather than a silent
    failure, which is luckier than it sounds, but it is the same defect seen from
    the other end: the input table and the input config were two lists that
    nobody compared.

    A key that does nothing is not visible in a screenshot either. The check
    compares all four sides.

WHAT IT CHECKS
    1. Every action in `build_input.py`'s ACTIONS table has an entry in
       `INPUT_CONFIG_FIELD_ORDER` - i.e. the generated asset has a field that
       points at it.
    2. Every field in `INPUT_CONFIG_FIELD_ORDER` has an action - i.e. the field
       will not be left null on the data asset.
    3. Every action bound in `Config/DefaultInput.ini`, and every action named in
       an `ActionPrompt`, is one of the generated actions. A binding for an action
       that does not exist is a typo that Enhanced Input reports at run time, or
       not at all.
    4. Every `InputConfig->Field` read in `Source/OneBraham` names a field the
       struct actually declares.
    5. The `Required` table inside `UOneBrahamInputConfig::ValidateAllActionsSet`
       names exactly those fields. That table is the fifth list of the same
       actions - it is what `ValidateAllActionsSet` checks at possession time and
       what the control-schema automation test reads - and it had drifted by
       eight rows while a comment above it claimed a test would catch that.
    6. No key is bound to two actions on the same device unless every action on
       it is in one of build_input.py's SHARED_KEY_GROUPS. E was lean-right and
       interact at once, so peeking a corner beside a door opened the door; and
       seven gamepad buttons each did two things.
    7. Every modifier a binding names (`S,Swizzle+Negate`) is one the controller
       knows how to build: it appears as TEXT("<name>") in the player controller.
       A modifier the controller ignores turns S into a second W.
    8. No prompt in C++ or in build_ui.py names a key ("Hold E - override"). A
       prompt says {Interact} and the HUD names the key; every "Hold E" in the
       game was wrong on a pad, wrong after a rebind, and E was the lean key.

    The ACTIONS table is read with `ast`, not by importing `build_input.py`: that
    module imports `unreal`, so importing it would tie a static check to a running
    editor, and the whole point of these checks is that they run without one.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_input_actions.py
"""

from __future__ import print_function

import ast
import io
import os
import re
import sys

from onebraham import common as C

python_root = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))

BUILD_INPUT = os.path.join(python_root, "Tools", "Python", "onebraham", "build_input.py")
SETTINGS_HEADER = os.path.join(python_root, "Source", "OneBraham", "Core", "OneBrahamGameSettings.h")
SETTINGS_SOURCE = os.path.join(python_root, "Source", "OneBraham", "Core", "OneBrahamGameSettings.cpp")
SOURCE_ROOT = os.path.join(python_root, "Source", "OneBraham")
INPUT_INI = os.path.join(python_root, "Config", "DefaultInput.ini")
CONTROLLER_SOURCE = os.path.join(python_root, "Source", "OneBraham", "Core", "OneBrahamPlayerController.cpp")

#: `InputConfig->Field` in C++, with or without a call.
CONFIG_READ = re.compile(r'InputConfig\s*->\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\()?')

#: Methods it is legitimate to call on the config object itself. Anything else
#: after `InputConfig->` is meant to be a field; a field that is not there is the
#: compile error this check exists to find.
CONFIG_METHODS = {
    "GetName", "GetClass", "GetPathName", "GetOuter", "GetFullName",
    "IsValid", "IsValidLowLevel", "StaticClass", "ValidateAllActionsSet",
}

#: `+KeyboardBinding=IA_Jump,SpaceBar` and friends.
INI_BINDING = re.compile(r'^\+[A-Za-z]*Binding\s*=\s*(IA_[A-Za-z0-9_]+)\s*,', re.MULTILINE)

#: Device, action, key and optional modifiers of one shipped binding.
INI_KEYED_BINDING = re.compile(
    r'^\+(Keyboard|Gamepad)Binding\s*=\s*(IA_[A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*(?:,\s*([A-Za-z+]+))?\s*$',
    re.MULTILINE)

#: `+ActionPrompt=IA_Map=Floor plan overlay`.
INI_PROMPT = re.compile(r'^\+ActionPrompt\s*=\s*(IA_[A-Za-z0-9_]+)\s*=', re.MULTILINE)

#: `{ TEXT("Map"), Map }` in ValidateAllActionsSet's Required table.
VALIDATED_FIELD = re.compile(r'\{\s*TEXT\("([A-Za-z0-9_]+)"\)\s*,\s*([A-Za-z0-9_]+)\s*\}')


def _literal_assignment(path, name):
    """The literal value assigned to a module-level name, via ast.

    Returns None if the name is missing or is not a literal.
    """
    if not os.path.isfile(path):
        return None

    tree = ast.parse(io.open(path, encoding="utf-8", errors="ignore").read(), path)

    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id == name:
                    try:
                        return ast.literal_eval(node.value)
                    except ValueError:
                        return None

    return None


def generated_actions():
    """The action names build_input.py creates, without the IA_ prefix."""
    table = _literal_assignment(BUILD_INPUT, "ACTIONS")

    if table is None:
        return []

    return [row[1][3:] for row in table if len(row) > 1 and row[1].startswith("IA_")]


def config_fields():
    """The field names UOneBrahamInputConfig declares."""
    if not os.path.isfile(SETTINGS_HEADER):
        return []

    text = io.open(SETTINGS_HEADER, encoding="utf-8", errors="ignore").read()

    # It is a UCLASS (a data asset), not a USTRUCT: the name says config struct
    # and the declaration says class, which is how a checker ends up parsing
    # nothing and reporting it as a rename.
    match = re.search(r'(?:class|struct)\s+ONEBRAHAM_API\s+UOneBrahamInputConfig\b[^{]*\{', text)

    if not match:
        return []

    depth = 1
    index = match.end()

    while index < len(text) and depth > 0:
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
        index += 1

    body = text[match.end():index]

    # Comments in this struct carry field names as prose, so they are removed
    # with the shared one-pass stripper rather than by two regexes in an order
    # that lets a `/*` inside a comment start a phantom block comment.
    body = C.strip_comments(body)

    return re.findall(r'TObjectPtr<UInputAction>\s+([A-Za-z_][A-Za-z0-9_]*)\s*;', body)


def ini_actions():
    """(kind, action name) for every IA_ the ini binds or prompts."""
    if not os.path.isfile(INPUT_INI):
        return []

    text = io.open(INPUT_INI, encoding="utf-8", errors="ignore").read()
    found = [(u"binding", name[3:]) for name in INI_BINDING.findall(text)]
    found.extend((u"prompt", name[3:]) for name in INI_PROMPT.findall(text))

    return found


def keyed_bindings():
    """(device, action, key, [modifiers]) for every binding the ini ships."""
    if not os.path.isfile(INPUT_INI):
        return []

    text = io.open(INPUT_INI, encoding="utf-8", errors="ignore").read()

    return [(device, action, key, [m for m in (mods or u"").split("+") if m])
            for device, action, key, mods in INI_KEYED_BINDING.findall(text)]


#: A quoted prompt that names a single key: "Hold E - ...", "Press F to ...".
LITERAL_KEY_PROMPT = re.compile(r'"(?:Hold|Press|Tap)\s+[A-Z0-9]\b[^"]*"')


def literal_key_prompts(source_root=None):
    """(file, line, text) for every prompt that hard-codes a key."""
    root = source_root or SOURCE_ROOT
    paths = [BUILD_INPUT.replace("build_input.py", "build_ui.py")]

    for dirpath, _dirs, files in os.walk(root):
        paths.extend(os.path.join(dirpath, n) for n in files if n.endswith((".cpp", ".h")))

    found = []

    for path in paths:
        if not os.path.isfile(path):
            continue

        for number, line in enumerate(io.open(path, encoding="utf-8", errors="ignore"), 1):
            # Prose about the old prompts is allowed; shipping one is not.
            stripped = line.lstrip()
            if stripped.startswith(("//", "*", "/*", "#")):
                continue
            code = line.split("//", 1)[0] if path.endswith((".cpp", ".h")) else line.split("#", 1)[0]

            for match in LITERAL_KEY_PROMPT.findall(code):
                found.append((os.path.relpath(path, python_root), number, match))

    return found


def key_conflicts(bindings, groups):
    """Messages for every key that two actions share outside a declared group."""
    by_key = {}

    for device, action, key, _mods in bindings:
        by_key.setdefault((device, key), set()).add(action)

    messages = []

    for (device, key), actions in sorted(by_key.items()):
        if len(actions) < 2:
            continue

        if any(actions <= set(group) for group in groups):
            continue

        messages.append(u"{0} key {1} is bound to {2} - two verbs on one press".format(
            device, key, u", ".join(sorted(actions))))

    return messages


def validated_fields():
    """(field names, mismatched rows) from ValidateAllActionsSet's Required table.

    Returns two lists so the caller can report a row whose id and field disagree
    as its own kind of problem - a copy-paste in that table is how a field stops
    being checked without anybody noticing.
    """
    if not os.path.isfile(SETTINGS_SOURCE):
        return [], []

    text = io.open(SETTINGS_SOURCE, encoding="utf-8", errors="ignore").read()
    marker = "const TMap<FName, const UInputAction*> Required"

    if marker not in text:
        return [], []

    block = text[text.index(marker):]
    block = block[:block.index("\n\t};")]

    fenced = []

    for name, field in VALIDATED_FIELD.findall(block):
        if name != field:
            fenced.append((name, field))

    return [field for _name, field in VALIDATED_FIELD.findall(block)], fenced


def config_reads(source_root=None):
    """(file, line, field) for every InputConfig-> field read in C++."""
    root = source_root or SOURCE_ROOT

    if not os.path.isdir(root):
        return []

    reads = []

    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            if not name.endswith((".cpp", ".h")):
                continue

            path = os.path.join(dirpath, name)
            raw = io.open(path, encoding="utf-8", errors="ignore").read()
            rel = os.path.relpath(path, root)

            for number, line in enumerate(raw.splitlines(), 1):
                code = line.split("//", 1)[0]

                for field, call in CONFIG_READ.findall(code):
                    # A method call on the config object is not a field read.
                    if call and field in CONFIG_METHODS:
                        continue

                    reads.append((rel, number, field, bool(call)))

    return reads


def run(source_root=None):
    """Returns (read_count, problems)."""
    problems = []

    actions = generated_actions()
    fields = config_fields()

    if not actions:
        return 0, [(u"no input actions were parsed out of build_input.py - the ACTIONS "
                    u"table was renamed or reformatted", u"parse failure")]

    if not fields:
        return 0, [(u"no fields were parsed out of UOneBrahamInputConfig - the struct was "
                    u"renamed or reformatted", u"parse failure")]

    action_set = set(actions)
    field_set = set(fields)

    # 1. Every generated action is pointed at by a field.
    for name in actions:
        if name not in field_set:
            problems.append((
                u"IA_{0} is generated as an asset and no field of "
                u"UOneBrahamInputConfig points at it - the key does nothing".format(name),
                u"unbound action"))

    # 2. Every field has an action to point at.
    for name in fields:
        if name not in action_set:
            problems.append((
                u"UOneBrahamInputConfig::{0} has no IA_{1} asset in the generation "
                u"table - the field stays null".format(name, name),
                u"missing action"))

    # 3. Everything the ini binds or prompts names a real action.
    for kind, name in ini_actions():
        if name not in action_set:
            problems.append((
                u"Config/DefaultInput.ini has a {0} for IA_{1}, which is not a generated "
                u"action - the binding refers to nothing".format(kind, name),
                u"unknown binding"))

    # 4. The validation table names exactly the same fields.
    validated, mismatched_rows = validated_fields()

    if validated:
        for name, field in mismatched_rows:
            problems.append((
                u"ValidateAllActionsSet has a row whose id and field disagree: "
                u"TEXT(\"{0}\") read into {1}".format(name, field),
                u"mismatched row"))

        for field in field_set:
            if field not in validated:
                problems.append((
                    u"UOneBrahamInputConfig::{0} is not in the Required table in "
                    u"ValidateAllActionsSet, so nothing checks that it is assigned".format(field),
                    u"unvalidated field"))

        for field in validated:
            if field not in field_set:
                problems.append((
                    u"the Required table checks a field called {0}, which "
                    u"UOneBrahamInputConfig does not declare".format(field),
                    u"unknown validated field"))

    # 5. Every field the code reads exists on the struct.
    reads = config_reads(source_root)

    for rel, number, field, called in reads:
        if field not in field_set:
            if called:
                problems.append((
                    u"{0}:{1} - InputConfig->{2}(...) is not a field of "
                    u"UOneBrahamInputConfig and not a known UObject method".format(rel, number, field),
                    u"unknown method"))
                continue
            problems.append((
                u"{0}:{1} - InputConfig->{2} is not a field of UOneBrahamInputConfig "
                u"(fields: {3})".format(rel, number, field, ", ".join(sorted(field_set))[:120]),
                u"missing field"))

    # 6. One key, one verb, per device, outside the declared shared groups.
    bindings = keyed_bindings()
    groups = _literal_assignment(BUILD_INPUT, "SHARED_KEY_GROUPS")

    if groups is None:
        problems.append((u"no SHARED_KEY_GROUPS table was parsed out of build_input.py",
                         u"parse failure"))
        groups = []

    for message in key_conflicts(bindings, groups):
        problems.append((message, u"key conflict"))

    # 7. Every modifier is one the controller builds.
    controller = u""
    if os.path.isfile(CONTROLLER_SOURCE):
        controller = io.open(CONTROLLER_SOURCE, encoding="utf-8", errors="ignore").read()

    for device, action, key, mods in bindings:
        for mod in mods:
            if u'TEXT("{0}")'.format(mod) not in controller:
                problems.append((
                    u"{0} binding {1},{2} asks for modifier '{3}', which "
                    u"OneBrahamPlayerController.cpp does not build".format(device, action, key, mod),
                    u"unknown modifier"))

    # 8. No prompt names a key.
    for rel, number, text in literal_key_prompts(source_root):
        problems.append((
            u"{0}:{1} - prompt {2} names a key; write {{Interact}} and let the HUD "
            u"name it".format(rel, number, text),
            u"literal key prompt"))

    return len(reads), problems


class _InputReport(object):
    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            return u"{0} config reads, {1} problem ({2})".format(
                self.checked, len(self.problems), self.problems[0][0][:60])
        return u"{0} config reads, every action bound and every field declared".format(self.checked)


def build_all():
    """Entry point used by run_all.py."""
    checked, problems = run()

    C.log(u"")
    C.log(u"  input config field reads checked: {0}".format(checked))

    if not problems:
        C.log(u"  every generated action has a field, every field has an action, every "
              u"binding names a real action. OK")
        return _InputReport(checked, problems)

    C.log(u"  PROBLEM: {0}".format(len(problems)))
    for message, _kind in problems:
        C.log(u"      {0}".format(message))

    return _InputReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - input actions: generated, declared, bound, and read")
    C.log(u"=" * 70)
    C.log(u"  actions generated: {0}".format(len(generated_actions())))
    C.log(u"  fields declared:   {0}".format(len(config_fields())))
    C.log(u"  config reads:      {0}".format(checked))

    if not problems:
        C.log(u"  agree. OK")
        return 0

    C.log(u"  PROBLEM: {0}".format(len(problems)))
    for message, _kind in problems:
        C.log(u"  {0}".format(message))

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
