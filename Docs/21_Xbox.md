# ONE BRAHAM — Xbox (ID@Xbox)

What it takes to put the game on Xbox alongside Steam, what this repository
already does towards it, and what it cannot do until the studio is in the
programme.

---

## 1. Getting in

ID@Xbox is Microsoft's programme for independent studios. Joining it gets you
the GDK (Microsoft's toolkit for Xbox and Windows games) and permission to
publish on Xbox. The steps, from the developer docs
([devdocs.xbox.com](https://devdocs.xbox.com/publishing/game-publishing/onboarding/onboarding-join-id-at-xbox.md)):

1. Register as an Xbox partner and sign the non-disclosure agreement.
2. Submit a short application and get the game concept approved. Allow at
   least two weeks, most of it Microsoft's review.
3. Sign the title licence agreement (TLA) and the GDK agreement. You are then in
   the programme.
4. Onboarding continues from there: development kit hardware and royalty
   payouts. You do not need a dev kit to start.

There is no fee to apply, to submit to certification, or to publish and update
([developer.microsoft.com/games/publish](https://developer.microsoft.com/games/publish)).

**For the application:** the pitch is in `Docs/19` section 1. Send a short
gameplay capture once a build runs. A concept is judged on what it shows.

## 2. Unreal Engine on Xbox

Epic does not ship Xbox support in the public engine. Once Microsoft has
approved the studio, Epic gives that studio access to the Xbox platform
extension through Epic's developer portal. It is under the same NDA as the GDK.
Confirm the current steps on Epic's console-development pages when the time
comes; this document does not repeat NDA material, and nothing under that NDA
may be committed to this repository. Keep any platform code in a private
`Platforms/` folder or a separate repository.

## 3. What the code already does

Everything below is written against the engine's platform-neutral interfaces
(the online subsystem, the input device mapper, the core lifecycle delegates).
On Steam it works today. On Xbox it should work through the GDK's online
subsystem without a rewrite, but that is unverified until it runs on a kit.

| Requirement area | Where | What it does |
|---|---|---|
| Controller play | `Core/OneBrahamAimAssist`, `AOneBrahamPlayerController::TickAimAssist` | aim assist on the stick only: slowdown over an enemy and a follow while the player is moving. No snap. Walls and smoke stop it, and so do teammates and downed players. Strength is in Settings → Controls. |
| Controller disconnect | `UOneBrahamPlatformSubsystem` | a pad that drops while in use opens the pause menu and the HUD asks for it back. The world is never paused (PvP). |
| Suspend / resume | `UOneBrahamPlatformSubsystem` | losing focus or being suspended opens the pause menu. Resuming re-reads privileges and the block list. |
| Online privilege | `UOneBrahamGameInstance::CheckOnlinePrivilege` | an account that may not play online cannot matchmake, browse or host online. The menu says why. Practice and LAN still work. |
| Communication privilege | voice subsystem, proximity voice component | an account that may not talk online neither sends nor hears voice, whatever the game's own setting is |
| Block list | `UOneBrahamPlatformSubsystem::IsBlocked` | voice from a player on the platform block list is dropped on arrival |
| Crossplay | session keys `OB_PLATFORM`, `OB_CROSSPLAY`; Settings → Gameplay → Online | another platform's lobby is listed only when both sides allow crossplay. Dedicated servers carry no platform and suit everyone. The platform's own crossplay privilege overrides the setting. |
| Disconnect reasons | `UOneBrahamPlatformSubsystem::DescribeNetworkFailure` | a dropped or refused connection lands back on the main menu with a line saying why |

`OneBraham.Online.PlatformRules` and `OneBraham.Input.AimAssist` test the pure halves of these.

## 4. What is left, and needs the GDK

- **Achievements and stats.** These are Xbox's own systems, configured in Partner Center.
  `UOneBrahamSteamSubsystem` already uses the online subsystem's achievement interface. The
  stat writes go through Steam's leaderboard path, so Xbox needs its own
  writer. Use `GetAchievementDefinitions()` to build the Partner Center list.
- **Rich presence.** Xbox uses its own presence strings. The tokens in
  `Config/Steam/rich_presence_english.vdf` are the list to copy.
- **Sign-in and user switching.** The user signs in on the title screen and the
  game handles a sign-out mid-session. That needs the GDK's identity flow.
- **Pad button glyphs.** Prompts already switch to the pad's keys. They show the
  key names, not Xbox button art. Glyph textures are a content task.
- **Title-safe area.** Every HUD panel should sit inside a `USafeZone`. Check on a TV at 90% safe area.
- **Performance targets.** Series S is the floor. Set the scalability preset per
  device in the platform's ini, which lives in the NDA folder.
- **Certification.** Microsoft's Xbox Requirements are under the NDA. Run the
  build against that list on a kit before submitting.
- **Store and ratings.** Xbox store listing, plus IARC or regional ratings. The Steam page copy in
  `Docs/19` is the starting point.

None of the items in this section has been started. No promise about an Xbox
release goes in public copy until it has.
