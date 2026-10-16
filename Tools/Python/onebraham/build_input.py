# Copyright (c) One Braham. All Rights Reserved.
"""
onebraham.build_input
=====================

Builds the Enhanced Input assets and the UOneBrahamInputConfig that the player
controller reads.

THE CONTRACT, AND WHY IT IS THIS STRICT
    `AOneBrahamPlayerController::SetupInputComponent` binds each action through
    `UOneBrahamInputConfig` fields - InputConfig->Move, InputConfig->Fire,
    InputConfig->PushToTalk, and so on - and calls ValidateAllActionsSet(), which
    reports every field left null. So the *field names in the C++ class are the
    spec*, and this script's table is the implementation of it:

        Move Look LookGamepad Jump Sprint Walk Crouch Lean Mantle Slide
        Fire Aim Reload FireModeSwitch Melee Inspect
        WeaponSlotPrimary WeaponSlotSidearm WeaponSlotMelee
        WeaponCycleNext WeaponCyclePrev ThrowGrenade UseUtility UseMedkit DropWeapon
        Interact InteractHold ToggleFlashlight ToggleLaser
        PushToTalk VoiceChannelCycle VoiceMuteSelf
        Scoreboard PauseMenu Ping SpectateNextPlayer SpectatePrevPlayer
        SpectateToggleFreeCam ClipMarker

    Every IA_ asset below is named IA_<FieldName> for exactly that reason, and
    DA_InputConfig has each field pointed at it, so a fresh clone binds input
    with no manual assignment.

DESIGN RULES
  * One action per *intent*, never per key.
  * Hip and ADS share one look curve; the ADS multiplier is a setting.
  * Comfort stock: hold-or-toggle sprint, hold-or-toggle crouch, hold-or-toggle
    aim, because a shipped PC shooter that forces one scheme loses players over
    a control preference, not over a design decision.
  * Interact and InteractHold are separate actions: a door is a tap, a terminal
    is a hold, and the player's hand should not have to guess which.

USAGE
    UnrealEditor-Cmd.exe OneBraham.uproject -run=pythonscript
        -script="Tools/Python/onebraham/build_input.py"
"""

from __future__ import print_function

from onebraham import common as C

if C.IN_EDITOR:
    import unreal

