// TikTokGiftStructs.h
#pragma once // Guard

#include "CoreMinimal.h"              // Basic UE types
#include "Engine/Texture2DDynamic.h"  // Texture type for avatar (if cached)
#include "GameplayTagContainer.h"     // FGameplayTag
#include "TikTokTeams.h"              // Teams
#include "TikTokGiftTypes.h"          // EGiftAutoQueueMode / EGiftValueBucket
#include "TikTokUserRegistry.h"       // FTikTokUserRecord (for embedding a snapshot)
#include "TikTokGiftStructs.generated.h" 

/**
 * Full payload stored in queues so game logic has everything it needs.
 */
USTRUCT(BlueprintType) 
struct FTikTokQueuedGift
{
    // Make struct fields editable in BP (so the auto "Make" node shows pins and
    // BP variables of this struct expose Default Value fields in Details)
    GENERATED_BODY() 

    // Stable user id   
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TikTok|Gift")
    FString UserID;                                     

    // Display/unique name
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TikTok|Gift")
    FString Username;                                   

    // Raw avatar URL
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TikTok|Gift")
    FString AvatarURL;                                  

    // Cached texture if available
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TikTok|Gift")
    TObjectPtr<UTexture2DDynamic> AvatarTexture = nullptr; 

    // Diamonds/coins for this gift
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TikTok|Gift")
    int32 DiamondValue = 0;                             

    // Use Gameplay Tags like: Gift.rose, Gift.gg, etc.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TikTok|Gift")
    FGameplayTag GiftTag;

    // Raw gift name as seen in payload (for fidelity)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TikTok|Gift")
    FString GiftNameRaw;                                

    // Team at enqueue time
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TikTok|Gift")
    ETikTokTeam Team = ETikTokTeam::None;               

    // When enqueued (UTC)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TikTok|Gift")
    FDateTime Timestamp = FDateTime::UtcNow();          

    // Snapshot of the user record at enqueue time (for easy BP access)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TikTok|Gift")
    FTikTokUserRecord UserRecordSnapshot;

    // (C++ convenience) fetch a fresh copy of the current user record from the registry.
    // Not exposed to BP on purpose to avoid adding UFUNCTION to a USTRUCT.
    bool GetLatestUserRecord(FTikTokUserRecord& OutRecord) const
    {
        if (UTikTokUserRegistry* Reg = UTikTokUserRegistry::Get())
        {
            return Reg->GetUser(UserID, OutRecord);
        }
        return false;
    }
};
