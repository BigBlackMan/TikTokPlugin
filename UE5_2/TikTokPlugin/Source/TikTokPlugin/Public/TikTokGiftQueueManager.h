// TikTokGiftQueueManager.h
#pragma once // Include guard

#include "CoreMinimal.h"               // Core UE types
#include "GameplayTagContainer.h"      // FGameplayTag
#include "TikTokGiftTypes.h"           // EGiftValueBucket / EGiftAutoQueueMode
#include "TikTokTeams.h"               // ETikTokTeam enum
#include "TikTokGiftQueueManager.generated.h" // UHT glue

// Forward declaration
class UTikTokGiftQueue;

// Wrapper so a TMap value can store queue arrays safely (UHT-friendly)
USTRUCT(BlueprintType)
struct FGiftQueueList
{
    GENERATED_BODY()

    UPROPERTY() // GC-tracked
        TArray<TObjectPtr<UTikTokGiftQueue>> Queues;
};

// Holds key spaces (ByName, ByValue) for either Global or a single Team scope
USTRUCT(BlueprintType)
struct FGiftQueues
{
    GENERATED_BODY()

    // Name-based queues keyed by Gameplay Tag (Gift.*)
    UPROPERTY()
    TMap<FGameplayTag, FGiftQueueList> ByName;

    // Value-based queues keyed by diamond bucket
    UPROPERTY()
    TMap<EGiftValueBucket, FGiftQueueList> ByValue;
};

UCLASS(BlueprintType)
class UTikTokGiftQueueManager : public UObject
{
    GENERATED_BODY()

public:
    
    static UTikTokGiftQueueManager* Get();

    // ---------- Lifecycle from BPLibrary ----------
    UFUNCTION(BlueprintCallable, Category = "TikTok|Gifts")
    void Initialize(EGiftAutoQueueMode InMode);

    void HandleDisconnected();
    void HandleReconnected();

    // ---------- Config ----------
    UFUNCTION(BlueprintCallable, Category = "TikTok|Gifts")
    void SetAutoQueueMode(EGiftAutoQueueMode InMode) { AutoMode = InMode; }

    UFUNCTION(BlueprintPure, Category = "TikTok|Gifts")
    EGiftAutoQueueMode GetAutoQueueMode() const { return AutoMode; }

    // Canonical mapping of diamond value -> value bucket (used by parser & UI)
    UFUNCTION(BlueprintPure, Category = "TikTok|Gifts")
    EGiftValueBucket MapValueToBucket(int32 DiamondValue) const;

    
    // Use Gameplay Tag dropdown (Gift.*) to select queue
	// If bCreateIfMissing is true, a new queue is created if needed
	// If bAutoProcess is true, the new queue starts auto-processing at the given time interval
	// Team scope selects which team's queues to access (or None for global)
    UFUNCTION(BlueprintCallable, Category = "TikTok|Gifts",
        meta = (AdvancedDisplay = "bAutoProcess,AutoProcessInterval,Team", GameplayTagFilter = "Gift"))
    UTikTokGiftQueue* GetGiftQueueByName(
        FGameplayTag GiftTypeTag,
        bool bCreateIfMissing,
        bool bAutoProcess = false,
        float AutoProcessInterval = 0.0f,
        ETikTokTeam Team = ETikTokTeam::None);

    // Use EGiftValueBucket dropdown to select queue
    // If bCreateIfMissing is true, a new queue is created if needed
    // If bAutoProcess is true, the new queue starts auto-processing at the given time interval
    // Team scope selects which team's queues to access (or None for global)
    UFUNCTION(BlueprintCallable, Category = "TikTok|Gifts",
        meta = (AdvancedDisplay = "bAutoProcess,AutoProcessInterval,Team"))
    UTikTokGiftQueue* GetGiftQueueByValue(
        EGiftValueBucket Bucket,
        bool bCreateIfMissing,
        bool bAutoProcess = false,
        float AutoProcessInterval = 0.0f,
        ETikTokTeam Team = ETikTokTeam::None);

	// [SELECT TEAM SCOPE] returns all non-empty queues for the selected team (or global if None)
    UFUNCTION(BlueprintCallable, Category = "TikTok|Gifts",
        meta = (AdvancedDisplay = "Team"))
    void GetNonEmptyQueues(
        TArray<UTikTokGiftQueue*>& OutQueues,
        ETikTokTeam Team = ETikTokTeam::None) const;

	// Empties all gift queues in all scopes (global + all teams)
    UFUNCTION(BlueprintCallable, Category = "TikTok|Gifts")
    void EmptyAllGiftQueues();

    // Debug: print all non-empty queues
    UFUNCTION(BlueprintCallable, Category = "TikTok|Gifts", meta = (AdvancedDisplay = "Team"))
    void DebugPrintNonEmptyQueues(ETikTokTeam Team = ETikTokTeam::None) const;

private:
    // Helpers to retrieve per-team scope
    FGiftQueues* GetTeamQueues(ETikTokTeam Team);
    const FGiftQueues* GetTeamQueues(ETikTokTeam Team) const;

    void EnsureTeamArrayInitialized();

    // Factory for new queue with initial auto-processing settings
    UTikTokGiftQueue* CreateQueue(bool bAutoProcess, float AutoProcessInterval);

    // Lookup/create helpers (now carry the scope team so new queues get stamped correctly)
    UTikTokGiftQueue* FindOrAdd_ByName(
        FGiftQueues& Scope, const FGameplayTag& GiftTypeTag,
        bool bCreateIfMissing, bool bAutoProcess, float AutoProcessInterval, ETikTokTeam ScopeTeam);

    UTikTokGiftQueue* FindOrAdd_ByValue(
        FGiftQueues& Scope, EGiftValueBucket Bucket,
        bool bCreateIfMissing, bool bAutoProcess, float AutoProcessInterval, ETikTokTeam ScopeTeam);

    // Iterate all queues in a scope
    void ForEachQueue(const FGiftQueues& Scope, TFunctionRef<void(UTikTokGiftQueue*)> Fn) const;

private:
    UPROPERTY() EGiftAutoQueueMode AutoMode = EGiftAutoQueueMode::None;

    UPROPERTY() FGiftQueues Global;
    UPROPERTY() TArray<FGiftQueues> Teams; // size 4: Team1..Team4
};
