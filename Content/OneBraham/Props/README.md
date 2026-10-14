# Content/OneBraham/Props

Sixty-three prop kinds, 827 instances, catalogued with real dimensions in
`Tools/Python/onebraham/build_props.py` and exported to
`Tools/Python/payloads/prop_manifest.json`.

Conventional paths, so the dressing pass finds them automatically:

```
/Game/OneBraham/Props/<Category>/SM_OB_<Name>
Categories: Office Server Lobby Circulation Executive Canteen Skydeck Loose Decals
```

Model them to the sizes in the manifest, or import a licence-cleared pack and
rename to these paths (`Docs/16_Marketplace_Fallbacks.md` names a pack per
category). Then run `run_all.py --dress` to place them.
