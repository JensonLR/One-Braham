# Content/OneBraham/VFX

17 Niagara systems, 3 camera shakes, 6 decal materials - all created by
`build_vfx.py`, all listed with their cull distance and concurrency budget.

The emitter stacks are the one part that is authored by hand, because that is
what Niagara is for. Their specs (emitters, modules, values, budgets) are in
`Tools/Python/payloads/vfx_authoring_sheet.txt` and explained in
`Docs/11_Niagara_VFX.md`.

The game runs with none of them present: `UOneBrahamVFXStatics` falls back to a
sprite actor and logs once per effect. That is a development convenience, not a
shipping plan.
