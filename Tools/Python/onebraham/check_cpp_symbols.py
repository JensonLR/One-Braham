# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.check_cpp_symbols
===========================

Finds C++ member functions that are declared in a class body and defined nowhere
in the module.

WHY THIS EXISTS
    Nothing in this project has been compiled by a machine with an Unreal Engine
    install behind it yet, so "it will link" is a belief rather than a fact. The
    two most common ways a large hand-written C++ module fails to link are also
    the two most mechanical:

      1. A method declared in a header and defined in no .cpp at all. The header
         looks complete, the .cpp looks complete, and the linker is the first
         thing to disagree.
      2. A method whose definition has a different signature from its declaration
         - usually a missing `const`, or a parameter that was a reference in the
         header and a value in the source.

    Both are found by comparing declarations against definitions textually, and
    neither is found by a code review of 107 headers.

    Two more rules were added the day two of each were found by hand in code that
    had been reviewed three times:

      3. A relational or bitwise operator applied to a scoped enum. `enum class`
         has no `operator>`, and both instances of this in the project were inside
         a conditional that read as ordinary code: `AccessTier >=
         EOneBrahamAccessTier::Executive` (the keycard's glow, in
         `OneBrahamKeycard.cpp`) and `Size >= EOneBrahamTextSize::Header` (the
         letter-spacing, in `OneBrahamUIStyle.cpp`). Neither would have compiled.
      4. A project member whose name is also an engine property of one of its
         ancestors. This one *does* compile, and that is the problem:
         `UOneBrahamCharacterMovementComponent::AirControl` shadowed
         `UCharacterMovementComponent::AirControl`, so the constructor's
         `AirControl = 0.14f;` wrote to the project's member, the engine property
         stayed at its default 0.05, and nothing read the member. Air control was
         a fifth of what the code, the tuning comments and Docs/04 all said it
         was, in a file where every number is written twice and only one of them
         is the one the engine uses. The list of engine names below is curated,
         not complete: it is a net for the properties a project like this one is
         likely to reach for, and it says so rather than implying it is a proof.

    Two more, found the same way - by hand, in code that had been reviewed:

      5. A read or write of an engine member that UE5 made private. PlayerId,
         Score and the spectator flags on APlayerState, and bReplicates, bHidden,
         bCanBeDamaged, Role and RemoteRole on AActor, are reached through
         GetPlayerId(), GetScore(), GetIsReplicated(), IsHidden() and friends.
         `State->PlayerId` reads like the obvious thing and does not compile;
         there were six of them. The PlayerState names are only checked on a
         receiver this file declares as a player state, because the project's
         own structs have a PlayerId and a Score too.
      6. A parameter or local that has the name of a member of its own class.
         UE5 builds with shadow-variable warnings as errors, and even where it
         compiled it was a bug: `SetDeafened(bool bDeafened)` assigned the
         parameter to itself and the member never changed, and the HUD's
         `OwningController` local hid the member it was named after.

      8. `IsValid()` on a TSoftObjectPtr or TSoftClassPtr. On a soft pointer
         IsValid() means "the asset is already in memory", not "a path is
         set", so `if (Ptr.IsValid()) Ptr.LoadSynchronous()` never loads from
         a cold start. The body and arms meshes, every weapon montage, the
         weapon meshes, muzzle flash, shells, tracers and every authored weapon
         sound were gated that way. IsNull() is the "is anything set" test.

      7. Audio played from inside a walk over the player controllers. Six
         actors played their sounds by iterating every controller and playing
         through each one's audio manager. On a listen host that is the sound
         once per connected player, on the host's own speakers; on a client,
         when the code was server-only, it was nothing. A sound plays through
         UOneBrahamAudioManagerComponent::GetLocal(), from code that runs on
         every machine (a multicast, an OnRep, a local tick).

HOW IT PARSES
    Not with a regex over the whole file, which is what the first version of this
    tool did and why it reported 177 "missing" functions of which 170 were macro
    invocations and default arguments. It now walks class bodies: find `class X {`,
    find the matching brace, split the body into statements at brace depth zero,
    and keep the statements that are a plain declaration ending in `;` with no
    initialiser and no body. That is the shape of a declaration and of nothing
    else.

