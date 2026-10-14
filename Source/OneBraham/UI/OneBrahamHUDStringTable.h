// Copyright (c) One Braham. All Rights Reserved.
//
// OneBrahamHUDStringTable.h
// ---------------------------------------------------------------------------
// The HUD's copy, in one place, with a compiled-in default.
//
// The same principle as the HUD layout itself (Docs/08 section 1): the game is
// playable before any asset exists. A fresh clone has no DT_HUDStrings, so the
// thirty-one strings below are compiled in - the generated DataTable is the
// override, the place a writer edits copy without a recompile, and
// Tools/Python/onebraham/build_ui.py writes it from exactly this list.
//
// That sentence used to be false. The table was created empty - the builder had
// no row struct to write rows onto, because FOneBrahamHUDStringRow did not
// exist, and nothing in C++ read DT_HUDStrings anyway. It was a DataTable asset
// whose only content was a log line in a Python file.
//
// check_hud_strings.py compares this table, the builder's list and the keys the
// code asks for, in both directions.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Internationalization/Text.h"
#include "OneBrahamHUDStringTable.generated.h"

/** One row of DT_HUDStrings. Field names are the CSV columns the builder fills. */
USTRUCT(BlueprintType)
struct ONEBRAHAM_API FOneBrahamHUDStringRow : public FTableRowBase
{
	GENERATED_BODY()

	/** HUD_* key, e.g. HUD_Objective_Ascend. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD")
	FName Key = NAME_None;

	/** The copy. {0} is filled by the caller where the row has a placeholder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD")
	FText Text;
};

/** The compiled-in copy table: key -> text, in table order. */
namespace OneBrahamHUDStrings
{
	/** Every key, as an array of pairs, table order preserved. */
	ONEBRAHAM_API const TArray<TPair<FName, FText>>& All();

	/**
	 * The copy for a key, from the compiled-in table.
	 *
	 * Returns an empty text and logs for an unknown key rather than substituting
	 * something plausible: a HUD string that quietly becomes a different
	 * sentence is worse than a blank line, because a blank line gets reported.
	 */
	ONEBRAHAM_API FText Get(FName Key);

	/** True when the key exists. Used by the checker and by tests. */
	ONEBRAHAM_API bool Has(FName Key);
}
