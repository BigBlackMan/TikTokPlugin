//TikTokGiftQueueManager.cpp

#include "TikTokGiftQueueManager.h"
#include "TikTokGiftQueue.h"
#include "UObject/Package.h"

// Singleton storage
static UTikTokGiftQueueManager* GTikTokGiftQueueManager = nullptr;

UTikTokGiftQueueManager* UTikTokGiftQueueManager::Get()
{
    if (!GTikTokGiftQueueManager)
    {
        GTikTokGiftQueueManager = NewObject<UTikTokGiftQueueManager>(GetTransientPackage());
        if (GTikTokGiftQueueManager)
        {
            GTikTokGiftQueueManager->AddToRoot();
            GTikTokGiftQueueManager->EnsureTeamArrayInitialized();
        }
    }
    return GTikTokGiftQueueManager;
}

void UTikTokGiftQueueManager::Initialize(EGiftAutoQueueMode InMode)
{
    AutoMode = InMode;
    EmptyAllGiftQueues(); // clear contents (preserve queue objects so BP refs remain valid)
}

void UTikTokGiftQueueManager::HandleDisconnected()
{
    // Policy hook: keep contents intact; dev may clear in OnDisconnected if desired.
}

void UTikTokGiftQueueManager::HandleReconnected()
{
    // Policy hook: resume; currently nothing to do.
}

// ------- Canonical Diamond -> Bucket mapping (kept here so UI + parser stay consistent) -------
EGiftValueBucket UTikTokGiftQueueManager::MapValueToBucket(int32 Diamonds) const
{
    if (Diamonds <= 1)    return EGiftValueBucket::V_1;          // 1
    if (Diamonds <= 5)    return EGiftValueBucket::V_2_5;        // 2–5
    if (Diamonds <= 10)   return EGiftValueBucket::V_6_10;       // 6–10
    if (Diamonds <= 20)   return EGiftValueBucket::V_11_20;      // 11–20
    if (Diamonds <= 30)   return EGiftValueBucket::V_21_30;      // 21–30
    if (Diamonds <= 99)   return EGiftValueBucket::V_31_99;      // 31–99
    if (Diamonds == 100)  return EGiftValueBucket::V_100;        // 100
    if (Diamonds <= 150)  return EGiftValueBucket::V_101_150;    // 101–150
    if (Diamonds <= 200)  return EGiftValueBucket::V_151_200;    // 151–200
    if (Diamonds <= 250)  return EGiftValueBucket::V_201_250;    // 201–250
    if (Diamonds <= 300)  return EGiftValueBucket::V_251_300;    // 251–300
    if (Diamonds <= 400)  return EGiftValueBucket::V_301_400;    // 301–400
    if (Diamonds <= 500)  return EGiftValueBucket::V_401_500;    // 401–500
    if (Diamonds <= 700)  return EGiftValueBucket::V_501_700;    // 501–700
    if (Diamonds <= 900)  return EGiftValueBucket::V_701_900;    // 701–900
    if (Diamonds <= 1000) return EGiftValueBucket::V_901_1000;   // 901–1000
    if (Diamonds <= 1200) return EGiftValueBucket::V_1001_1200;  // 1001–1200
    if (Diamonds <= 1500) return EGiftValueBucket::V_1201_1500;  // 1201–1500
    if (Diamonds <= 2000) return EGiftValueBucket::V_1501_2000;  // 1501–2000
    if (Diamonds <= 2500) return EGiftValueBucket::V_2001_2500;  // 2001–2500
    if (Diamonds <= 3000) return EGiftValueBucket::V_2501_3000;  // 2501–3000
    if (Diamonds <= 4000) return EGiftValueBucket::V_3001_4000;  // 3001–4000
    if (Diamonds <= 5000) return EGiftValueBucket::V_4001_5000;  // 4001–5000
    if (Diamonds <= 6000) return EGiftValueBucket::V_5001_6000;  // 5001–6000
    if (Diamonds <= 7000) return EGiftValueBucket::V_6001_7000;  // 6001–7000
    if (Diamonds <= 10000) return EGiftValueBucket::V_7001_10000;// 7001–10000
    if (Diamonds <= 13000) return EGiftValueBucket::V_10001_13000;// 10001–13000
    if (Diamonds <= 15000) return EGiftValueBucket::V_13001_15000;// 13001–15000
    if (Diamonds <= 20000) return EGiftValueBucket::V_15001_20000;// 15001–20000
    if (Diamonds <= 25000) return EGiftValueBucket::V_20001_25000;// 20001–25000
    if (Diamonds <= 30000) return EGiftValueBucket::V_25001_30000;// 25001–30000
    if (Diamonds <= 35000) return EGiftValueBucket::V_30001_35000;// 30001–35000
    if (Diamonds <= 40000) return EGiftValueBucket::V_35001_40000;// 35001–40000
    if (Diamonds <= 50000) return EGiftValueBucket::V_40001_50000;// 40001–50000
    return EGiftValueBucket::V_50000P;                         // > 50000
}