WHAT IT DOES NOT DO
    It is not a compiler. It does not resolve overloads, does not understand
    templates and skips anything it is not sure about. A file it cannot parse
    produces noise at worst, and the known-external list below absorbs the
    declarations that UnrealHeaderTool generates.

USAGE
    PYTHONPATH=Tools/Python python3 Tools/Python/onebraham/check_cpp_symbols.py
    (runs without the editor; used by run_all --text-only and by validate_project)
"""

from __future__ import print_function

import io
import os
import re
import sys

from onebraham import common as C

SOURCE_ROOT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "Source", "OneBraham"))

#: Definitions that UnrealHeaderTool generates, or that the engine provides.
#: Anything here is deliberate; anything not here and not defined is a true
#: "declared but never defined".
KNOWN_EXTERNAL = {
    # -- UHT generated ------------------------------------------------------
    "GetLifetimeReplicatedProps",
    "GetReplicatedCustomConditionState",
    "GetSubobjectReplicationList",
    "StaticClass",
    "GetPrivateStaticClass",

    # -- BlueprintNativeEvent: UHT writes the thunk, the project writes
    #    *_Implementation. The _Implementation is what is checked instead.
    "GetInteractionPrompt",
    "GetInteractionTag",
    "CanInteract",
    "RequiresHold",
    "GetHoldDuration",
    "GetInteractionRange",
    "OnInteractionStart",
    "OnInteractionHold",
    "OnInteractionEnd",
    "OnInteractionCancelled",
    "SetInteractionHighlight",
    "ApplyBreachDamage",
    "IsBreached",
    "GetBreachResistance",
    "GetBreachIntegrity01",
    "GetBreachFloor",

    # -- Inline in a base class, redeclared for documentation --------------
    "IsAlive",
    "GetHealth",
    "HasAuthority",
}

MACRO_LINE = re.compile(r'^[A-Z][A-Z0-9_]*\s*[\(;]')

#: Engine properties, by the engine class that declares them. Curated: the names
#: a project writing its own movement, camera, HUD or character code is most
#: likely to reach for. A name missing from this table costs a missed report; a
#: name that is wrong costs a false one, which is why every entry here is a
#: property of the class it is filed under and not a guess.
ENGINE_PROPERTIES = {
    "UCharacterMovementComponent": (
        "MaxWalkSpeed", "MaxWalkSpeedCrouched", "MaxAcceleration", "MaxFlySpeed",
        "MaxSwimSpeed", "MaxCustomMovementSpeed", "BrakingDecelerationWalking",
        "BrakingDecelerationFalling", "BrakingDecelerationSwimming",
        "BrakingDecelerationFlying", "GroundFriction", "AirControl",
        "AirControlBoostMultiplier", "AirControlBoostVelocityThreshold",
        "FallingLateralFriction", "JumpZVelocity", "GravityScale", "MaxStepHeight",
        "CrouchedHalfHeight", "PerchRadiusThreshold", "PerchAdditionalHeight",
        "bCanWalkOffLedges", "bCanWalkOffLedgesWhenCrouching",
        "bOrientRotationToMovement", "bUseControllerDesiredRotation",
        "bIsCrouched", "MaxSimulationTimeStep", "MaxSimulationIterations",
    ),
    "ACharacter": (
        "JumpMaxCount", "JumpMaxHoldTime", "CrouchedEyeHeight", "BaseEyeHeight",
        "bUseControllerRotationPitch", "bUseControllerRotationYaw",
        "bUseControllerRotationRoll", "Mesh", "CharacterMovement", "CapsuleComponent",
    ),
    "APawn": ("AutoPossessPlayer", "AutoPossessAI", "AIControllerClass",
              "PlayerState", "Controller", "InputComponent"),
    "AActor": ("Tags", "PrimaryActorTick", "RootComponent", "bReplicates",
               "bAlwaysRelevant", "bNetLoadOnClient", "NetCullDistanceSquared",
               "NetUpdateFrequency", "MinNetUpdateFrequency", "CustomTimeDilation",
               "bCanBeDamaged", "bHidden", "bBlockInput", "Owner", "Instigator",
               "InputComponent"),
    "UActorComponent": ("PrimaryComponentTick", "bAutoActivate", "bIsActive",
                        "ComponentTags", "bReplicates"),
    "APlayerController": ("Player", "PlayerCameraManager", "MyHUD",
                          "bShowMouseCursor", "bEnableClickEvents",
                          "DefaultMouseCursor", "bAutoManageActiveCameraTarget",
                          "ClickEventKeys", "bEnableTouchEvents"),
    "APlayerState": ("PlayerName", "Score", "Ping", "bIsSpectator",
                     "bOnlySpectator", "bIsABot", "CompressedPing"),
    "AGameStateBase": ("PlayerArray", "ElapsedTime", "ServerWorldTimeSeconds"),
    "UUserWidget": ("ColorAndOpacity", "Visibility", "bIsFocusable",
                    "ForegroundColor", "RootWidget", "WidgetTree"),
}

#: The engine's own inheritance, for the project classes that sit on top of it:
#: a member that shadows `bAutoActivate` shadows it through the whole chain.
ENGINE_PARENT = {
    "UCharacterMovementComponent": "UActorComponent",
    "USkeletalMeshComponent": "USkinnedMeshComponent",
    "UStaticMeshComponent": "UMeshComponent",
    "ACharacter": "APawn",
    "APawn": "AActor",
    "AController": "AActor",
    "APlayerController": "AController",
    "APlayerState": "AInfo",
    "AGameStateBase": "AInfo",
    "AInfo": "AActor",
    "UUserWidget": "UWidget",
}

#: An operator scoped enums do not have. `==` and `!=` are fine and are what the
#: fixes use.
SCOPED_ENUM_OPERATOR = re.compile(r'(?:>=|<=|<<|>>|>|<|\||&|\^)\s*(%s)::')

#: A data member with an initialiser or a bare declaration in a class body.
MEMBER_DECLARATION = re.compile(
    r'\b(?:float|double|int32|uint8|int64|bool|FName|FText|FString|FVector|FRotator|'
    r'FLinearColor|FGameplayTag|TSubclassOf\s*<[^>]*>)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=|;)')

#: A definition in a .cpp: `ReturnType AClass::Function(` at the start of a line.
DEFINITION = re.compile(r'^[A-Za-z_][A-Za-z0-9_:<>,\s\*&]*?\b([A-Za-z_][A-Za-z0-9_]*)::'
                        r'([A-Za-z_][A-Za-z0-9_]*)\s*\(', re.MULTILINE)

#: A declaration: `Type Name(args) tail;` where Name is the identifier directly
#: before the first top-level parenthesis.
DECL_LINE = re.compile(
    r'^(?P<return>(?:[\w:<>,*&\[\]\s]|::)+?)\s*'      # return type
    r'(?<![A-Za-z0-9_])'                              # a whole identifier: a
                                                      # constructor has no return
                                                      # type, and without this the
                                                      # F of FFoo(...) became one
    r'(?P<name>~?[A-Za-z_][A-Za-z0-9_]*)\s*'          # method name
    r'\((?P<params>.*)\)\s*'                          # parameters
    r'(?P<tail>(?:const\b|override\b|final\b|noexcept\b|virtual\b|\s)*)$',
    re.DOTALL)


def strip_comments_and_strings(text):
    """Removes comments and string literals, preserving line count.

    The implementation lives in common.py: it used to be four regexes here, in
    five other checkers, and the order of those regexes could delete real code
    from the analysis. See common.strip_comments_and_strings.
    """
    return C.strip_comments_and_strings(text)


def class_bodies(text):
    """The shared class-body brace matcher, which lives in common.py.

    It used to be a copy of this, with the same `[AUIE]` prefix set that left
    every F struct invisible - so a method declared in a struct was never checked
    for a definition at all. One implementation, in one place.
    """
    return C.class_bodies(text)


def split_statements(body):
    """Top-level brace-depth-zero statements of a class body.

    Statements are separated by `;` at depth zero of ()/[]; a statement that
    opens a brace at depth zero (an inline definition or a nested type) is
    returned as its own chunk so the caller can discard it.
    """
    statements = []
    current = []
    depth_paren = 0
    depth_brace = 0

    index = 0
    while index < len(body):
        char = body[index]

        if char in "([":
            depth_paren += 1
        elif char in ")]":
            depth_paren -= 1
        elif char == "{":
            depth_brace += 1
        elif char == "}":
            depth_brace -= 1

        if char == ";" and depth_paren == 0 and depth_brace == 0:
            current.append(char)
            statements.append("".join(current))
            current = []
        elif char == "}" and depth_brace == 0 and depth_paren == 0:
            current.append(char)
            statements.append("".join(current))
            current = []
        else:
            current.append(char)

        index += 1

    if current:
        statements.append("".join(current))

    return statements


def declarations_in_body(body):
    """Method names declared (not defined) in a class body."""
    names = []

    for statement in split_statements(body):
        text = " ".join(statement.split()).strip()

        if not text.endswith(";"):
            continue

        # Access specifiers, macros, UHT blocks, static_assert and friends.
        if text.endswith(":"):
            continue
        if MACRO_LINE.match(text):
            continue
        if text.startswith("//") or text.startswith("#"):
            continue
        if "{" in text or "}" in text:
            continue

        # Properties and enum values have an initialiser; declarations do not.
        if "=" in text.split("(", 1)[0]:
            continue

        match = DECL_LINE.match(text[:-1].strip())
        if match is None:
            continue

        name = match.group("name")

        # A parameter list containing another call at depth one means the regex
        # latched onto an inner call rather than the declaration itself.
        if "(" in match.group("params") and ")" in match.group("params"):
            continue

        if name in ("if", "for", "while", "switch", "return", "sizeof", "new", "delete",
                    "static_assert", "check", "ensure", "TEXT", "LOCTEXT", "NSLOCTEXT",
                    "FVector", "FVector2D", "FRotator", "FLinearColor", "FTransform",
                    "FName", "FString", "FText", "FGameplayTag", "TArray", "TMap", "TSet"):
            continue

        if name.startswith("DECLARE_") or name.startswith("GENERATED_"):
            continue

        names.append(name)

    return names


def defined_symbols(text):
    """Every `Class::Method` seen in the module's sources."""
    found = set()
    for match in re.finditer(r'([A-Za-z_][A-Za-z0-9_]*)::(~?[A-Za-z_][A-Za-z0-9_]*)\s*\(', text):
        found.add((match.group(1), match.group(2)))
    return found