# value_type, name, description, KBM default keys, gamepad default keys
#
# Keyboard and mouse is the lead platform (Steam), laid out the way PC
# shooters are: Q/E lean, so interact is F and the utility H; Alt walks, so
# push-to-talk is Caps Lock. On a pad, X is use-and-reload as in most console
# shooters, B crouches (and slides from a sprint), Y swaps weapon, how far the
# stick is pushed is the walk, and voice defaults to voice activation, so a
# pad has no push-to-talk button to share with sprint.
# Managed by default: everything. Unbound by default: ClipMarker (the player
# picks a key in Settings, because a clip key has no universal default).
ACTIONS = [
    # --- Movement ---------------------------------------------------------
    ("Axis2D", "IA_Move", "Walk, jog, sprint, backpedal",
     ["W", "S", "A", "D"], ["Gamepad_Left2D"]),
    # Mouse and stick look are two actions, not one with two keys: the mouse
    # reports how far it moved this frame and the stick how far it is held over,
    # so only the stick's turn is scaled by the frame time. On one action the pad
    # turned faster at a higher frame rate.
    ("Axis2D", "IA_Look", "Mouse look (sensitivity from settings)",
     ["Mouse2D"], []),
    ("Axis2D", "IA_LookGamepad", "Stick look (pad sensitivity and invert from settings)",
     [], ["Gamepad_Right2D"]),
    ("Boolean", "IA_Jump", "Jump / vault / mantle upward",
     ["SpaceBar"], ["Gamepad_FaceButton_Bottom"]),
    ("Boolean", "IA_Sprint", "Sprint while held, or toggle",
     ["LeftShift"], ["Gamepad_LeftThumbstick"]),
    ("Boolean", "IA_Walk", "Slow walk: quiet, and your aim recovers faster",
     ["LeftAlt"], []),
    ("Boolean", "IA_Crouch", "Crouch, stay crouched",
     ["LeftControl", "C"], ["Gamepad_FaceButton_Right"]),
    ("Axis1D", "IA_Lean", "Peek around cover without exposing your whole body",
     ["Q", "E"], []),
    ("Boolean", "IA_Mantle", "Climb: desks, counters, planters, the ledge on 12",
     ["SpaceBar"], ["Gamepad_FaceButton_Bottom"]),
    ("Boolean", "IA_Slide", "Slide. Only from a sprint, and it costs you your aim",
     ["LeftControl"], ["Gamepad_FaceButton_Right"]),

    # --- Combat -----------------------------------------------------------
    ("Boolean", "IA_Fire", "Fire", ["LeftMouseButton"], ["Gamepad_RightTrigger"]),
    ("Boolean", "IA_Aim", "Aim down sights, hold or toggle",
     ["RightMouseButton"], ["Gamepad_LeftTrigger"]),
    ("Boolean", "IA_Reload", "Reload", ["R"], ["Gamepad_FaceButton_Left"]),
    ("Boolean", "IA_FireModeSwitch", "Cycle safe / single / burst / auto",
     ["B"], ["Gamepad_DPad_Up"]),
    ("Boolean", "IA_Melee", "Weapon bash. Opens doors, embarrasses people",
     ["V"], ["Gamepad_RightThumbstick"]),
    ("Boolean", "IA_Inspect", "Inspect the weapon. Cosmetic, no gameplay effect",
     ["I"], []),

    # --- Inventory --------------------------------------------------------
    ("Boolean", "IA_WeaponSlotPrimary", "Primary weapon", ["One"], []),
    ("Boolean", "IA_WeaponSlotSidearm", "Sidearm", ["Two"], []),
    ("Boolean", "IA_WeaponSlotMelee", "Melee tool", ["Three"], ["Gamepad_DPad_Down"]),
    ("Boolean", "IA_WeaponCycleNext", "Next weapon", ["MouseScrollDown"], ["Gamepad_FaceButton_Top"]),
    ("Boolean", "IA_WeaponCyclePrev", "Previous weapon", ["MouseScrollUp"], []),
    ("Boolean", "IA_ThrowGrenade", "Throw the selected utility", ["G"], ["Gamepad_LeftShoulder"]),
    ("Boolean", "IA_UseUtility", "Place or use the selected utility (breach charge)",
     ["H"], ["Gamepad_RightShoulder"]),
    ("Boolean", "IA_UseMedkit", "Medkit, or a meal deal if you have one",
     ["Four"], ["Gamepad_DPad_Left"]),
    ("Boolean", "IA_DropWeapon", "Drop the current weapon for a teammate",
     ["BackSpace"], []),

    # --- Interaction ------------------------------------------------------
    ("Boolean", "IA_Interact", "Tap: doors, pickups, revives", ["F"], ["Gamepad_FaceButton_Left"]),
    ("Boolean", "IA_InteractHold", "Hold: terminals, overrides, finishers",
     ["F"], ["Gamepad_FaceButton_Left"]),
    ("Boolean", "IA_ToggleFlashlight", "Weapon light", ["T"], []),
    ("Boolean", "IA_ToggleLaser", "Laser sight", ["Y"], []),

    # --- Voice ------------------------------------------------------------
    ("Boolean", "IA_PushToTalk", "Push to talk, on the current channel",
     ["CapsLock"], []),
    ("Boolean", "IA_VoiceChannelCycle", "Proximity / squad / team",
     ["Z"], ["Gamepad_DPad_Right"]),
    ("Boolean", "IA_VoiceMuteSelf", "Mute yourself", ["N"], []),

    # --- Interface --------------------------------------------------------
    ("Boolean", "IA_Scoreboard", "Hold for the scoreboard", ["Tab"], ["Gamepad_Special_Left"]),
    ("Boolean", "IA_PauseMenu", "Pause / system menu", ["Escape"], ["Gamepad_Special_Right"]),
    ("Boolean", "IA_Ping", "World ping: enemy, item, move here",
     ["MiddleMouseButton"], []),
    ("Boolean", "IA_SpectateNextPlayer", "Next squadmate while downed or spectating",
     ["LeftMouseButton"], ["Gamepad_RightTrigger"]),
    ("Boolean", "IA_SpectatePrevPlayer", "Previous squadmate",
     ["RightMouseButton"], ["Gamepad_LeftTrigger"]),
    ("Boolean", "IA_SpectateToggleFreeCam", "Free camera while spectating",
     ["SpaceBar"], []),
    ("Boolean", "IA_ClipMarker", "Mark the last 30 seconds for capture. No default: "
                                 "the player chooses the key.", [], []),

    # --- Optional extras the HUD supports, unbound by default -------------
    ("Boolean", "IA_Map", "Floor plan overlay", ["M"], []),
    ("Boolean", "IA_VoiceLineMenu", "Quick voice line wheel", ["X"], []),
    ("Boolean", "IA_ToggleHUD", "Hide the HUD (screenshots, immersion)", ["F1"], []),
]

