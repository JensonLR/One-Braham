# Content/OneBraham/Data

* `DA_WeaponRegistry` and `Weapons/DA_Weapon_<Name>` - the ten weapons, fully tuned.
* `DA_CharacterRegistry` - the six archetypes and twelve cosmetic slots.
* `DA_InputConfig` - every input action the controller binds.
* `DA_MatchSettings_<Mode>` - per-mode tuning (timings, lockdown stages, extraction).

All are generated or applied by scripts in `Tools/Python/onebraham/`. If a value
here needs to change for good, change it in the script so the next build keeps
it; edit the asset directly only for a playtest experiment, and expect it to be
overwritten.