def run(report=None):
    """Returns (declarations_checked, missing) where missing is a list of tuples."""
    if not os.path.isdir(SOURCE_ROOT):
        if report is not None:
            report.failed("cpp symbols", u"no source root at {0}".format(SOURCE_ROOT))
        return 0, []

    headers = []
    source_files = []

    for dirpath, _dirs, files in os.walk(SOURCE_ROOT):
        for name in files:
            path = os.path.join(dirpath, name)
            if name.endswith(".h"):
                headers.append(path)
            elif name.endswith(".cpp"):
                source_files.append(path)

    all_text = "\n".join(
        strip_comments_and_strings(io.open(p, encoding="utf-8", errors="ignore").read())
        for p in headers + source_files)
    defined = defined_symbols(all_text)

    checked = 0
    missing = []

    for path in sorted(headers):
        raw = io.open(path, encoding="utf-8", errors="ignore").read()
        clean = strip_comments_and_strings(raw)
        rel = os.path.relpath(path, SOURCE_ROOT)

        parsed_any = False

        for class_name, body, is_interface in class_bodies(clean):
            parsed_any = True

            for name in declarations_in_body(body):
                checked += 1

                if name in KNOWN_EXTERNAL:
                    continue

                # Constructors and destructors are declared with the class name.
                if name == class_name or name == "~" + class_name:
                    if (class_name, class_name) in defined or (class_name, "~" + class_name) in defined:
                        continue
                    if class_name.startswith("U") or class_name.startswith("A") or class_name.startswith("F"):
                        # A struct/actor with no explicit constructor is fine: the
                        # compiler generates one.
                        continue

                if (class_name, name) in defined:
                    continue

                # An interface's BlueprintNativeEvent defaults are defined by each
                # implementer; the interface itself legitimately has no definition.
                if is_interface or class_name in ("IOneBrahamInteractionInterface",
                                                 "IOneBrahamBreachableInterface"):
                    continue

                missing.append((rel, class_name, name))

        if not parsed_any:
            continue

    return checked, missing


