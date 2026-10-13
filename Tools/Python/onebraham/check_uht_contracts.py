# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_uht_contracts
=============================

Checks the contracts UnrealHeaderTool enforces, without UnrealHeaderTool.

WHY THIS EXISTS
    UHT is the first thing a build runs and the last thing anybody reads. It
    fails on problems that are all mechanical, all easy to make in a
    hand-written header, and all reported with a line number inside a
    .generated.h file the author has never opened:

      1. A UFUNCTION(Server/Client/NetMulticast) with no `_Implementation`.
         UHT generates the thunk that calls it, the engine compiles, and the
         *linker* finds the hole - minutes later, in another translation unit.

      2. A UE RPC with a return value. Unreal's RPCs cannot return anything;
         UHT rejects it outright, and the mistake always looks reasonable when
         it is written ("the server tells me whether it worked").

      3. A UFUNCTION(BlueprintNativeEvent) with no `_Implementation`. The
         Blueprint override is optional; the C++ default body is not.

      4. A UFUNCTION(BlueprintImplementableEvent) that somebody also defined in
         C++, which UHT rejects and which is always a leftover.

      5. `ReplicatedUsing = OnRep_Thing` naming something that is not a
         UFUNCTION, or is nothing at all.

      6. **A Replicated specifier on a struct member.** A struct is not a
         replication unit; it travels as one property on the object that
         contains it. The specifier is not an error to UHT and not an error to
         the compiler - it is a lie to the next reader, which is worse.

      7. **A C++ UUserWidget that overrides NativeTick.** A UserWidget is created
         with EWidgetTickFrequency::Auto, and Auto means "tick only if this
         widget has a tick event implemented in Blueprint". An override of
         NativeTick is not one, so the engine never calls it: the animation in it
         does not stutter, does not warn - it never runs, and the widget is drawn
         frozen at whatever the first frame set. Three widgets in this project
         had that shape, including the HUD's banner, subtitle and damage-flash
         timers. Paint is the callback a visible widget is guaranteed, so time
         belongs in NativePaint (see UOneBrahamHUDWidget::NativePaint).

         A class that assigns TickFrequency is exempt: that is the deliberate way
         to ask for a tick, and the check is about doing it on purpose.

      8. **A replicated property with no DOREPLIFETIME.** This is the expensive
         one. It compiles, it validates, it does not warn, and the property
         simply never reaches a client - so a team score, a lockdown stage or a
         squad id is correct on the listen server and permanently zero
         everywhere else. It is the single most common bug in a hand-written
         multiplayer project and the hardest to notice while testing alone.

    All of them are found by comparing headers against sources textually, in a
    fifth of a second, on a machine with no engine on it.

WHAT IT IS NOT
    Not a compiler. It does not resolve overloads, does not know about
    inheritance, and it checks names and shapes rather than types. Where the
    cheap version would produce a false positive, it chooses the quieter
    check: a RepNotify is verified against the whole module rather than against
    its own class, because a base class may legitimately own the callback.

PARSING
    Class bodies are located by brace matching, then split into brace-depth-zero
    statements, exactly as check_cpp_symbols does. A statement is attributed to
    the macro immediately above it (UPROPERTY, UFUNCTION and friends), which is
    what lets the checks read specifiers instead of guessing from surrounding
    text - the first version of this file looked backwards 400 characters for
    the word UFUNCTION and reported 22 correct RepNotify callbacks as broken.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_uht_contracts.py
    (no editor needed; run by run_all --text-only and by validate_project)
