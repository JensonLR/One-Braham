# ONE BRAHAM — multiplayer and voice

Dedicated servers, listen servers, 12 players, three squads, and a proximity
voice system that is a gameplay system rather than a chat feature.

Implementation: `Source/OneBraham/Voice/*`, `Source/OneBraham/Core/OneBrahamGameMode.cpp`,
`Config/DefaultEngine.ini` (`[/Script/OnlineSubsystemUtils.IpNetDriver]`).

---

## 1. Topology

| Mode | Who hosts | Notes |
| --- | --- | --- |
| Dedicated server | the player's own Linux server, or a rented one | the shipping default for ASCENT: no host advantage in a no-respawn mode |
| Listen server | a player | supported for FIRE DRILL and private lobbies; the host is flagged in the scoreboard |
| Offline | local | a full match with bots, using the same game mode |

Server tick rate: **60 Hz**, `NetServerMaxTickRate=60`. Bandwidth cap
`MaxClientRate=96000`. Replication graph on, `net.UseAdaptiveNetUpdateFrequency=1`.

The dedicated server runs `-server -NullRHI -log`: no rendering, no audio
device, no voice capture. Every system in this project checks
`IsRunningDedicatedServer()` before touching a client-only subsystem, and
`UOneBrahamVoiceSubsystem` refuses to initialise capture on a dedicated process
rather than failing at capture time.

### Finding a match

A dedicated server advertises itself. `AGameModeBase::InitGame` calls
`RegisterServer` on any networked server without a session, and
`AOneBrahamGameSession` turns that into `RegisterDedicatedSession` on the
game instance: an advertised, presence-free session (`bIsDedicated`, no lobby,
no invites) carrying the mode, the name from `-ServerName=`, the region and
the network protocol. A listen host made its session from the menu, so the
same call does nothing there.

The server browser and matchmaking search twice: player lobbies, then
dedicated servers (`SEARCH_DEDICATED_ONLY`). Both passes filter on
`OB_PROTOCOL`, so an out-of-date client never sees a server it cannot join.
The results of both go into one list, ping first; a row's index stays
joinable whichever pass found it. A LAN browse has no lobbies and runs one pass.

Every session call binds its completion delegate by handle and unbinds it in
the handler, so a second host, search or join never fires the first one's
callback. Cancelling matchmaking cancels the search in flight
(`CancelFindSessions`), and so does the timeout before it hosts. A re-host waits
for the old session's destroy to complete before it creates the new one.
`Shutdown` stops the search, unbinds everything and leaves the session.

## 2. Authority model

| Thing | Authority |
| --- | --- |
| Damage | server only. The client's hit is a *claim*; the server re-traces with a tolerance |
| Fire rate | server: the minimum interval is enforced against the server clock, and over-rate shots are dropped silently |
| Ammo and reload | server |
| Unlock, override, breach | server |
| Lockdown stage, extraction state | server |
| Voice: is this player transmitting | server (`ServerSetVoiceTransmitting`) |
| Voice: the audio itself | peer-to-peer via `UVoiceChannel`, not through the game server |
| Movement | server-authoritative with client prediction and a 15% speed tolerance |
| Cosmetics | server: a client cannot grant itself an appearance |

Anti-cheat basics (thresholds in `Config/DefaultGame.ini` `[OneBrahamAntiCheat]`):

* Impossible speed: flagged above 1.15× the movement component's maximum for
  more than 0.4 s; corrected on the spot, logged to the server's audit line.
* Impossible fire rate: over-rate shots are dropped; a sustained 20% over-rate
  flags.
* Damage originating from a client value: ignored entirely; the server recomputes.
* Hit registration through geometry: rejected — the server line-traces the same
  shot from the same origin with a 12 cm tolerance.
* Shot from outside the map volume: rejected.
* Reported and kicked, not silently tolerated. The reason is written to the
  player's session so a support ticket can be answered.

### Hit rewind

A shot is scored against the world the shooter was looking at.
`UOneBrahamLagCompensationSubsystem` samples every character's body mesh 30
times a second on the server. When a shot arrives it moves every other
character's mesh (the per-bone `OneBrahamHitbox` bodies) to where it was one
round trip plus `ob.LagComp.ExtraMs` (50 ms, the proxy smoothing) ago,
interpolated between samples. Then it runs the trace, ignoring the
present-day capsules, and puts the meshes back. All of that happens in one
frame, so nothing is replicated in between.

* **The cap.** The rewind is capped at `ob.LagComp.MaxMs` (250 ms). Beyond
  that, a shooter has to lead their targets.
* **The round trip.** It is the full round trip, not half. A client sees
  state that is half a round trip old, and its shot takes the other half to
  arrive.
* **The pose.** It is the current pose at the old position. Rewinding every
  bone is possible from the same history but has not been needed at these
  ranges.
* **Hosts and bots.** A listen host's own shots and bots are not rewound;
  they already see the present.
* **Switching it off.** `ob.LagComp 0` turns rewind off, so the
  difference can be felt on a high-ping test client.

## 3. The player path

```
Connect → travel to L_Persistent_OneBraham
  → GameMode::PreLogin            slot reserved, anti-cheat values initialised
  → PostLogin                     PlayerController + PlayerState + voice registration
  → HandleStartingNewPlayer       team + squad assignment (see below)
  → RestartPlayer                 PlayerStart chosen by SpawnPoint scoring
  → OnDeploy (deploy screen)      archetype, loadout, spawn confirmed
  → playing
  → OnLogout                      squad slot released, kill feed informed
```

### Squad assignment