def source_files():
    """Every .h and .cpp under Source/OneBraham, as (rel, path)."""
    out = []

    for dirpath, _dirs, files in os.walk(SOURCE_ROOT):
        for name in sorted(files):
            if name.endswith(".h") or name.endswith(".cpp"):
                path = os.path.join(dirpath, name)
                out.append((os.path.relpath(path, SOURCE_ROOT), path))

    return out


def scoped_enums(files):
    """The project's `enum class` names, and the file each is declared in."""
    out = {}

    for rel, path in files:
        if not rel.endswith(".h"):
            continue

        for match in re.finditer(r'enum\s+class\s+([A-Za-z_][A-Za-z0-9_]*)',
                                 strip_comments_and_strings(io.open(path, encoding="utf-8",
                                                                    errors="ignore").read())):
            out.setdefault(match.group(1), rel)

    return out


def class_bases(files):
    """class name -> (base names, body, rel path), for every class with a body."""
    out = {}

    for rel, path in files:
        if not rel.endswith(".h"):
            continue

        text = strip_comments_and_strings(io.open(path, encoding="utf-8", errors="ignore").read())

        for match in re.finditer(
                r'\bclass\s+(?:[A-Za-z0-9_]+_API\s+)?([A-Za-z0-9_]+)\s*'
                r'(?::\s*public\s+([^{;]+?))?\s*\{', text):
            name, bases = match.group(1), match.group(2) or ""
            body = _braced_body(text, match.end() - 1)
            out.setdefault(name, (bases, body, rel))

    return out


