# ONE BRAHAM - running it

How to play a match from a packaged build, and from the editor while developing.
Building the packages is `README_BUILD.md`.

## A local match, packaged

```
Scripts\RunServer.bat                          dedicated server, ASCENT, port 7777
Scripts\RunClient.bat 127.0.0.1                a client, straight into that server
```

Start more clients the same way, from other machines with the server's address.
ASCENT is twelve players in three squads of four; the match waits in the plaza
until it has its players.

| Server switch | Effect |
| --- | --- |
| `-Mode=FireDrill` | FIRE DRILL instead of ASCENT |
| `-Map=<map>` | another map from the cook list (default `L_Persistent_OneBraham`) |
| `-Port=7810` | a second server on the same machine |
| `L_Persistent_OneBraham?Bots=11` as the map | bot practice: that many bots, dealt round the squads |

| Client switch | Effect |
| --- | --- |
| `<address>[:port]` | join that server; omit it for the main menu |
| `-windowed -ResX=1600 -ResY=900` | a window, for running several clients on one machine |
| `-log` | added automatically when joining; the client log opens beside the game |

The server log is in the packaged build's `OneBraham\Saved\Logs\OneBraham.log`; if a
match does not start, its first screen says why.

## A listen server

```
Scripts\RunClient.bat L_Persistent_OneBraham?listen        host, ASCENT
Scripts\RunClient.bat L_Persistent_OneBraham?listen?Bots=11
```

Others join with `Scripts\RunClient.bat <host address>`. A listen host plays and
serves at once, so it is for friends and testing; public matches run on dedicated
servers. The session code behind HOST, matchmaking and the server browser exists
(`UOneBrahamGameInstance`); the front-end screens that call it are not built yet,
so the command line is the way in for now.

## Linux dedicated server

```
Scripts/Package.sh Server                      on a Linux build machine
Scripts/RunServer.sh                           on the box
Scripts/RunServer.sh -Port=7810 -Mode=FireDrill
MAP=L_FireDrill Scripts/RunServer.sh
```

Open the game port (UDP 7777, or the `-Port` you pass) in the host's firewall and
the provider's security group. Voice travels inside the game connection; there is
no second port.

## From the editor

Open `OneBraham.uproject`, open `L_Persistent_OneBraham`, and use Play with the
net mode set to "Play As Client" and the number of players you want: the editor
runs a dedicated server in the background, which is the closest thing to the
shipped topology.

## Controls

The default controls are in `Docs/03_Core_Systems.md` §7, for keyboard and
mouse and for a controller. Rebinds are read from the `[OneBraham.Input]` section
of your `GameUserSettings.ini` (same format as `Config/DefaultInput.ini`; an
action you list there replaces all of that action's default keys on that
device). The Settings > Controls screen that writes them is not built yet.

## Useful console commands (development builds)

| Command | |
| --- | --- |
| `ob.Audio.Dump` | the local mix state |
| `ob.NetDebug 1` | the replication cost overlay |
| `ob.bots.dump` | every practice bot's state (server) |
| `ob.extraction.fast` | jump to the 30 s extraction hold (server, ASCENT) |
| `ob.lockdown [stage]` | force the lockdown stage (server) |
| `ob.Bench.Skydeck` | the scripted performance flythrough |

The full list is `Docs/18_Testing_Performance.md`.
