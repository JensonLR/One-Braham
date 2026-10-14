# Content/OneBraham/Weapons

`Blueprints/BP_OB_Weapon_<Name>` for all ten weapons, subclassing
`AOneBrahamWeaponBase`. The data lives in `/Game/OneBraham/Data/Weapons/`, so an
artist can work on a weapon mesh without touching its ballistics and a designer
can retune a weapon without opening a mesh.

Socket names are fixed by the data asset and the animation code:
`Muzzle`, `ShellEject`, `OpticMount`, `MuzzleMount`, `GripMount`, `LaserMount`,
`Magazine`, `MagWell`, `ChargingHandle`.

Attachment and skin meshes are built from `StaticMesh` parts, not skeletal
meshes: it keeps ten weapons, four attachments each and six skins each inside the
texture and draw-call budget in `Docs/18_Testing_Performance.md`.
