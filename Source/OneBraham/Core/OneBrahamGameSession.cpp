// Copyright (c) One Braham. All Rights Reserved.

#include "Core/OneBrahamGameSession.h"

#include "OneBraham.h"
#include "Core/OneBrahamGameInstance.h"
#include "Core/OneBrahamGameSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AOneBrahamGameSession::InitOptions(const FString& Options)
{
	Super::InitOptions(Options);
	OptionsModeString = UGameplayStatics::ParseOption(Options, TEXT("Mode"));
}

void AOneBrahamGameSession::RegisterServer()
{
	Super::RegisterServer();

	if (!IsRunningDedicatedServer())
	{
		return;
	}

	UOneBrahamGameInstance* GameInstance = GetGameInstance<UOneBrahamGameInstance>();
	if (GameInstance == nullptr)
	{
		return;
	}

	FString ModeString = OptionsModeString;
	if (ModeString.IsEmpty())
	{
		FParse::Value(FCommandLine::Get(), TEXT("Mode="), ModeString);
	}
	const int64 ModeValue = ModeString.IsEmpty() ? INDEX_NONE : StaticEnum<EOneBrahamMatchMode>()->GetValueByNameString(ModeString);
	const EOneBrahamMatchMode Mode = ModeValue != INDEX_NONE
		? static_cast<EOneBrahamMatchMode>(ModeValue)
		: UOneBrahamGameSettings::Get().DefaultMode;

	FString ServerName;
	FParse::Value(FCommandLine::Get(), TEXT("ServerName="), ServerName);

	// MaxPlayers is AGameSession's, from ?MaxPlayers= or its config default.
	const int32 PlayerCap = FMath::Clamp(MaxPlayers > 0 ? MaxPlayers : ONE_BRAHAM_MAX_PLAYERS, 2, ONE_BRAHAM_MAX_PLAYERS);

	OB_LOG("Registering dedicated server: mode=%s cap=%d name='%s'",
		*UEnum::GetValueAsString(Mode), PlayerCap, *ServerName);
	GameInstance->RegisterDedicatedSession(Mode, PlayerCap, ServerName);
}
