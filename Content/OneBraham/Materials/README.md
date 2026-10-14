# Content/OneBraham/Materials

`M_OB_Surface`, `M_OB_Glass`, `M_OB_Emissive`, `M_OB_Screen` and `M_OB_Decal`
are authored as real node graphs by `Tools/Python/onebraham/build_materials.py`,
including the parameter names every other system sets:

```
BaseColour (vector)  Roughness (scalar)  Metallic (scalar)  SurfaceTypeIndex (scalar)
```

`Instances/` holds one instance per surface plus the brand set. The 15 physical
materials here are what make footsteps, impacts and decals resolve correctly with
no per-asset wiring - the order of `+PhysicalSurfaces` in `Config/DefaultEngine.ini`
is the contract.