Three squads of four for ASCENT, two teams of six or eight for FIRE DRILL.
The game mode fills the smallest squad that still has room, so a party of four
that joins together stays together; a solo player is placed in the squad with
the lowest average skill, which is the cheapest form of balance that works.

### Late join

Allowed in FIRE DRILL, allowed in ASCENT until the first lockdown stage. After
that, the match is closed to new players, and a late join is given the deploy
screen and a spawn anchor scored with the usual 2,000 cm enemy exclusion.

---

## 4. Voice

Wired to Unreal's own Voice module (`UVoiceChannel`, `IVoiceTalkInterface`),
not to a third-party SDK, and routed by this project's own mixer so the same
attenuation, occlusion and duck rules apply to speech and to gunfire.

### Channels

| Channel | Who hears | Attenuation | Ducking |
| --- | --- | --- | --- |
| Proximity | anybody within 2,500 cm | inverse distance, 3200 Hz radio band | no |
| Squad | your squad, any distance | none (radio) | no |
| Team (FIRE DRILL) | your team | none (radio) | no |
| Command | dead players to living squad | radio, always intelligible | no |
| Muted | nobody | — | — |

Proximity voice is **spatial**: a voice on the other side of a wall is quieter
and duller, because the same occlusion trace that affects footsteps also affects
speech. That single fact is what makes the voice system a gameplay system:
whispering on a stairwell is a real tactic, and a squad that talks too much on
the wrong floor is a squad that gets found.

### Technical spec

| Property | Value |
| --- | --- |
| Sample rate | 16,000 Hz monophosphate (16 kHz mono) |
| Frame size | 20 ms |
| Max frame bytes | 1024 (`MAX_VOICE_FRAME_BYTES`) |
| Encoder | Opus, 24 kbps VBR, in-band FEC |
| Jitter buffer | 60 ms adaptive, 200 ms maximum |
| Radio band | 3200 Hz low-pass, applied per channel, not per player |

`FOneBrahamVoiceCodec` packs 20 ms frames to the platform encoder and refuses
anything over the frame budget, which is how a modified client cannot use the
voice channel as a data channel.

### Ducking

When speech is routed to the proximity channel, the world mix ducks to **0.45×**
over **250 ms** and returns over 400 ms, ref-counted: four people talking does
not duck four times. Playing the same voice line twice produces two ducks, not
one — the ref count is per transmission, not per player.

### Push to talk

Default `V` (toggle) and `B` (push-to-talk), both rebindable. Hold duration is
never used for voice; a hold that is not registered is the most common complaint
about voice systems and a toggle default avoids it. A 60-second continuous
transmission is allowed and does not silence the match for anybody else, because
the duck is capped at 0.45× and never mutes world audio.

### Voice lines versus voice chat

They are different systems and they never mix:

* **Voice lines** (`Docs/14_Voice_Script.md`, 161 lines) are the *game's* voice:
  pre-recorded, triggered by gameplay, subtitled, and they play on the dialogue
  bus.
* **Voice chat** is the *player's* voice: captured, routed, spatialised, and it
  plays on the voice bus.

A player who hears a squadmate say "behind you" and the Finance archetype say
"Contact, east" hears them on two different buses with two different treatments,
and the game never confuses the two.

---

## 5. Network-relevant game rules

| Rule | Where |
| --- | --- |
| 12 players = 3 squads of 4 | `MAX_SQUADS 4`, `SQUAD_SIZE 4`, `MAX_PLAYERS 16` |
| Match end on extraction or total elimination | `OneBrahamGameMode` |
| Lockdown stages advance on the server clock and replicate as an enum plus a time | `UOneBrahamLockdownComponent` |
| Doors and shutters replicate state, not position: the opening animation is driven locally from a multicast timestamp | `AOneBrahamAccessZone`, `AOneBrahamFireShutter` |
| Breach damage is applied server-side and the opening replicates as a state change | `IOneBrahamBreachableInterface` |
| Extraction is a state machine on the game state component; the countdown is derived locally from the replicated stage start time, so it never drifts | `UOneBrahamExtractionComponent` |
| Kill feed entries are replicated reliably, batched per frame | `AOneBrahamGameState` |
| The death screen and its cause line go to the player who died, and only to them | `AOneBrahamPlayerController::ClientShowDeathScreen` (Client, Reliable) |
| A kill is recorded once - feed, personal score and team score in one function | `AOneBrahamGameMode::RegisterKill` |
| Voice transmission state is replicated so the HUD can show a speaker without the audio | `UOneBrahamVoiceSubsystem` |

### Why timers are derived rather than replicated

A replicated countdown that ticks on the server and sends every second is a
countdown that arrives late, drifts against the client's clock, and stutters.
Every timer in this game replicates a **start time** and a **duration**; the
client computes the remainder locally from the server timestamp. The extraction
hold, the lockdown stage, the door swing and the terminal override all work this
way.

---

## 6. Testing multiplayer

| Test | What it proves |
| --- | --- |
| 2 clients, PIE | joins, deploys, damages, dies, spectates |
| 4 clients, PIE, one per squad | squads assign correctly and voice channel routing is right |
| Dedicated server + 12 simulated clients | the full smoke test in `Docs/18_Testing_Performance.md` |
| 100 ms simulated latency and 2% loss | no rubber-banding above the movement tolerance, and no lost unlock |
| A client disconnecting mid-firefight | the match is unaffected and the squad slot is freed |
| A client with a modified damage value | the server ignores it and the audit line records it |
| 60-minute endurance server run | no memory growth above 2%/hour, no actor leak, no stuck state machine |
