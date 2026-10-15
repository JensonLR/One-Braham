# ONE BRAHAM — multiplayer and voice

Dedicated servers, listen servers, 12 players, three squads, and a proximity
voice system that is a gameplay system rather than a chat feature.

Implementation: `Source/OneBraham/Voice/*`, `Source/OneBraham/Core/OneBrahamGameMode.cpp`,
`Source/OneBraham/Core/OneBrahamPlayerState.cpp`, `Config/DefaultEngine.ini`
(`[/Script/OnlineSubsystemUtils.IpNetDriver]`, `[Voice]`, `[OnlineSubsystem]`),
`Build/Mac/Resources/Info.Template.plist`.

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

## 2. Authority model

| Thing | Authority |
| --- | --- |
| Damage | server only. The client's hit is a *claim*; the server re-traces with a tolerance |
| Fire rate | server: the minimum interval is enforced against the server clock, and over-rate shots are dropped silently |
| Ammo and reload | server |
| Unlock, override, breach | server |
| Lockdown stage, extraction state | server |
| Voice: is this player transmitting | server (`UOneBrahamProximityVoiceComponent::ServerSetTransmitting` sets the replicated `AOneBrahamPlayerState::bIsTransmitting`) |
| Voice: who hears whom | server (`OneBrahamVoiceRouting::ResolveRoute`, applied per frame by the relay) |
| Voice: the audio itself | relayed through the game server as unreliable RPCs; never peer-to-peer |
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

Built on Unreal's own Voice module (`IVoiceCapture`, the Opus `IVoiceEncoder` /
`IVoiceDecoder`) with the game's own transport, not on the online subsystem's
voice interface or a third-party SDK. `[OnlineSubsystem] bHasVoiceEnabled` is
deliberately **False**: the subsystem's voice engine would open a second capture
of the same microphone and could send peer voice that ignores proximity and
mutes. `[Voice] bEnabled=true` is required - without it `FVoiceModule` hands out
no capture, encoder or decoder on any platform.

### Where it lives

| Piece | Host | Why |
| --- | --- | --- |
| Behaviour lines (barks) | `UOneBrahamProximityVoiceComponent` on the character | multicast from the body, so a line comes from the head |
| Live chat: capture, send, relay, receive, playback | the same component class on the **PlayerState** (`AOneBrahamPlayerState::GetVoiceComponent`) | the connection owns its PlayerState whether the player is alive, dead, spectating or between respawns, so voice never depends on having a pawn |

The live-chat API on a character's copy forwards to its PlayerState's copy, so
the HUD's existing calls (`IsLocalTransmitting`, `HasWorkingMicrophone`) keep
working.

### The path

```
mic (16 kHz mono, 20 ms frames)
  -> input gain (ob.Voice.InputGain) -> gate by mode (PTT / voice activation / open mic)
  -> Opus encode -> bounded outgoing queue (200 ms), paced at 50 frames/s
  -> ServerPushVoiceFrame (unreliable, PlayerState component)
  -> server: size check, channel/self-mute check, relay budget
  -> for every player in GameState->PlayerArray: OneBrahamVoiceRouting::ResolveRoute
  -> ClientReceiveVoiceFrame (unreliable) on the listener's PlayerState component
  -> that speaker's own Opus decoder -> USoundWaveProcedural (60 ms start cushion, 240 ms cap)
  -> UAudioComponent: proximity = spatialised + shaped, radio = 2D band-pass
```

### Channels and who hears them

| Speaker | Channel | Who hears | Sound |
| --- | --- | --- | --- |
| alive or downed | Proximity | anyone, enemies included, within the match's proximity range (`UOneBrahamMatchSettingsAsset::ProximityVoiceRangeCm`, default 3,500 cm), measured from where the listener hears (below) | spatialised on the speaker's head, distance/wall/floor/crouch shaped |
| alive or downed | Squad | the speaker's squad, any distance, alive or dead | 2D radio, 300-3,200 Hz |
| alive or downed | Team (FIRE DRILL) | the speaker's team, living listeners only | 2D radio |
| eliminated | anything but Off | **squad radio only**, whatever they selected | 2D radio |
| director | Announcement | everyone (players cannot select it; the server refuses) | 2D |
| anyone | Off, or self-muted | nobody | - |