# Per-key modifiers, as (action, key) -> modifier names. IA_Move's value is
# (right, forward): W and S drive the second axis, so they are swizzled. Look
# takes the raw mouse delta (the controller applies sensitivity and invert);
# the sticks, Move's and LookGamepad's, get a dead zone.
KEY_MODIFIERS = {
    ("IA_Move", "W"): ("SwizzleYXZ",),
    ("IA_Move", "S"): ("SwizzleYXZ", "Negate"),
    ("IA_Move", "A"): ("Negate",),
    ("IA_Move", "Gamepad_Left2D"): ("DeadZone",),
    ("IA_LookGamepad", "Gamepad_Right2D"): ("DeadZone",),
    ("IA_Lean", "Q"): ("Negate",),
}

# The two contexts the controller installs (Project Settings -> One Braham:
# CommonMappingContext at priority 0, DefaultMappingContext at 1). Menus and
# the scoreboard go in UI, so they keep working when gameplay input is off;
# everything else, keyboard and pad alike, goes in Default.
UI_ACTIONS = ("IA_Scoreboard", "IA_PauseMenu", "IA_Map", "IA_ToggleHUD")
RUNTIME_CONTEXT_DEFAULT = "IMC_OneBraham_Default"
RUNTIME_CONTEXT_UI = "IMC_OneBraham_UI"

MAPPING_CONTEXTS = [
    ("IMC_OneBraham_Default", "Keyboard and mouse. The lead platform."),
    ("IMC_OneBraham_Gamepad", "Controller, with pad dead zones and turn rates."),
    ("IMC_OneBraham_Voice", "Voice, radio and ping, so voice can be rebound "
                            "without touching combat."),
    ("IMC_OneBraham_UI", "Menus, scoreboard, map. Applied above gameplay contexts."),
]

VALUE_TYPE_ENUM = {
    "Boolean": "BOOLEAN",
    "Axis1D": "AXIS1D",
    "Axis2D": "AXIS2D",
    "Axis3D": "AXIS3D",
}

# The C++ field name is the action name without the IA_ prefix.
INPUT_CONFIG_FIELD_ORDER = [
    "Move", "Look", "LookGamepad", "Jump", "Sprint", "Walk", "Crouch", "Lean", "Mantle", "Slide",
    "Fire", "Aim", "Reload", "FireModeSwitch", "Melee", "Inspect",
    "WeaponSlotPrimary", "WeaponSlotSidearm", "WeaponSlotMelee",
    "WeaponCycleNext", "WeaponCyclePrev", "ThrowGrenade", "UseUtility", "UseMedkit",
    "DropWeapon",
    "Interact", "InteractHold", "ToggleFlashlight", "ToggleLaser",
    "PushToTalk", "VoiceChannelCycle", "VoiceMuteSelf",
    "Scoreboard", "PauseMenu", "Ping", "SpectateNextPlayer", "SpectatePrevPlayer",
    "SpectateToggleFreeCam", "ClipMarker",
    "ToggleHUD", "Map", "VoiceLineMenu",
]


def _ensure_dirs():
    if not unreal.EditorAssetLibrary.does_directory_exist(C.INPUT):
        unreal.EditorAssetLibrary.make_directory(C.INPUT)
    if not unreal.EditorAssetLibrary.does_directory_exist(C.DATA):
        unreal.EditorAssetLibrary.make_directory(C.DATA)