// ---------------- Queue API ----------------

UTikTokGiftQueue* UTikTokGiftQueueManager::GetGiftQueueByName(
    FGameplayTag GiftTypeTag, bool bCreateIfMissing, bool bAutoProcess, float AutoProcessInterval, ETikTokTeam Team)
{
    if (!GiftTypeTag.IsValid())
    {
        return nullptr; // invalid tag → no queue
    }

    if (Team == ETikTokTeam::None)
    {
        return FindOrAdd_ByName(Global, GiftTypeTag, bCreateIfMissing, bAutoProcess, AutoProcessInterval, ETikTokTeam::None);
    }
    FGiftQueues* Scope = GetTeamQueues(Team);
    return Scope ? FindOrAdd_ByName(*Scope, GiftTypeTag, bCreateIfMissing, bAutoProcess, AutoProcessInterval, Team) : nullptr;
}

UTikTokGiftQueue* UTikTokGiftQueueManager::GetGiftQueueByValue(
    EGiftValueBucket Bucket, bool bCreateIfMissing, bool bAutoProcess, float AutoProcessInterval, ETikTokTeam Team)
{
    if (Team == ETikTokTeam::None)
    {
        return FindOrAdd_ByValue(Global, Bucket, bCreateIfMissing, bAutoProcess, AutoProcessInterval, ETikTokTeam::None);
    }
    FGiftQueues* Scope = GetTeamQueues(Team);
    return Scope ? FindOrAdd_ByValue(*Scope, Bucket, bCreateIfMissing, bAutoProcess, AutoProcessInterval, Team) : nullptr;
}

void UTikTokGiftQueueManager::GetNonEmptyQueues(
    TArray<UTikTokGiftQueue*>& OutQueues, ETikTokTeam Team) const
{
    OutQueues.Reset();

    auto CollectIfNotEmpty = [&OutQueues](UTikTokGiftQueue* Q)
        {
            if (Q && !Q->IsEmpty())
            {
                OutQueues.Add(Q);
            }
        };

    if (Team == ETikTokTeam::None)
    {
        ForEachQueue(Global, CollectIfNotEmpty);
    }
    else
    {
        const FGiftQueues* Scope = GetTeamQueues(Team);
        if (Scope) { ForEachQueue(*Scope, CollectIfNotEmpty); }
    }
}

void UTikTokGiftQueueManager::EmptyAllGiftQueues()
{
    auto ClearScope = [](FGiftQueues& Scope)
        {
            for (auto& Pair : Scope.ByName)
            {
                for (UTikTokGiftQueue* Q : Pair.Value.Queues)
                {
                    if (Q) Q->Clear();
                }
            }
            for (auto& Pair : Scope.ByValue)
            {
                for (UTikTokGiftQueue* Q : Pair.Value.Queues)
                {
                    if (Q) Q->Clear();
                }
            }
        };

    ClearScope(Global);
    for (FGiftQueues& T : Teams) { ClearScope(T); }
}

// ---------------- Debug convenience ----------------

void UTikTokGiftQueueManager::DebugPrintNonEmptyQueues(ETikTokTeam Team) const
{
    TArray<UTikTokGiftQueue*> Queues;
    GetNonEmptyQueues(Queues, Team);

    UE_LOG(LogTemp, Log, TEXT("----- Non-Empty Gift Queues (Team=%s, Count=%d) -----"),
        *StaticEnum<ETikTokTeam>()->GetDisplayNameTextByValue((int64)Team).ToString(), Queues.Num());

    for (UTikTokGiftQueue* Q : Queues)
    {
        if (Q)
        {
            Q->PrintGiftTag(TEXT("  •"));
        }
    }
}

// ---------------- Internals ----------------

FGiftQueues* UTikTokGiftQueueManager::GetTeamQueues(ETikTokTeam Team)
{
    EnsureTeamArrayInitialized();
    int32 Index = 0;
    switch (Team)
    {
    case ETikTokTeam::Team1: Index = 0; break;
    case ETikTokTeam::Team2: Index = 1; break;
    case ETikTokTeam::Team3: Index = 2; break;
    case ETikTokTeam::Team4: Index = 3; break;
    default: return nullptr;
    }
    return Teams.IsValidIndex(Index) ? &Teams[Index] : nullptr;
}

