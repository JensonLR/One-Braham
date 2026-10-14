# Content/OneBraham/UI

* `HUD/WBP_OneBrahamHUD` subclasses `UOneBrahamHUDWidget`. The C++ builds the
  whole layout when the widget tree is empty, so the HUD works before any UMG
  work exists and a designer can then take over one element at a time.
* `Fonts/F_OneBrahamSans` - composite font, weights and sizes in `Docs/08_UI.md`.
* `DT_HUDStrings` - every string the HUD shows, in one localisable table.
* `Menus/` - main menu, deploy screen, wardrobe, settings, results, server browser.