**Where a listener hears from.** Alive or downed: their own pawn. Eliminated:
the pawn of the player they spectate (`AOneBrahamPlayerController::SpectatedPlayerId`),
so a spectator hears proximity voice exactly as the watched player does - the
engine's audio listener is on the spectate camera, and occlusion is traced from
the camera too. A spectator with nobody to watch hears squad radio only.

**Mutes.** A listener's own `MutedPlayerIds` is the per-listener mute, enforced
by the server's relay and again on the receiving client. Self-mute closes the
speaker's microphone; a self-muted listener still hears. Match switches
`bProximityVoiceEnabled`, `bTeamVoiceEnabled` and `bVoiceOcclusionEnabled` are
honoured.

The range is resolved live in `UOneBrahamVoiceSubsystem::GetProximityRangeCm`:
match settings first, then an explicit `FOneBrahamVoiceChatConfig::ProximityRangeCm`
override (0 = unset), then `UOneBrahamGameSettings::VoiceMaxDistanceCm`, then
3,500 cm. It cannot be cached at subsystem start: the game state that carries
the match settings does not exist yet then.

### How a proximity voice sounds

`OneBrahamVoiceRouting::ShapeProximityVoice` is the single curve, and the tests
assert it. The attenuation object used for live chat only spatialises; the gain
comes from this function so walls and floors fold into the same curve.

| Factor | Effect |
| --- | --- |
| distance | full level inside 2.5 m, then close to inverse distance (exponent 0.85: about -10 dB at 10 m), faded to silence over the last quarter of the range; air absorption rolls the top end from 20 kHz towards 6 kHz |
| each wall (`VoiceOccluder` surface) | about -8 dB, low-pass 1.4 kHz, then lower per extra wall |
| each floor between speaker and listener (400 cm storeys) | about -12 dB, low-pass 700 Hz or lower; two floors is barely there |
| crouched speaker | a whisper: as loud up close, gone by 45 % of the range |

Occlusion is traced at 10 Hz per stream by walking the `VoiceOccluder` channel
surface by surface (a single blocking trace only ever finds the first), and the
result is smoothed so a doorframe does not flicker. The user setting
`bVoiceMuffledThroughWalls` turns walls and floors off locally.

### Transmission modes (Settings -> Audio)