const FGiftQueues* UTikTokGiftQueueManager::GetTeamQueues(ETikTokTeam Team) const
{
    if (Teams.Num() < 4) return nullptr;
    int32 Index = 0;
    switch (Team)
    {
    case ETikTokTeam::Team1: Index = 0; break;
    case ETikTokTeam::Team2: Index = 1; break;
    case ETikTokTeam::Team3: Index = 2; break;
    case ETikTokTeam::Team4: Index = 3; break;
    default: return nullptr;
    }
    return Teams.IsValidIndex(Index) ? &Teams[Index] : nullptr;
}

void UTikTokGiftQueueManager::EnsureTeamArrayInitialized()
{
    if (Teams.Num() != 4)
    {
        Teams.SetNum(4);
    }
}

UTikTokGiftQueue* UTikTokGiftQueueManager::CreateQueue(bool bAutoProcess, float AutoProcessInterval)
{
    UTikTokGiftQueue* Q = NewObject<UTikTokGiftQueue>(this);
    if (Q)
    {
        Q->SetAutoProcessing(bAutoProcess, AutoProcessInterval);
    }
    return Q;
}

UTikTokGiftQueue* UTikTokGiftQueueManager::FindOrAdd_ByName(
    FGiftQueues& Scope, const FGameplayTag& GiftTypeTag,
    bool bCreateIfMissing, bool bAutoProcess, float AutoProcessInterval, ETikTokTeam ScopeTeam)
{
    FGiftQueueList* List = Scope.ByName.Find(GiftTypeTag);
    if (!List)
    {
        if (!bCreateIfMissing) return nullptr;
        FGiftQueueList& NewList = Scope.ByName.FindOrAdd(GiftTypeTag);
        UTikTokGiftQueue* NewQ = CreateQueue(bAutoProcess, AutoProcessInterval);
        if (NewQ)
        {
            NewQ->Mode = EGiftAutoQueueMode::ByName;
            NewQ->GiftTag = GiftTypeTag;
            NewQ->Team = ScopeTeam;                   // stamp owning team scope
            NewList.Queues.Add(NewQ);
        }
        return NewQ;
    }
    if (List->Queues.Num() == 0 && bCreateIfMissing)
    {
        UTikTokGiftQueue* NewQ = CreateQueue(bAutoProcess, AutoProcessInterval);
        if (NewQ)
        {
            NewQ->Mode = EGiftAutoQueueMode::ByName;
            NewQ->GiftTag = GiftTypeTag;
            NewQ->Team = ScopeTeam;                   // stamp owning team scope
            List->Queues.Add(NewQ);
        }
        return NewQ;
    }
    return List->Queues.Num() > 0 ? List->Queues[0] : nullptr;
}

UTikTokGiftQueue* UTikTokGiftQueueManager::FindOrAdd_ByValue(
    FGiftQueues& Scope, EGiftValueBucket Bucket,
    bool bCreateIfMissing, bool bAutoProcess, float AutoProcessInterval, ETikTokTeam ScopeTeam)
{
    FGiftQueueList* List = Scope.ByValue.Find(Bucket);
    if (!List)
    {
        if (!bCreateIfMissing) return nullptr;
        FGiftQueueList& NewList = Scope.ByValue.FindOrAdd(Bucket);
        UTikTokGiftQueue* NewQ = CreateQueue(bAutoProcess, AutoProcessInterval);
        if (NewQ)
        {
            NewQ->Mode = EGiftAutoQueueMode::ByValueBucket;
            NewQ->ValueBucket = Bucket;
            NewQ->Team = ScopeTeam;                   // stamp owning team scope
            NewList.Queues.Add(NewQ);
        }
        return NewQ;
    }
    if (List->Queues.Num() == 0 && bCreateIfMissing)
    {
        UTikTokGiftQueue* NewQ = CreateQueue(bAutoProcess, AutoProcessInterval);
        if (NewQ)
        {
            NewQ->Mode = EGiftAutoQueueMode::ByValueBucket;
            NewQ->ValueBucket = Bucket;
            NewQ->Team = ScopeTeam;                   // stamp owning team scope
            List->Queues.Add(NewQ);
        }
        return NewQ;
    }
    return List->Queues.Num() > 0 ? List->Queues[0] : nullptr;
}

void UTikTokGiftQueueManager::ForEachQueue(const FGiftQueues& Scope, TFunctionRef<void(UTikTokGiftQueue*)> Fn) const
{
    for (const auto& Pair : Scope.ByName)
    {
        for (UTikTokGiftQueue* Q : Pair.Value.Queues)
        {
            if (Q) Fn(Q);
        }
    }
    for (const auto& Pair : Scope.ByValue)
    {
        for (UTikTokGiftQueue* Q : Pair.Value.Queues)
        {
            if (Q) Fn(Q);
        }
    }
}
