// TikTokGiftTypes.h
#pragma once // Prevent multiple inclusion of this header

#include "CoreMinimal.h"          // Core UE types/macros
#include "TikTokTeams.h"          // Team enums we already ship
#include "TikTokGiftTypes.generated.h" // UHT glue

/**
 * How the manager should auto-queue incoming gifts (set on Connect).
 */
UENUM(BlueprintType) // Expose to Blueprints as a dropdown
enum class EGiftAutoQueueMode : uint8
{
    None           UMETA(DisplayName = "Don't Auto Queue Gifts"),   // Dev handles manually
    ByName         UMETA(DisplayName = "Auto Queue Gifts by Tag (Gift.*)"), // Uses Gameplay Tags
    ByValueBucket  UMETA(DisplayName = "Auto Queue Gifts by Value") // Buckets by diamond value
};

/**
 * Value buckets for diamonds.
 */
UENUM(BlueprintType)
enum class EGiftValueBucket : uint8
{
    V_1                 UMETA(DisplayName = "1"),
    V_2_5               UMETA(DisplayName = "2–5"),
    V_6_10              UMETA(DisplayName = "6–10"),
    V_11_20             UMETA(DisplayName = "11–20"),
    V_21_30             UMETA(DisplayName = "21–30"),
    V_31_99             UMETA(DisplayName = "31–99"),
    V_100               UMETA(DisplayName = "100"),
    V_101_150           UMETA(DisplayName = "101–150"),
    V_151_200           UMETA(DisplayName = "151–200"),
    V_201_250           UMETA(DisplayName = "201–250"),
    V_251_300           UMETA(DisplayName = "251–300"),
    V_301_400           UMETA(DisplayName = "301–400"),
    V_401_500           UMETA(DisplayName = "401–500"),
    V_501_700           UMETA(DisplayName = "501–700"),
    V_701_900           UMETA(DisplayName = "701–900"),
    V_901_1000          UMETA(DisplayName = "901–1000"),
    V_1001_1200         UMETA(DisplayName = "1001–1200"),
    V_1201_1500         UMETA(DisplayName = "1201–1500"),
    V_1501_2000         UMETA(DisplayName = "1501–2000"),
    V_2001_2500         UMETA(DisplayName = "2001–2500"),
    V_2501_3000         UMETA(DisplayName = "2501–3000"),
    V_3001_4000         UMETA(DisplayName = "3001–4000"),
    V_4001_5000         UMETA(DisplayName = "4001–5000"),
    V_5001_6000         UMETA(DisplayName = "5001–6000"),
    V_6001_7000         UMETA(DisplayName = "6001–7000"),
    V_7001_10000        UMETA(DisplayName = "7001–10000"),
    V_10001_13000       UMETA(DisplayName = "10001–13000"),
    V_13001_15000       UMETA(DisplayName = "13001–15000"),
    V_15001_20000       UMETA(DisplayName = "15001–20000"),
    V_20001_25000       UMETA(DisplayName = "20001–25000"),
    V_25001_30000       UMETA(DisplayName = "25001–30000"),
    V_30001_35000       UMETA(DisplayName = "30001–35000"),
    V_35001_40000       UMETA(DisplayName = "35001–40000"),
    V_40001_50000       UMETA(DisplayName = "40001–50000"),
    V_50000P            UMETA(DisplayName = "50000+")
};

/**
 * Utilities for value buckets and team indexing.
 */
struct FTikTokGiftValueBuckets
{
    static EGiftValueBucket FromDiamonds(int32 Diamonds)
    {
        if (Diamonds <= 0) Diamonds = 0;

        if (Diamonds <= 1) return EGiftValueBucket::V_1;
        if (Diamonds <= 5) return EGiftValueBucket::V_2_5;
        if (Diamonds <= 10) return EGiftValueBucket::V_6_10;
        if (Diamonds <= 20) return EGiftValueBucket::V_11_20;
        if (Diamonds <= 30) return EGiftValueBucket::V_21_30;
        if (Diamonds <= 99) return EGiftValueBucket::V_31_99;
        if (Diamonds == 100) return EGiftValueBucket::V_100;
        if (Diamonds <= 150) return EGiftValueBucket::V_101_150;
        if (Diamonds <= 200) return EGiftValueBucket::V_151_200;
        if (Diamonds <= 250) return EGiftValueBucket::V_201_250;
        if (Diamonds <= 300) return EGiftValueBucket::V_251_300;
        if (Diamonds <= 400) return EGiftValueBucket::V_301_400;
        if (Diamonds <= 500) return EGiftValueBucket::V_401_500;
        if (Diamonds <= 700) return EGiftValueBucket::V_501_700;
        if (Diamonds <= 900) return EGiftValueBucket::V_701_900;
        if (Diamonds <= 1000) return EGiftValueBucket::V_901_1000;
        if (Diamonds <= 1200) return EGiftValueBucket::V_1001_1200;
        if (Diamonds <= 1500) return EGiftValueBucket::V_1201_1500;
        if (Diamonds <= 2000) return EGiftValueBucket::V_1501_2000;
        if (Diamonds <= 2500) return EGiftValueBucket::V_2001_2500;
        if (Diamonds <= 3000) return EGiftValueBucket::V_2501_3000;
        if (Diamonds <= 4000) return EGiftValueBucket::V_3001_4000;
        if (Diamonds <= 5000) return EGiftValueBucket::V_4001_5000;
        if (Diamonds <= 6000) return EGiftValueBucket::V_5001_6000;
        if (Diamonds <= 7000) return EGiftValueBucket::V_6001_7000;
        if (Diamonds <= 10000) return EGiftValueBucket::V_7001_10000;
        if (Diamonds <= 13000) return EGiftValueBucket::V_10001_13000;
        if (Diamonds <= 15000) return EGiftValueBucket::V_13001_15000;
        if (Diamonds <= 20000) return EGiftValueBucket::V_15001_20000;
        if (Diamonds <= 25000) return EGiftValueBucket::V_20001_25000;
        if (Diamonds <= 30000) return EGiftValueBucket::V_25001_30000;
        if (Diamonds <= 35000) return EGiftValueBucket::V_30001_35000;
        if (Diamonds <= 40000) return EGiftValueBucket::V_35001_40000;
        if (Diamonds <= 50000) return EGiftValueBucket::V_40001_50000;
        return EGiftValueBucket::V_50000P;
    }

    static int32 TeamToIndex(ETikTokTeam Team)
    {
        switch (Team)
        {
        case ETikTokTeam::Team1: return 0;
        case ETikTokTeam::Team2: return 1;
        case ETikTokTeam::Team3: return 2;
        case ETikTokTeam::Team4: return 3;
        default: return -1;
        }
    }
};