| Mode | Behaviour |
| --- | --- |
| Push to talk (default) | sends while the key is held, plus a 150 ms tail for the last word; the device is open only while transmitting |
| Voice activation | RMS gate at `ob.Voice.NoiseGate` (the menu's activation threshold), closes at 60 % of it (hysteresis), 400 ms hang, an adaptive noise floor so a fan does not hold it open, and one frame of pre-roll so the first consonant is not clipped |
| Open mic | always on, but gated at a third of the activation threshold with a 1 s hang, so silence and hiss are never sent |
| Muted | microphone closed |

In voice activation and open mic, holding push-to-talk forces transmission.
`AOneBrahamPlayerState::bIsTransmitting` follows what is actually being sent (key
held or gate open), set by the server from a reliable RPC, and drives the squad
panel's mic icon.

### Technical spec

| Property | Value |
| --- | --- |
| Sample rate | 16,000 Hz mono |
| Frame size | 20 ms |
| Max frame bytes | 1024 (`MAX_VOICE_FRAME_BYTES`) |
| Encoder | Opus via the engine Voice module; raw PCM16 fallback when no codec |
| Decoder | one per speaker per listener (Opus is stateful; a shared decoder warbles) |
| Send pacing | 50 frames/s, burst of 4, client cap 55/s, under the server's 60/s relay budget |
| Outgoing queue | 10 frames (200 ms); oldest dropped first |
| Jitter buffer | playback starts after 3 frames (60 ms); a backlog over 240 ms is dropped |
| Radio band | 300 Hz high-pass, 3,200 Hz low-pass, applied per channel, not per player |

`FOneBrahamVoiceCodec` packs 20 ms frames to the platform encoder and refuses
anything over the frame budget, which is how a modified client cannot use the
voice channel as a data channel.

### Ducking

While a voice stream is receiving frames, the world mix ducks
(`UOneBrahamAudioManagerComponent::PushVoiceDuck`), ref-counted one reference
per live transmission: four people talking does not duck four times. The
reference is released 0.45 s after that speaker's last frame - one comparison
per stream per tick, not a timer per frame.

### Push to talk

Default `V` (toggle) and `B` (push-to-talk), both rebindable. Hold duration is
never used for voice; a hold that is not registered is the most common complaint
about voice systems and a toggle default avoids it. A 60-second continuous
transmission is allowed and does not silence the match for anybody else, because
the duck is capped at 0.45x and never mutes world audio.

### Settings and console

| Setting / command | Effect |
| --- | --- |
| `UOneBrahamGameUserSettings::VoiceChatMode`, `bVoiceChatEnabled` | mode and opt-out, applied live through `OnVoiceSettingsChanged` |
| `VoiceInputDeviceId` (read with `GetVoiceInputDeviceId`) | the microphone; the legacy `AudioInputDeviceId` from older saves is folded in and retired on the next change |
| `ob.Voice.InputGain` | microphone gain before the gate (mirrors the menu) |
| `ob.Voice.OutputGain` | gain on every received stream (mirrors the menu) |
| `ob.Voice.NoiseGate` | voice-activation threshold (mirrors the menu) |
| `ob.Voice.Mute <name>` / `ob.Voice.Unmute <name>` | per-player mute for the local player (exact name or a unique prefix) |
| `ob.VoiceLoopback 1` | QA: your own voice comes back through the full relay path |

A scoreboard row's mute button calls `UOneBrahamVoiceSubsystem::SetPlayerMuted(PlayerState, bMuted)`
and reads `IsPlayerMuted(PlayerState)`; both are Blueprint-callable. The HUD's
channel label should use `AOneBrahamPlayerState::GetEffectiveVoiceChannel` so a
dead player sees SQUAD rather than the channel they selected while alive.

### macOS

* `[Voice] bEnabled=true` in `Config/DefaultEngine.ini` - the only engine key the
  Voice module reads.
* `Build/Mac/Resources/Info.Template.plist` adds `NSMicrophoneUsageDescription`
  ("ONE BRAHAM uses your microphone for proximity and squad voice chat."). Without
  it macOS kills the process when the microphone opens; the game checks for the
  key and refuses to open the device instead. The editor's own plist already has
  one, so PIE prompts on behalf of UnrealEditor.
* `Build/Mac/Resources/Sandbox.Server.entitlements` (Development) and
  `Sandbox.NoNet.entitlements` (Shipping) add `com.apple.security.device.audio-input`;
  the shipping file also adds the network entitlements a multiplayer client needs.
* Permission is requested when the local player's voice component first ticks in
  a match, not on the first key press. Denied or restricted: capture never opens
  (it would record silence), voice is listen-only, and `HasWorkingMicrophone`
  returns false so the HUD shows the microphone as unavailable. Checked again
  every 5 s, so access granted in System Settings recovers where macOS allows.
* The macOS capture backend always records from the system default input
  device; the device chosen in Settings applies on Windows.
* Reset the prompt for testing with `tccutil reset Microphone`.

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
| Voice transmission state is replicated so the HUD can show a speaker without the audio | `AOneBrahamPlayerState::bIsTransmitting`, set by `UOneBrahamProximityVoiceComponent::ServerSetTransmitting` |

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
| 2 clients, PIE, `ob.VoiceLoopback 0`, one dies | the dead player is heard on squad radio by their squad only, and hears the player they spectate on proximity |
| 2 clients on two floors, then through a wall | proximity voice is quieter and duller through each wall and much more so a floor apart |
| Dedicated server + 12 simulated clients | the full smoke test in `Docs/18_Testing_Performance.md` |
| 100 ms simulated latency and 2% loss | no rubber-banding above the movement tolerance, and no lost unlock |
| A client disconnecting mid-firefight | the match is unaffected and the squad slot is freed |
| A client with a modified damage value | the server ignores it and the audit line records it |
| 60-minute endurance server run | no memory growth above 2%/hour, no actor leak, no stuck state machine |
