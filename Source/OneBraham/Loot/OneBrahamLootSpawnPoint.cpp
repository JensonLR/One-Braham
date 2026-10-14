// Copyright (c) One Braham. All Rights Reserved.

#include "Loot/OneBrahamLootSpawnPoint.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"

AOneBrahamLootSpawnPoint::AOneBrahamLootSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	// A marker, nothing more: the server reads its transform once at match start.
	bReplicates = false;
	SetCanBeDamaged(false);

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

#if WITH_EDITORONLY_DATA
	EditorSprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("EditorSprite"));
	if (EditorSprite != nullptr)
	{
		EditorSprite->SetupAttachment(SceneRoot);
		EditorSprite->bIsScreenSizeScaled = true;
	}
#endif
}