def _braced_body(text, start):
    depth = 0

    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:i]

    return text[start + 1:]


def engine_ancestors(class_name, classes, seen=None):
    """The engine classes this project class sits on, transitively."""
    seen = seen or set()

    if class_name in seen:
        return set()

    seen.add(class_name)

    if class_name in ENGINE_PROPERTIES:
        # A class in both tables is an engine class whose own ancestors also carry
        # properties: UCharacterMovementComponent is a UActorComponent, so a member
        # that shadows `bAutoActivate` shadows it through the movement component.
        out = {class_name}

        if class_name in ENGINE_PARENT:
            out |= engine_ancestors(ENGINE_PARENT[class_name], classes, seen)

        return out

    if class_name in ENGINE_PARENT:
        return engine_ancestors(ENGINE_PARENT[class_name], classes, seen)

    if class_name not in classes:
        return set()

    bases, _body, _rel = classes[class_name]
    out = set()

    for base in re.findall(r'[AUIF][A-Za-z0-9_]+', bases):
        out |= engine_ancestors(base, classes, seen)

    return out


#: Engine members that are private in UE5, and the accessor to use instead.
ENGINE_PRIVATE_PLAYER_STATE = {
    "PlayerId": "GetPlayerId()", "Score": "GetScore()/SetScore()",
    "bIsSpectator": "IsSpectator()", "bOnlySpectator": "IsOnlyASpectator()",
    "bIsABot": "IsABot()", "bIsInactive": "IsInactive()",
    "ExactPing": "GetPingInMilliseconds()", "CompressedPing": "GetCompressedPing()",
    "PlayerNamePrivate": "GetPlayerName()",
}
ENGINE_PRIVATE_ACTOR = {
    "bReplicates": "GetIsReplicated()/SetReplicates()", "bHidden": "IsHidden()",
    "bCanBeDamaged": "CanBeDamaged()/SetCanBeDamaged()",
    "RemoteRole": "GetRemoteRole()", "Role": "GetLocalRole()",
}

#: `AOneBrahamPlayerState* OtherState`, `const APlayerState* PS`.
PLAYER_STATE_VARIABLE = re.compile(r'\b[A-Z][A-Za-z0-9_]*PlayerState\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)')

#: A local declared at the start of a statement, or in an `if (Type* X = ...)`.
LOCAL_DECLARATION = re.compile(
    r'(?:^|[;{(]|\bif\s*\()\s*(?:const\s+)?'
    r'(?:auto|bool|float|double|int32|uint8|uint32|int64|[AUFTE][A-Z][A-Za-z0-9_]*(?:\s*<[^;()=]*>)?)'
    r'\s*[*&]?\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=|;|\{)', re.MULTILINE)


