# Content/OneBraham/Characters

Six archetypes, one body set, twelve cosmetic slots.

* Cast table and payload: `Tools/Python/onebraham/build_characters.py`,
  applied by `apply_cast_payload.py` to `DA_CharacterRegistry`.
* Bodies: MetaHuman-class, `SK_OB_Body_<variant>_Skeleton` for the shared
  skeleton and `SK_OB_Body_<variant>` for each of the six body variants.
* Animation: `ABP_OneBrahamCharacter` with the twelve parameters listed in
  `Docs/13_Characters_Clothing.md`.
* Clothing: cloth-simulated via Chaos Cloth, one asset per garment, budgeted at
  eight cloth assets visible per player.

Every cosmetic is cosmetic-only, and `validate_project.py` proves it on every
build rather than trusting it.
