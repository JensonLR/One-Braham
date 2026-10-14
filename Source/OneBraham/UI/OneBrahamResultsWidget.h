// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamResultsWidget.h
// ---------------------------------------------------------------------------
// The end of a match (Docs/08 §4): how it ended, how you did, how each squad
// did, and what you earned. Squad-focused, not a leaderboard: the table reads
// squad by squad, in the order the result lists them.
//
// It is shown on every player's machine when the result replicates, over the
// world, which keeps running behind it. RETURN TO MENU leaves at once; the
// server sends everyone back when the post-match window ends anyway.

#pragma once

#include "CoreMinimal.h"
#include "UI/OneBrahamMenuWidget.h"
#include "Core/OneBrahamTypes.h"
#include "OneBrahamResultsWidget.generated.h"

UCLASS()
class ONEBRAHAM_API UOneBrahamResultsWidget : public UOneBrahamMenuWidget
{
	GENERATED_BODY()

public:
	UOneBrahamResultsWidget(const FObjectInitializer& ObjectInitializer);

	/** The result to show, and the local player's id to find their row. */
	void SetResult(const FOneBrahamMatchResult& InResult, int32 InLocalPlayerId);

	/** Results stay up: back does nothing but leave through the button. */
	virtual void GoBack() override {}

	/** The headline for an ending, also used by tests and the voice director. */
	static FText GetHeadline(const FOneBrahamMatchResult& InResult, bool bLocalWon);

protected:
	virtual void BuildPage() override;
	virtual FText GetPageTitle() const override;
	virtual FText GetFooterText() const override;

	FOneBrahamMatchResult Result;
	int32 LocalPlayerId = INDEX_NONE;
};