def data_members(body):
    """Names of the data members declared at the top level of a class body."""
    names = set()

    for statement in split_statements(body):
        text = " ".join(statement.split())

        if not text.endswith(";") or text.startswith(("using ", "friend ", "typedef ", "enum ")):
            continue

        # Drop UPROPERTY(...) and similar macro prefixes, then anything that is a
        # function declaration: a '(' before any '=' is a parameter list.
        text = re.sub(r'^(?:[A-Z_][A-Z0-9_]*\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*)+', "", text)
        head = text.split("=", 1)[0]

        if "(" in head:
            continue

        match = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*\d+\s*)?(?:\[[^\]]*\]\s*)?$', head.rstrip(";").strip())
        if match and " " in head.strip():
            names.add(match.group(1))

    return names


def run_hygiene():
    """(checks, problems) for the rules that are not about linking.

    problems are (rel, line_number, message).
    """
    files = source_files()
    classes = class_bases(files)
    enums = scoped_enums(files)
    checks = 0
    problems = []

    if not classes:
        problems.append((u"Source/OneBraham", 0, u"no class bodies parsed"))
        return checks, problems

    # --- a scoped enum compared with an operator it does not have ---------
    pattern = re.compile(SCOPED_ENUM_OPERATOR.pattern % u"|".join(
        re.escape(name) for name in sorted(enums))) if enums else None

    for rel, path in files:
        text = strip_comments_and_strings(io.open(path, encoding="utf-8", errors="ignore").read())
        checks += 1

        if pattern is not None:
            for match in pattern.finditer(text):
                line = text[:match.start()].count("\n") + 1
                problems.append((
                    rel, line,
                    u"{0} is a scoped enum and has no relational or bitwise operator: "
                    u"'{1} {2}::' cannot compile - compare against the values it can be"
                    .format(match.group(1), match.group(0)[:2].strip(), match.group(1))))

    # --- a member that shadows an engine property -------------------------
    for class_name in sorted(classes):
        ancestors = engine_ancestors(class_name, classes)
        if not ancestors:
            continue

        engine_names = set()
        for ancestor in ancestors:
            engine_names |= set(ENGINE_PROPERTIES[ancestor])

        if not engine_names:
            continue

        _bases, body, rel = classes[class_name]

        # Only data members: a function of the same name is an override, which is
        # how the engine is meant to be extended.
        body_no_calls = re.sub(r'[A-Za-z_][A-Za-z0-9_:<>,\*&\s]*\([^)]*\)[^;{]*[;{]',
                               u"", body)

        for match in MEMBER_DECLARATION.finditer(body_no_calls):
            checks += 1
            member = match.group(1)

            if member in engine_names:
                problems.append((
                    rel, 0,
                    u"{0}::{1} shadows the engine property {1} on {2}: the constructor "
                    u"and the tuning UI write the project's member and the engine never "
                    u"sees the value".format(class_name, member,
                                             u"/".join(sorted(ancestors)))))

    # --- engine members UE5 made private ----------------------------------
    for rel, path in files:
        if not rel.endswith(".cpp"):
            continue

        text = strip_comments_and_strings(io.open(path, encoding="utf-8", errors="ignore").read())
        receivers = set(PLAYER_STATE_VARIABLE.findall(text))
        checks += 1

        receiver_pattern = r'(?:\b(?:%s)|GetPlayerState\s*<[^>]*>\s*\(\s*\))' % u"|".join(
            re.escape(name) for name in sorted(receivers)) if receivers else r'GetPlayerState\s*<[^>]*>\s*\(\s*\)'

        for match in re.finditer(receiver_pattern + r'\s*->\s*(%s)\b(?!\s*\()' % u"|".join(ENGINE_PRIVATE_PLAYER_STATE), text):
            field = match.group(1)
            problems.append((
                rel, text[:match.start()].count("\n") + 1,
                u"APlayerState::{0} is private in UE5 - use {1}".format(field, ENGINE_PRIVATE_PLAYER_STATE[field])))

        for match in re.finditer(r'->\s*(%s)\b(?!\s*\()' % u"|".join(ENGINE_PRIVATE_ACTOR), text):
            field = match.group(1)
            problems.append((
                rel, text[:match.start()].count("\n") + 1,
                u"AActor::{0} is private in UE5 - use {1}".format(field, ENGINE_PRIVATE_ACTOR[field])))

    # --- a parameter or local named like a member of its own class --------
    members = {}

    def _members_of(class_name, seen=None):
        seen = seen or set()
        if class_name in seen or class_name not in classes:
            return set()
        seen.add(class_name)
        if class_name not in members:
            bases, body, _rel = classes[class_name]
            own = data_members(body)
            for base in re.findall(r'[AUIF][A-Za-z0-9_]+', bases):
                own |= _members_of(base, seen)
            members[class_name] = own
        return members[class_name]

    for rel, path in files:
        if not rel.endswith(".cpp"):
            continue

        text = strip_comments_and_strings(io.open(path, encoding="utf-8", errors="ignore").read())

        for match in DEFINITION.finditer(text):
            class_name = match.group(1)
            if class_name not in classes:
                continue

            own = _members_of(class_name)
            if not own:
                continue

            close = text.find(")", match.end())
            brace = text.find("{", close)
            semicolon = text.find(";", close)
            if close == -1 or brace == -1 or (semicolon != -1 and semicolon < brace):
                continue

            checks += 1
            params = text[match.end():close]
            line = text[:match.start()].count("\n") + 1
            found = []

            for param in params.split(","):
                name = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:=[^,]*)?$', param.strip())
                if name and name.group(1) in own and len(param.split()) > 1:
                    found.append((u"parameter", name.group(1)))

            body = _braced_body(text, brace)
            for local in LOCAL_DECLARATION.finditer(body):
                if local.group(1) in own:
                    found.append((u"local", local.group(1)))

            for kind, name in found:
                problems.append((
                    rel, line,
                    u"{0}::{1} has a {2} called {3}, which is a member of {0}: an error "
                    u"under UE5's shadow-variable level, and the member is not the one "
                    u"being written".format(class_name, match.group(2), kind, name)))

    # --- IsValid() on a soft pointer ----------------------------------------
    soft_names = set()
    for rel, path in files:
        text = strip_comments_and_strings(io.open(path, encoding="utf-8", errors="ignore").read())
        soft_names.update(re.findall(
            r'TSoft(?:Object|Class)Ptr\s*<[^;()]*?>\s*&?\s*([A-Za-z_][A-Za-z0-9_]*)\s*[;=,)]', text))

    if soft_names:
        soft_pattern = re.compile(r'(?<![A-Za-z0-9_])(%s)\s*\.\s*IsValid\s*\(\s*\)' % u"|".join(
            re.escape(name) for name in sorted(soft_names)))

        for rel, path in files:
            if not rel.endswith(".cpp"):
                continue

            text = strip_comments_and_strings(io.open(path, encoding="utf-8", errors="ignore").read())
            checks += 1

            for match in soft_pattern.finditer(text):
                problems.append((
                    rel, text[:match.start()].count("\n") + 1,
                    u"{0}.IsValid() on a soft pointer is true only when the asset is already "
                    u"loaded - use !{0}.IsNull() to ask whether one is set".format(match.group(1))))

    # --- audio played once per player controller --------------------------
    for rel, path in files:
        if not rel.endswith(".cpp"):
            continue

        text = strip_comments_and_strings(io.open(path, encoding="utf-8", errors="ignore").read())

        for match in re.finditer(r'\bfor\s*\([^;{]*GetPlayerControllerIterator\s*\(\s*\)[^{]*\{', text):
            checks += 1
            body = _braced_body(text, match.end() - 1)

            if re.search(r'GetAudioManager\s*\(|GetAudioFor\s*\(|PlaySound(?:AtLocation|2D|Attached)\s*\(', body):
                problems.append((
                    rel, text[:match.start()].count("\n") + 1,
                    u"plays audio inside a loop over the player controllers: once per player "
                    u"on a listen host, and never on a client if this runs on the server - "
                    u"play through UOneBrahamAudioManagerComponent::GetLocal() from code that "
                    u"runs on every machine"))

    # --- the same function defined twice ---------------------------------
    seen = {}

    for rel, path in files:
        if not rel.endswith(".cpp"):
            continue

        text = io.open(path, encoding="utf-8", errors="ignore").read()

        for match in DEFINITION.finditer(text):
            key = (match.group(1), match.group(2))
            line = text[:match.start()].count("\n") + 1
            seen.setdefault(key, []).append((rel, line))

    for (class_name, function), places in sorted(seen.items()):
        if len(places) > 1:
            where = u", ".join(u"{0}:{1}".format(rel, line) for rel, line in places)
            problems.append((
                places[0][0], places[0][1],
                u"{0}::{1} is defined {2} times ({3}): a redefinition, or a second "
                u"copy of the file's logic in another file".format(
                    class_name, function, len(places), where)))

    return checks, problems


