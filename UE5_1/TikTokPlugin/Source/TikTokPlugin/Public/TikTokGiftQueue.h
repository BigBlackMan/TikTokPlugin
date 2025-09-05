#pragma once // Guard

#include "CoreMinimal.h"                 // Core UE types
#include "UObject/Object.h"              // UObject base
#include "Containers/Ticker.h"           // FTSTicker for auto-processing
#include "GameplayTagContainer.h"        // FGameplayTag
#include "TikTokGiftTypes.h"             // EGiftAutoQueueMode / EGiftValueBucket
#include "TikTokGiftStructs.h"           // FTikTokQueuedGift
#include "TikTokGiftQueue.generated.h"   // UHT glue

// Delegate fired when a gift is enqueued (per-queue)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGiftEnqueued, const FTikTokQueuedGift&, Gift);

// Delegate fired when a gift is processed (per-queue)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGiftProcessed, const FTikTokQueuedGift&, Gift);

// Edge-transition delegates (per-queue)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnQueueBecameNonEmpty);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnQueueBecameEmpty);

/**
 * A single gift queue (by-tag OR by-value-bucket OR a custom/manual queue).
 * Game-thread only; no locks required.
 */
UCLASS(BlueprintType)
class UTikTokGiftQueue : public UObject
{
    GENERATED_BODY()

public:
    // ----- Identity ----- //
    UPROPERTY(BlueprintReadOnly, Category = "TikTok|GiftQueue") EGiftAutoQueueMode Mode = EGiftAutoQueueMode::None; // Which keying mode this queue uses
    // Which team this queue belongs to (or None for global)
    UPROPERTY(BlueprintReadOnly, Category = "TikTok|GiftQueue") ETikTokTeam Team = ETikTokTeam::None;               

    // Name mode: Gameplay Tag (Gift.*)
    UPROPERTY(BlueprintReadOnly, Category = "TikTok|GiftQueue") FGameplayTag GiftTag;                                

    // Value mode: bucket
    UPROPERTY(BlueprintReadOnly, Category = "TikTok|GiftQueue") EGiftValueBucket ValueBucket = EGiftValueBucket::V_1; // Valid when Mode==ByValueBucket

    // ----- Auto-processing configuration ----- //
    UPROPERTY(BlueprintReadOnly, Category = "TikTok|GiftQueue") bool bAutoProcessGifts = false;
    UPROPERTY(BlueprintReadOnly, Category = "TikTok|GiftQueue") float AutoProcessInterval = 0.0f; // 0 = every tick

    // ----- Events ----- //
    UPROPERTY(BlueprintAssignable, Category = "TikTok|GiftQueue") FOnGiftEnqueued OnGiftEnqueued;
    UPROPERTY(BlueprintAssignable, Category = "TikTok|GiftQueue") FOnGiftProcessed OnGiftProcessed;
    UPROPERTY(BlueprintAssignable, Category = "TikTok|GiftQueue") FOnQueueBecameNonEmpty OnBecameNonEmpty;
    UPROPERTY(BlueprintAssignable, Category = "TikTok|GiftQueue") FOnQueueBecameEmpty OnBecameEmpty;

public:
    // Initialize identity; call right after NewObject (optional helper)
    void Initialize(EGiftAutoQueueMode InMode, ETikTokTeam InTeam, const FGameplayTag& InGiftTag, EGiftValueBucket InBucket)
    {
        Mode = InMode;
        Team = InTeam;
        GiftTag = InGiftTag;
        ValueBucket = InBucket;
    }

	// set auto-processing state (enables/disables ticker as needed)
    UFUNCTION(BlueprintCallable, Category = "TikTok|GiftQueue")
    void SetAutoProcessing(bool bEnable, float IntervalSeconds)
    {
        bAutoProcessGifts = bEnable;
        AutoProcessInterval = FMath::Max(0.0f, IntervalSeconds);
        AutoAccumulator = 0.0f;
        EnsureTicker(bAutoProcessGifts);
    }

	// get current auto-processing state
    UFUNCTION(BlueprintPure, Category = "TikTok|GiftQueue")
    bool IsAutoProcessing() const { return bAutoProcessGifts; }

	// returns true if the queue is empty (no pending items)
    UFUNCTION(BlueprintCallable, Category = "TikTok|GiftQueue")
    bool IsEmpty() const { return (Items.Num() - Head) <= 0; } // ← consider head offset

	// Total number of items in the queue (including processed head) -- NumPending() is usually more useful
    UFUNCTION(BlueprintCallable, Category = "TikTok|GiftQueue")
    int32 Num() const { return Items.Num(); }

    // Number of pending items (after head)
    UFUNCTION(BlueprintPure, Category = "TikTok|GiftQueue")
    int32 NumPending() const { return FMath::Max(0, Items.Num() - Head); }

	// Clear all items from the queue (fires OnBecameEmpty if needed)
    UFUNCTION(BlueprintCallable, Category = "TikTok|GiftQueue")
    void Clear()
    {
        const bool bWasNonEmpty = (Items.Num() - Head) > 0;
        Items.Reset();
        Head = 0;
        if (bWasNonEmpty) { OnBecameEmpty.Broadcast(); }
    }