"""

from __future__ import print_function

import io
import os
import re

from onebraham import common as C

SOURCE_ROOT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "Source", "OneBraham"))

#: UFUNCTION specifiers that make UHT generate a thunk calling `_Implementation`.
RPC_SPECIFIERS = ("Server", "Client", "NetMulticast")

#: UFUNCTION specifiers that need a C++ default body named `_Implementation`.
IMPLEMENTATION_SPECIFIERS = ("BlueprintNativeEvent",)

#: UFUNCTION specifiers that must have no C++ body at all.
NO_BODY_SPECIFIERS = ("BlueprintImplementableEvent",)

MACRO_START = re.compile(r'^([A-Z][A-Z0-9_]*)\s*\(')
ACCESS_SPECIFIER = re.compile(r'^\s*(?:public|protected|private)\s*:\s*')


def strip_comments_and_strings(text):
    """Removes comments and string literals, preserving line count."""
    text = re.sub(r'/\*.*?\*/', lambda m: "\n" * m.group(0).count("\n"), text, flags=re.DOTALL)
    text = re.sub(r'//[^\n]*', "", text)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    return text


def class_bodies(text):
    """The shared class-body brace matcher, which lives in common.py."""
    return C.class_bodies(text)


def split_statements(body):
    """The shared class-body parser, which lives in common.py.

    Two checkers need this and it took three iterations to get right - a macro
    invocation is not semicolon-terminated, and `protected:` lands in the same
    statement as the macro above it - so there is exactly one copy of it.
    """
    return C.split_class_statements(body)


def _specifier_list(macros, macro_name):
    """The specifiers of the last <macro_name> in a statement's macro list."""
    for name, text in reversed(macros):
        if name != macro_name:
            continue

        clause = text[len(name):].strip()
        if clause.startswith("(") and clause.endswith(")"):
            clause = clause[1:-1]

        return [part.strip().split("=")[0].strip() for part in clause.split(",") if part.strip()]

    return None


def _property_name(declaration):
    """The name of a property declaration, ignoring any initialiser."""
    body = " ".join(declaration.split()).strip().rstrip(";")

    if "(" in body.split("=", 1)[0]:
        return None  # a function, not a property

    # Drop the initialiser: everything from the first top-level `=`.
    head = body.split("=", 1)[0]

    # Drop array/map suffixes so `TArray<int32> Scores` gives `Scores`.
    match = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*$', head)
    return match.group(1) if match else None


def _function_name(declaration):
    """The name of a function declaration, or None."""
    open_index = declaration.find("(")
    if open_index == -1:
        return None

    head = declaration[:open_index].strip()
    match = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*$', head)
    return match.group(1) if match else None


