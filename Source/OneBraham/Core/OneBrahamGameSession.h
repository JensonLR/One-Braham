// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamGameSession.h
// ---------------------------------------------------------------------------
// The server's half of the online session.
//
// AGameModeBase::InitGame calls RegisterServer on any networked server that
// has no session yet. A listen host already made its session from the menu,
// so this only does work on a dedicated server: it reads the mode, the player
// cap and the advertised name, and asks the game instance to advertise the
// server so the browser and matchmaking can find it.
//
// Launch options read here (map URL first, then the command line):
//   ?Mode=Ascent | -Mode=Ascent       the match mode
//   ?MaxPlayers=12                    the engine's own cap, already parsed
//   -ServerName="One Braham EU 1"     the name in the browser

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameSession.h"
#include "OneBrahamGameSession.generated.h"

UCLASS()
class ONEBRAHAM_API AOneBrahamGameSession : public AGameSession
{
	GENERATED_BODY()

public:
	virtual void InitOptions(const FString& Options) override;
	virtual void RegisterServer() override;

protected:
	/** The mode named in the map URL, kept from InitOptions for RegisterServer. */
	FString OptionsModeString;
};