class _SymbolReport(object):
    """Report object with the shape run_all expects (summary + ok())."""

    def __init__(self, checked, missing, hygiene=None, hygiene_checks=0):
        self.checked = checked
        self.missing = list(missing)
        self.hygiene = list(hygiene or [])
        self.hygiene_checks = hygiene_checks

    def ok(self):
        return not self.missing and not self.hygiene

    def summary(self):
        if self.missing:
            first = ", ".join(sorted(set("{0}::{1}".format(c, n) for _f, c, n in self.missing))[:3])
            return u"{0} declared, {1} undefined ({2})".format(
                self.checked, len(self.missing), first)

        if self.hygiene:
            return u"{0} declarations defined, {1} hygiene problems".format(
                self.checked, len(self.hygiene))

        # The hygiene check count is deliberately absent: it is the number of
        # statements looked at, it moves whenever anybody adds a member, and a
        # number in a document that changes on every edit is a number that will
        # be wrong. The count is logged by build_all() for the record; the
        # summary says only what a reader needs, which is that it passed.
        return u"{0} declarations, all defined; compile hygiene clean".format(self.checked)


def build_all():
    """Entry point used by run_all.py. Prints the detail, returns a report."""
    checked, missing = run()
    hygiene_checks, hygiene = run_hygiene()

    C.log(u"")
    C.log(u"  declarations checked: {0}".format(checked))
    C.log(u"  compile-hygiene checks: {0}".format(hygiene_checks))

    if missing:
        C.log(u"  MISSING: {0} - each one is a link error".format(len(missing)))
        for rel, cls, name in missing:
            C.log(u"      {0}: {1}::{2}".format(rel, cls, name))
    else:
        C.log(u"  every declared member function has a definition. OK")

    if hygiene:
        C.log(u"  HYGIENE: {0} - each one is a compile error or a silent no-op".format(
            len(hygiene)))
        for rel, line, message in hygiene:
            C.log(u"      {0}:{1} {2}".format(rel, line, message))
    else:
        C.log(u"  no scoped enum is compared with an operator it does not have, no "
              u"member shadows an engine property, no engine-private member is read, no "
              u"parameter or local hides a member, no sound is played once per player, "
              u"no soft pointer is asked IsValid(), no function is defined twice. OK")

    return _SymbolReport(checked, missing, hygiene, hygiene_checks)