def run(source_root=None):
    """Returns (contracts_checked, problems).

    problems is a list of (relative_path, class_name, message).
    """
    root = source_root or SOURCE_ROOT

    if not os.path.isdir(root):
        return 0, []

    headers = []
    sources = []

    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            path = os.path.join(dirpath, name)
            if name.endswith(".h"):
                headers.append(path)
            elif name.endswith(".cpp"):
                sources.append(path)

    source_text = "\n".join(
        strip_comments_and_strings(io.open(p, encoding="utf-8", errors="ignore").read())
        for p in sources)

    header_text = "\n".join(
        strip_comments_and_strings(io.open(p, encoding="utf-8", errors="ignore").read())
        for p in headers)

    module_text = source_text + "\n" + header_text

    # Every property named in any DOREPLIFETIME* call, anywhere in the module.
    replicated_in_lifetime = set(re.findall(
        r'DOREPLIFETIME[A-Z_]*\s*\(\s*[A-Za-z0-9_]+(?:::[A-Za-z0-9_]+)?\s*,\s*([A-Za-z0-9_]+)', module_text))

    # Every function declared with UFUNCTION, anywhere in the module. Used for
    # RepNotify callbacks, which may legitimately live on a base class.
    ufunction_names = set()

    for text in (header_text,):
        for _class_name, body, _is_interface in class_bodies(text):
            for macros, statement in split_statements(body):
                if _specifier_list(macros, "UFUNCTION") is None:
                    continue
                name = _function_name(statement)
                if name:
                    ufunction_names.add(name)

    checked = 0
    problems = []

    for path in sorted(headers):
        clean = strip_comments_and_strings(io.open(path, encoding="utf-8", errors="ignore").read())
        rel = os.path.relpath(path, root)

        for class_name, body, is_interface in class_bodies(clean):
            declared_here = set()

            # A USTRUCT is not a replication unit. Its whole value travels as
            # one property on a UObject, named in that object's
            # GetLifetimeReplicatedProps, so a `Replicated` specifier on a
            # struct member names a path that cannot exist and a DOREPLIFETIME
            # in a struct would not compile. This project names structs F*, the
            # same convention the rest of Unreal uses.
            is_struct = class_name.startswith("F")

            # UI classes are widgets: the tick contract below is one verification
            # each, so the count reflects the rule and not only the classes that
            # manage to break it.
            is_widget = rel.replace("\\", "/").startswith("UI/")

            if is_widget:
                checked += 1

            for macros, statement in split_statements(body):
                text = " ".join(statement.split()).strip()

                if not text or not text.endswith(";"):
                    # Nested types, inline bodies, enum braces: not our business.
                    continue

                function_name = _function_name(text)
                property_name = _property_name(text)

                if function_name is not None:
                    declared_here.add(function_name)

                # --- the widget tick contract ---------------------------------
                # A UI class in this project is a UUserWidget. Widgets are
                # created with EWidgetTickFrequency::Auto, and Auto means "tick
                # only if this widget has a tick event implemented in Blueprint"
                # - a C++ override of NativeTick is not one, so the engine never
                # calls it. The animation in it does not stutter or warn: it
                # never runs. Three widgets had that shape, including the HUD's
                # banner and subtitle timers.
                #
                # Assigning TickFrequency is the deliberate way to ask for a
                # tick and is exempt, because the exemption is the point: this
                # check is about ticking on purpose.
                if is_widget and function_name == "NativeTick":
                    if "TickFrequency" not in body:
                        problems.append((rel, class_name,
                                         u"{0} overrides NativeTick, which the engine never calls on a "
                                         u"C++ widget: TickFrequency starts at Auto, which means "
                                         u"\"tick only if Blueprint implements Tick\". Animate from the "
                                         u"clock in NativePaint, or assign TickFrequency on purpose".format(
                                             class_name)))

                ufunction_specs = _specifier_list(macros, "UFUNCTION")
                uproperty_specs = _specifier_list(macros, "UPROPERTY")

                # ---------------------------------------------------------
                #  UPROPERTY contracts
                # ---------------------------------------------------------
                if uproperty_specs is not None and property_name is not None:
                    specs = uproperty_specs

                    if "Replicated" in specs or any(s.startswith("Replicated") for s in specs):
                        checked += 1

                        if is_struct:
                            problems.append((rel, class_name,
                                             u"'{0}' is marked Replicated inside a struct: a struct "
                                             u"replicates as part of its containing property, so this "
                                             u"specifier does nothing except suggest otherwise".format(
                                                 property_name)))
                        elif property_name not in replicated_in_lifetime:
                            problems.append((rel, class_name,
                                             u"'{0}' is replicated but no DOREPLIFETIME mentions it - "
                                             u"it will never reach a client".format(property_name)))

                    for spec in specs:
                        if not spec.startswith("ReplicatedUsing"):
                            continue

                        checked += 1
                        callback = spec.split("=", 1)[1].strip() if "=" in spec else ""

                        # The specifier list splits on commas, so `ReplicatedUsing
                        # = OnRep_X, BlueprintReadOnly` splits the callback from
                        # the rest; read it back from the raw text instead.
                        match = re.search(r'ReplicatedUsing\s*=\s*([A-Za-z_][A-Za-z0-9_]*)', " ".join(macros[-1][1].split()))
                        callback = match.group(1) if match else callback

                        if not callback:
                            continue

                        if callback not in ufunction_names:
                            problems.append((rel, class_name,
                                             u"ReplicatedUsing = {0}, which is not declared as a "
                                             u"UFUNCTION anywhere in the module".format(callback)))

                # ---------------------------------------------------------
                #  UFUNCTION contracts
                # ---------------------------------------------------------
                if ufunction_specs is None or function_name is None:
                    continue

                checked += 1
                specs = ufunction_specs
                impl_name = function_name + "_Implementation"

                is_rpc = any(s in RPC_SPECIFIERS for s in specs)

                # 1. RPCs cannot return a value.
                if is_rpc:
                    head = text[:text.find("(")].strip()
                    return_type = re.sub(r'\b[A-Za-z0-9_]+\s*$', "", head).strip()

                    if return_type and return_type not in ("void", "virtual void"):
                        problems.append((rel, class_name,
                                         u"RPC {0}() returns '{1}' - Unreal RPCs cannot have a return "
                                         u"value".format(function_name, return_type)))

                # 2. RPCs and BlueprintNativeEvents need an _Implementation.
                needs_impl = is_rpc or any(s in IMPLEMENTATION_SPECIFIERS for s in specs)

                if needs_impl:
                    declared = impl_name in declared_here or re.search(
                        r'\b' + re.escape(impl_name) + r'\s*\(', body) is not None

                    defined = re.search(
                        r'\b' + re.escape(class_name) + r'::' + re.escape(impl_name) + r'\s*\(',
                        source_text) is not None

                    # An interface may give the default an inline body in its own
                    # header, which is the pattern this project uses.
                    inline_in_header = re.search(
                        r'\b' + re.escape(impl_name) + r'\s*\([^;{]*\)[^;{]*\{', body) is not None

                    # Pure virtual: the implementer is forced to provide a body by
                    # the compiler, so there is nothing here to check.
                    pure_virtual = re.search(
                        r'\b' + re.escape(impl_name) + r'\s*\([^;]*\)\s*(?:const\s*)?=\s*0',
                        body) is not None

                    if pure_virtual or defined or inline_in_header:
                        pass
                    elif is_interface:
                        # An interface that declares the _Implementation without a
                        # body leaves UHT generating a call to a symbol with no
                        # definition. It is also where a documented default goes to
                        # die: "Defaults to 220 cm" in a comment and nowhere else.
                        # When an interface declares nothing at all, UHT generates
                        # its own declaration and there is nothing to check here.
                        if declared:
                            problems.append((rel, class_name,
                                             u"interface {0}() declares {1}() with no body: add an "
                                             u"inline default so the documented default is real".format(
                                                 function_name, impl_name)))
                    elif not declared:
                        problems.append((rel, class_name,
                                         u"{0}() has no {1}() declared or defined".format(
                                             function_name, impl_name)))
                    else:
                        problems.append((rel, class_name,
                                         u"{0}() declares {1}() with no body anywhere - UHT will "
                                         u"generate a call to a symbol the linker cannot find".format(
                                             function_name, impl_name)))

                # 3. BlueprintImplementableEvent must have no C++ body.
                if any(s in NO_BODY_SPECIFIERS for s in specs):
                    if re.search(r'\b' + re.escape(class_name) + r'::' + re.escape(function_name) + r'\s*\(',
                                 source_text):
                        problems.append((rel, class_name,
                                         u"BlueprintImplementableEvent {0}() is also defined in C++; "
                                         u"UHT rejects that".format(function_name)))

    return checked, problems


