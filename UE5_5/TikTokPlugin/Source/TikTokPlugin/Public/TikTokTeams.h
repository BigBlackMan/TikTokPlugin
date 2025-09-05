// TikTokTeams.h
#pragma once

#include "CoreMinimal.h"
#include "TikTokTeams.generated.h"

UENUM(BlueprintType)
enum class ETikTokTeamMode : uint8
{
    None       UMETA(DisplayName="No Teams"),
    TwoTeams   UMETA(DisplayName="Two Teams (1–2)"),
    ThreeTeams UMETA(DisplayName="Three Teams (1–3)"),
    FourTeams  UMETA(DisplayName="Four Teams (1–4)")
};

UENUM(BlueprintType)
enum class ETikTokTeam : uint8
{
    None  UMETA(DisplayName="None"),
    Team1 UMETA(DisplayName="Team 1"),
    Team2 UMETA(DisplayName="Team 2"),
    Team3 UMETA(DisplayName="Team 3"),
    Team4 UMETA(DisplayName="Team 4"),
};