def main():
    checked, missing = run()
    hygiene_checks, hygiene = run_hygiene()

    C.log(u"=" * 70)
    C.log(u"ONE BRAHAM - declared-but-undefined member functions, and hygiene")
    C.log(u"=" * 70)
    C.log(u"  declarations checked: {0}".format(checked))
    C.log(u"  compile-hygiene checks: {0}".format(hygiene_checks))

    if missing:
        C.log(u"  MISSING: {0} - each one is a link error".format(len(missing)))
    else:
        C.log(u"  every declared member function has a definition. OK")

    if not hygiene and not missing:
        C.log(u"  hygiene: scoped enums, shadowed engine properties, engine-private "
              u"members, shadowed members, per-controller audio, soft-pointer IsValid, "
              u"duplicate definitions. OK")
        return 0

    for rel, line, message in hygiene:
        C.log(u"  {0}:{1} {2}".format(rel, line, message))

    if not missing:
        return 0 if not hygiene else 1

    C.log(u"  MISSING: {0} - each one is a link error".format(len(missing)))
    C.log(u"")

    by_file = {}
    for rel, cls, name in missing:
        by_file.setdefault(rel, []).append(u"{0}::{1}".format(cls, name))

    for rel in sorted(by_file):
        C.log(u"  {0}".format(rel))
        for symbol in sorted(set(by_file[rel])):
            C.log(u"      {0}".format(symbol))

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