class _UHTReport(object):
    """Report object with the shape run_all expects (summary + ok())."""

    def __init__(self, checked, problems):
        self.checked = checked
        self.problems = list(problems)

    def ok(self):
        return not self.problems

    def summary(self):
        if self.problems:
            first = "; ".join(u"{0}".format(m.split(" - ")[0]) for _r, _c, m in self.problems[:2])
            return u"{0} contracts, {1} broken ({2})".format(self.checked, len(self.problems), first)
        return u"{0} UHT contracts, all satisfied".format(self.checked)


def build_all():
    """Entry point used by run_all.py. Prints the detail, returns a report."""
    checked, problems = run()

    C.log(u"")
    C.log(u"  UHT contracts checked: {0}".format(checked))

    if not problems:
        C.log(u"  every RPC, native event, RepNotify and replicated property resolves. OK")
        return _UHTReport(checked, problems)

    C.log(u"  BROKEN: {0}".format(len(problems)))
    for rel, class_name, message in problems:
        C.log(u"      {0}: {1} - {2}".format(rel, class_name, message))

    return _UHTReport(checked, problems)


def main():
    checked, problems = run()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - UnrealHeaderTool contracts")
    C.log(u"=" * 70)

    if not problems:
        C.log(u"  {0} contracts checked; all satisfied. OK".format(checked))
        return 0

    C.log(u"  {0} contracts checked; {1} BROKEN".format(checked, len(problems)))
    for rel, class_name, message in problems:
        C.log(u"  {0}: {1} - {2}".format(rel, class_name, message))

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