def build_actions(report):
    """One Input Action per intent, named after the field that will bind it."""
    actions = {}

    for value_type, name, description, kbm_keys, pad_keys in ACTIONS:
        path = u"{0}/{1}".format(C.INPUT, name)

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            actions[name] = unreal.EditorAssetLibrary.load_asset(path)
            report.updated_asset(path)
            continue

        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.InputAction)
        action = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, C.INPUT, unreal.InputAction, factory)

        if action is None:
            report.failed(name, "could not create input action")
            continue

        try:
            action.set_editor_property(
                "value_type", getattr(unreal.InputActionValueType, VALUE_TYPE_ENUM[value_type]))

            # "Consume input" on a gameplay action means a UI widget cannot steal
            # the press. Menus apply IMC_OneBraham_UI above the gameplay contexts
            # instead, which is the supported way to do this.
            action.set_editor_property("consume_input", True)
            action.set_editor_property("trigger_when_paused", False)
        except Exception as exc:  # noqa: BLE001
            report.failed(name, u"could not configure: {0}".format(exc))

        unreal.EditorAssetLibrary.save_asset(path)
        report.created_asset(path)
        actions[name] = action

        C.log(u"  {0:<26} {1:<8} {2}{3}".format(
            name, value_type,
            " + ".join(kbm_keys) if kbm_keys else "(no default key) ",
            " / " + " + ".join(pad_keys) if pad_keys else ""))

    return actions


def build_contexts(report, actions):
    """Creates the four mapping contexts.

    The default keys are authored into the two contexts the controller
    installs, from the ACTIONS table: an empty context is a game nobody can
    move in, which is what shipped until the first packaged build said so.
    Config/DefaultInput.ini's [OneBraham.Input] section is generated from the
    same table (generate_input_ini.py) for the settings page and the docs. The
    pad and voice contexts stay as named assets with no keys of their own:
    their keys live in Default, so one context holds the whole layout.
    """
    contexts = {}

    for name, description in MAPPING_CONTEXTS:
        path = u"{0}/{1}".format(C.INPUT, name)

        if unreal.EditorAssetLibrary.does_asset_exist(path):
            contexts[name] = unreal.EditorAssetLibrary.load_asset(path)
            report.updated_asset(path)
        else:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property("data_asset_class", unreal.InputMappingContext)
            context = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                name, C.INPUT, unreal.InputMappingContext, factory)

            if context is None:
                report.failed(name, "could not create mapping context")
                continue

            unreal.EditorAssetLibrary.save_asset(path)
            report.created_asset(path)
            contexts[name] = context

        C.log(u"  {0}: {1}".format(name, description))

    for name in (RUNTIME_CONTEXT_DEFAULT, RUNTIME_CONTEXT_UI):
        if name in contexts:
            _author_mappings(report, contexts[name], actions, ui=(name == RUNTIME_CONTEXT_UI))
            unreal.EditorAssetLibrary.save_asset(u"{0}/{1}".format(C.INPUT, name), only_if_is_dirty=False)

    return contexts


def _key(name):
    key = unreal.Key()
    key.set_editor_property("key_name", name)
    return key


def _modifier(name, outer):
    if name == "Negate":
        return unreal.new_object(unreal.InputModifierNegate, outer=outer)
    if name == "SwizzleYXZ":
        swizzle = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=outer)
        swizzle.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
        return swizzle
    if name == "DeadZone":
        return unreal.new_object(unreal.InputModifierDeadZone, outer=outer)
    raise ValueError(u"unknown input modifier '{0}'".format(name))


def _author_mappings(report, context, actions, ui):
    """Replaces the context's mappings with the table's default keys.

    Rebuilt from scratch each run, so a key removed from the table is removed
    from the game. Unknown key names fail the build rather than bind nothing.
    """
    mappings = []
    for _value_type, name, _description, kbm_keys, pad_keys in ACTIONS:
        if (name in UI_ACTIONS) != ui:
            continue
        action = actions.get(name)
        if action is None:
            continue
        for key_name in list(kbm_keys) + list(pad_keys):
            key = _key(key_name)
            if not unreal.InputLibrary.key_is_valid(key):
                report.failed(name, u"'{0}' is not a key this engine knows".format(key_name))
                continue
            mapping = unreal.EnhancedActionKeyMapping()
            mapping.set_editor_property("action", action)
            mapping.set_editor_property("key", key)
            modifiers = [_modifier(m, context) for m in KEY_MODIFIERS.get((name, key_name), ())]
            if modifiers:
                mapping.set_editor_property("modifiers", modifiers)
            mappings.append(mapping)

    data = context.get_editor_property("default_key_mappings")
    data.set_editor_property("mappings", mappings)
    context.set_editor_property("default_key_mappings", data)
    C.log(u"  {0}: {1} key mappings".format(context.get_name(), len(mappings)))