	// Add a gift to the end of the queue (fires OnGiftEnqueued and OnBecameNonEmpty if needed)
    // Requires make Gift struct in BP
    // Target is the unique queue you are adding to (by-tag, by-value, by team)
    UFUNCTION(BlueprintCallable, Category = "TikTok|GiftQueue")
    void AddGift(const FTikTokQueuedGift& Gift)
    {
        const bool bWasEmpty = (Items.Num() - Head) <= 0;
        Items.Add(Gift);
        OnGiftEnqueued.Broadcast(Gift);
        if (bWasEmpty) { OnBecameNonEmpty.Broadcast(); }
    }

	// Peek at the next gift without removing it (returns false if empty)
    UFUNCTION(BlueprintCallable, Category = "TikTok|GiftQueue")
    bool PeekNextGift(FTikTokQueuedGift& OutGift) const
    {
        if ((Items.Num() - Head) <= 0) return false;
        OutGift = Items[Head];
        return true;
    }

	// Remove and return the next gift (returns false if empty)
    UFUNCTION(BlueprintCallable, Category = "TikTok|GiftQueue")
    bool ProcessNextGift(FTikTokQueuedGift& OutGift)
    {
        if ((Items.Num() - Head) <= 0) return false;
        OutGift = Items[Head];
        ++Head;
        if (Head > 32 && Head > Items.Num() / 2)
        {
            Items.RemoveAt(0, Head);
            Head = 0;
        }
        OnGiftProcessed.Broadcast(OutGift);
        if ((Items.Num() - Head) <= 0) { OnBecameEmpty.Broadcast(); }
        return true;
    }

    // ---- Debug helpers (Blueprint-friendly) ---- //

    // Returns a human-readable identity string for this queue
    UFUNCTION(BlueprintPure, Category = "TikTok|GiftQueue")
    FString GetQueueDebugString() const
    {
        const FString TeamStr = StaticEnum<ETikTokTeam>()->GetDisplayNameTextByValue((int64)Team).ToString();
        const int32 Pending = NumPending();

        if (Mode == EGiftAutoQueueMode::ByName)
        {
            const FString TagStr = GiftTag.IsValid() ? GiftTag.ToString() : TEXT("Gift.INVALID");
            return FString::Printf(TEXT("[ByName] %s | Team=%s | Pending=%d"), *TagStr, *TeamStr, Pending);
        }
        else if (Mode == EGiftAutoQueueMode::ByValueBucket)
        {
            const FString BucketStr = StaticEnum<EGiftValueBucket>()->GetDisplayNameTextByValue((int64)ValueBucket).ToString();
            return FString::Printf(TEXT("[ByValue] %s | Team=%s | Pending=%d"), *BucketStr, *TeamStr, Pending);
        }
        return FString::Printf(TEXT("[None/Manual] Team=%s | Pending=%d"), *TeamStr, Pending);
    }

    // Prints the identity string to log (useful while looping over GetNonEmptyQueues)
    UFUNCTION(BlueprintCallable, Category = "TikTok|GiftQueue")
    void PrintGiftTag(const FString& Prefix = TEXT("")) const
    {
        const FString S = GetQueueDebugString();
        if (Prefix.IsEmpty())
        {
            UE_LOG(LogTemp, Log, TEXT("%s"), *S);
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("%s %s"), *Prefix, *S);
        }
    }

    // returns gift tag
    UFUNCTION(BlueprintPure, Category = "TikTok|GiftQueue")
    FGameplayTag GetGiftTag() const { return GiftTag; }

	// returns gift tag as string (empty if invalid)
    UFUNCTION(BlueprintPure, Category = "TikTok|GiftQueue")
    FString GetGiftTagString() const { return GiftTag.IsValid() ? GiftTag.ToString() : FString(); }

    // returns gift value bucket
    UFUNCTION(BlueprintPure, Category = "TikTok|GiftQueue")
    EGiftValueBucket GetGiftValueBucket() const { return ValueBucket; }

protected:
    void EnsureTicker(bool bEnable)
    {
        if (bEnable)
        {
            if (!TickerHandle.IsValid())
            {
                TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
                    FTickerDelegate::CreateUObject(this, &UTikTokGiftQueue::TickAutoProcess),
                    0.0f
                );
            }
        }
        else
        {
            if (TickerHandle.IsValid())
            {
                FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
                TickerHandle.Reset();
            }
        }
    }

    bool TickAutoProcess(float DeltaTime)
    {
        if (!bAutoProcessGifts) return false;
        if ((Items.Num() - Head) <= 0) return true;

        AutoAccumulator += DeltaTime;
        if (AutoAccumulator + KINDA_SMALL_NUMBER >= AutoProcessInterval)
        {
            AutoAccumulator = 0.0f;
            FTikTokQueuedGift Gift;
            ProcessNextGift(Gift);
        }
        return true;
    }

    virtual void BeginDestroy() override
    {
        EnsureTicker(false);
        Super::BeginDestroy();
    }

private:
    TArray<FTikTokQueuedGift> Items;
    int32 Head = 0;
    FTSTicker::FDelegateHandle TickerHandle;
    float AutoAccumulator = 0.0f;
};