def build_input_config(report, actions):
    """Creates DA_InputConfig and points every field at its IA_ asset.

    This is the asset that stops the controller logging "no input will be bound"
    on the very first run, so it is verified field by field at the end.
    """
    config_class = getattr(unreal, "OneBrahamInputConfig", None)
    path = u"{0}/DA_InputConfig".format(C.DATA)

    if config_class is None:
        report.skipped_asset(path,
                             "UOneBrahamInputConfig is not reflected yet - compile the "
                             "OneBraham module, then re-run this script")
        return None

    if not unreal.EditorAssetLibrary.does_directory_exist(C.DATA):
        unreal.EditorAssetLibrary.make_directory(C.DATA)

    if unreal.EditorAssetLibrary.does_asset_exist(path):
        config = unreal.EditorAssetLibrary.load_asset(path)
        report.updated_asset(path)
    else:
        config = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_InputConfig", C.DATA, config_class, unreal.DataAssetFactory())

        if config is None:
            report.failed("DA_InputConfig", "could not create the input config")
            return None

        report.created_asset(path)

    assigned = 0
    missing = []

    for field in INPUT_CONFIG_FIELD_ORDER:
        action = actions.get("IA_" + field)

        if action is None:
            missing.append(field)
            continue

        try:
            # set_editor_property takes the C++ property name as-is; deriving the
            # Python name by hand breaks on acronyms (ToggleHUD -> toggle_hud).
            config.set_editor_property(field, action)
            assigned += 1
        except Exception as exc:  # noqa: BLE001
            report.failed(field, u"could not assign: {0}".format(exc))
            missing.append(field)

    # Every generated action has a field and every field has an action. There is
    # no such thing as an action that is "bound elsewhere": a generated asset with
    # no field pointing at it is a key that does nothing, which is what IA_Map,
    # IA_ToggleHUD and IA_VoiceLineMenu were for as long as this list left them
    # out. check_input_actions.py enforces the pairing in both directions.
    unreal.EditorAssetLibrary.save_asset(path)

    C.log(u"  DA_InputConfig: {0}/{1} fields assigned".format(
        assigned, len(INPUT_CONFIG_FIELD_ORDER)))

    if missing:
        report.failed("DA_InputConfig",
                      u"unassigned fields: {0}".format(", ".join(missing)))

    # Ask the asset itself whether it is complete: the same call the controller
    # makes on possess, so the build fails where the player would have seen a
    # silent missing keybind.
    try:
        still_missing = config.call_method("ValidateAllActionsSet")
        if still_missing:
            report.failed("DA_InputConfig",
                          u"ValidateAllActionsSet still reports: {0}".format(
                              ", ".join(str(m) for m in still_missing)))
        else:
            C.log(u"  ValidateAllActionsSet: all {0} actions bound".format(
                len(INPUT_CONFIG_FIELD_ORDER)))
    except Exception as exc:  # noqa: BLE001
        C.log(u"  ValidateAllActionsSet unavailable in this build: {0}".format(exc))

    return config


def build_all():
    if not C.IN_EDITOR:
        raise RuntimeError("build_input must run inside the Unreal Editor.")

    report = C.BuildReport("build_input")
    C.log(u"=== Building One Braham input ===")

    C.guard(report, "directories", _ensure_dirs)
    actions = C.guard(report, "input actions", build_actions, report) or {}
    contexts = C.guard(report, "mapping contexts", build_contexts, report, actions) or {}
    C.guard(report, "input config", build_input_config, report, actions)

    C.log(u"  actions: {0}, contexts: {1}".format(len(actions), len(contexts)))
    C.log(report.summary())
    return report


if __name__ == "__main__":
    _report = build_all()
    if not _report.ok():
        raise SystemExit(1)
