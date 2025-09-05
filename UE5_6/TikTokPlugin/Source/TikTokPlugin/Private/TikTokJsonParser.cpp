// TikTokJsonParser.cpp

#include "TikTokJsonParser.h"                          // This header
#include "TikTokPluginEventHandler.h"                  // For triggering BP delegates
#include "Dom/JsonObject.h"                            // JSON DOM
#include "Dom/JsonValue.h"                             // JSON values
#include "Serialization/JsonReader.h"                  // JSON reader
#include "Serialization/JsonSerializer.h"              // JSON serializer
#include "Async/Async.h"                               // AsyncTask(GameThread)
#include "TikTokUserRegistry.h"                        // Registry singleton (users, textures, teams)
#include "HAL/PlatformTime.h"                          // FPlatformTime::Seconds

// gift queue and types includes (for auto-queue feature)
#include "TikTokGiftQueueManager.h"                    // Manager singleton for gift queues
#include "TikTokGiftQueue.h"                           // Per-queue class (AddGift/Process)
#include "TikTokGiftTypes.h"                           // EGiftAutoQueueMode, EGiftValueBucket
#include "TikTokGiftStructs.h"                         // FTikTokQueuedGift (payload stored in queues)

// Gameplay Tags
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogTikTokJsonParser, Log, All); // Log category

// ---------- Helpers (string sanitization, avatar URL cleanup) ---------- //

static void SanitizeUrlInline(FString& S)
{
    S.TrimStartAndEndInline();
    S.ReplaceInline(TEXT("\r"), TEXT(""));
    S.ReplaceInline(TEXT("\n"), TEXT(""));
    S.ReplaceInline(TEXT("\t"), TEXT(""));
    static const TCHAR* ZWSP = TEXT("\x200B\x200C\x200D\xFEFF");
    for (const TCHAR* P = ZWSP; *P; ++P)
    {
        FString C; C.AppendChar(*P);
        S.ReplaceInline(*C, TEXT(""));
    }
}

enum class EImgKind { Preferred, WebP, Other };

static EImgKind ClassifyExtNoQuery(const FString& Url)
{
    int32 Q = INDEX_NONE;
    FString Head = Url;
    if (Head.FindChar(TEXT('?'), Q)) Head.LeftInline(Q, /*bAllowShrinking=*/false);

    if (Head.EndsWith(TEXT(".jpeg"), ESearchCase::IgnoreCase) ||
        Head.EndsWith(TEXT(".jpg"), ESearchCase::IgnoreCase) ||
        Head.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
    {
        return EImgKind::Preferred;
    }
    if (Head.EndsWith(TEXT(".webp"), ESearchCase::IgnoreCase))
    {
        return EImgKind::WebP;
    }
    return EImgKind::Other;
}

static bool PickBestAvatarUrl(const TArray<TSharedPtr<FJsonValue>>* UrlsArray, FString& OutUrl)
{
    if (!UrlsArray || UrlsArray->Num() == 0) return false;

    FString FirstAny, FirstWebP;

    for (const TSharedPtr<FJsonValue>& V : *UrlsArray)
    {
        if (!V.IsValid() || V->Type != EJson::String) continue;
        FString U = V->AsString();
        SanitizeUrlInline(U);

        const EImgKind K = ClassifyExtNoQuery(U);
        if (K == EImgKind::Preferred)
        {
            OutUrl = U;
            return true;
        }
        if (FirstAny.IsEmpty())  FirstAny = U;
        if (K == EImgKind::WebP && FirstWebP.IsEmpty()) FirstWebP = U;
    }

    if (!FirstWebP.IsEmpty()) { OutUrl = FirstWebP; return true; }
    if (!FirstAny.IsEmpty()) { OutUrl = FirstAny;  return true; }
    return false;
}

static bool TryReadIntFlexible(const FJsonObject* Obj, const TCHAR* Key, int32& OutValue)
{
    if (!Obj) return false;
    double Num = 0.0;
    if (Obj->TryGetNumberField(Key, Num))
    {
        OutValue = static_cast<int32>(Num);
        return true;
    }
    FString Str;
    if (Obj->TryGetStringField(Key, Str))
    {
        OutValue = FCString::Atoi(*Str);
        return true;
    }
    return false;
}

static void ExtractUserCommon(const FJsonObject* User, FString& OutName, FString& OutId, FString& OutAvatar)
{
    if (!User) return;
    User->TryGetStringField(TEXT("nickname"), OutName);
    User->TryGetStringField(TEXT("userId"), OutId);

    if (OutName.IsEmpty())
    {
        FString Unique;
        User->TryGetStringField(TEXT("uniqueId"), Unique);
        if (!Unique.IsEmpty()) OutName = Unique;
    }

    const TSharedPtr<FJsonObject>* PicObjPtr = nullptr;
    if (User->TryGetObjectField(TEXT("profilePicture"), PicObjPtr) && PicObjPtr)
    {
        const TArray<TSharedPtr<FJsonValue>>* Urls = nullptr;

        // FIX: TikTok payload uses "url" (singular). Keep "urls" as a fallback for older payloads.
        if (!(*PicObjPtr)->TryGetArrayField(TEXT("url"), Urls))
        {
            (*PicObjPtr)->TryGetArrayField(TEXT("urls"), Urls);
        }

        if (Urls)
        {
            if (!PickBestAvatarUrl(Urls, OutAvatar))
            {
                OutAvatar.Reset();
            }
        }
    }

    if (!OutAvatar.IsEmpty())
    {
        SanitizeUrlInline(OutAvatar);
    }
}

// ---------- NEW: Helpers to resolve gift name → GameplayTag and diamonds → EGiftValueBucket ---------- //

// Normalize a label to build a tag suffix (lowercase, trim, remove separators)
static FString NormalizeGiftLabel(const FString& In)
{
    FString S = In;                          // Copy input string
    S.TrimStartAndEndInline();               // Remove leading/trailing whitespace
    S.ToLowerInline();                       // Convert to lowercase

    // Remove specific characters that should not appear in tags
    S.ReplaceInline(TEXT(" "), TEXT(""));    // Strip spaces
    S.ReplaceInline(TEXT("_"), TEXT(""));    // Strip underscores
    S.ReplaceInline(TEXT("-"), TEXT(""));    // Strip dashes
    S.ReplaceInline(TEXT(":"), TEXT(""));    // Strip colons
    S.ReplaceInline(TEXT("'"), TEXT(""));    // Strip apostrophes (important for "jolliesheartland")
    S.ReplaceInline(TEXT("&"), TEXT(""));
    S.ReplaceInline(TEXT("!"), TEXT(""));

    return S;                                // Return normalized string
}

// Build a tag from a raw gift name. We expect project-level tags like Gift.rose, Gift.gg, etc.
// If the tag isn't defined in the project settings, this returns an invalid tag (empty).
static FGameplayTag ResolveGiftTagFromName(const FString& RawGiftName)
{
    FString S = RawGiftName;
    S.TrimStartAndEndInline();
    S.ToLowerInline();
    S.ReplaceInline(TEXT(" "), TEXT(""));
    S.ReplaceInline(TEXT("_"), TEXT(""));
    S.ReplaceInline(TEXT("-"), TEXT(""));
    S.ReplaceInline(TEXT(":"), TEXT(""));
    S.ReplaceInline(TEXT("'"), TEXT(""));
    S.ReplaceInline(TEXT("&"), TEXT(""));
    S.ReplaceInline(TEXT("!"), TEXT(""));
    const FString TagStr = FString::Printf(TEXT("Gift.%s"), *S);
    return UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagStr), /*ErrorIfNotFound*/ false);
}

// Map diamonds into a value bucket; prefer manager helper if available
static EGiftValueBucket ResolveBucketFromDiamonds(int32 Diamonds)
{
    if (UTikTokGiftQueueManager* GM = UTikTokGiftQueueManager::Get())
    {
        return GM->MapValueToBucket(Diamonds);
    }
    return FTikTokGiftValueBuckets::FromDiamonds(Diamonds);
}

// ---------- Parser ---------- //

void FTikTokJsonParser::ParseAndDispatchAll(const FString& JsonText, UTikTokPluginEventHandler* Handler)
{
    if (!Handler)
    {
        UE_LOG(LogTikTokJsonParser, Verbose, TEXT("No handler provided; skipping parse."));
        return;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    const bool bOk = FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid();
    if (!bOk)
    {
        UE_LOG(LogTikTokJsonParser, Verbose, TEXT("Invalid JSON payload; skipping."));
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
    if (!Root->TryGetArrayField(TEXT("messages"), Messages) || !Messages)
    {
        return;
    }

    for (const TSharedPtr<FJsonValue>& MsgVal : *Messages)
    {
        if (!MsgVal.IsValid() || MsgVal->Type != EJson::Object) continue;
        const TSharedPtr<FJsonObject> MsgObj = MsgVal->AsObject();

        FString Type; MsgObj->TryGetStringField(TEXT("type"), Type);
        const TSharedPtr<FJsonObject>* DataObjPtr = nullptr;
        if (!MsgObj->TryGetObjectField(TEXT("data"), DataObjPtr) || !DataObjPtr) continue;
        const FJsonObject* Data = DataObjPtr->Get();

        // --- LIKE --- //
        if (Type.Equals(TEXT("WebcastLikeMessage"), ESearchCase::IgnoreCase))
        {
            int32 LikeCount = 0;
            TryReadIntFlexible(Data, TEXT("likeCount"), LikeCount);

            FString Name, UserID, Avatar;
            const TSharedPtr<FJsonObject>* UserObjPtr = nullptr;
            if (Data->TryGetObjectField(TEXT("user"), UserObjPtr) && UserObjPtr)
            {
                ExtractUserCommon(UserObjPtr->Get(), Name, UserID, Avatar);
            }

            AsyncTask(ENamedThreads::GameThread, [Handler, Name, UserID, Avatar, LikeCount]()
                {
                    UTikTokUserRegistry* Reg = UTikTokUserRegistry::Get();
                    Reg->AddOrUpdateUser(UserID, Name, Avatar);
                    Reg->AddLikes(UserID, LikeCount);

                    FTikTokUserRecord RecCopy;
                    Reg->GetUser(UserID, RecCopy);

                    Handler->TriggerLikeEvent(Name, UserID, Avatar, LikeCount, RecCopy);
                });
            continue;
        }

        // --- GIFT --- //
        if (Type.Equals(TEXT("WebcastGiftMessage"), ESearchCase::IgnoreCase))
        {
            const TSharedPtr<FJsonObject>* GiftDetailsPtr = nullptr;
            FString GiftNameRaw;
            int32 DiamondCount = 0;
            if (Data->TryGetObjectField(TEXT("giftDetails"), GiftDetailsPtr) && GiftDetailsPtr)
            {
                GiftDetailsPtr->Get()->TryGetStringField(TEXT("giftName"), GiftNameRaw);
                TryReadIntFlexible(GiftDetailsPtr->Get(), TEXT("diamondCount"), DiamondCount);
            }

            int32 RepeatCount = 1;
            TryReadIntFlexible(Data, TEXT("repeatCount"), RepeatCount);
            int32 RepeatEnd = 0;
            TryReadIntFlexible(Data, TEXT("repeatEnd"), RepeatEnd);

            const int32 GiftValue = DiamondCount;

            // Skip end-of-combo echo (prevents double-counting)
            if (RepeatEnd == 1)
            {
                UE_LOG(LogTikTokJsonParser, Verbose, TEXT("Skipping gift end-of-combo echo (repeatCount=%d)."), RepeatCount);
                continue;
            }

            FString Name, UserID, Avatar;
            const TSharedPtr<FJsonObject>* UserObjPtr = nullptr;
            if (Data->TryGetObjectField(TEXT("user"), UserObjPtr) && UserObjPtr)
            {
                ExtractUserCommon(UserObjPtr->Get(), Name, UserID, Avatar);
            }

            // Fan out to Game Thread
            AsyncTask(ENamedThreads::GameThread, [Handler, Name, UserID, Avatar, GiftNameRaw, GiftValue]()
                {
                    UTikTokUserRegistry* Reg = UTikTokUserRegistry::Get();

                    // Update registry first
                    Reg->AddOrUpdateUser(UserID, Name, Avatar);
                    Reg->AddGiftValue(UserID, GiftValue);

                    // Build queue payload
                    FTikTokQueuedGift Q;
                    Q.UserID = UserID;
                    Q.Username = Name;
                    Q.AvatarURL = Avatar;
                    Q.AvatarTexture = Reg->GetUserProfileTexture(UserID);
                    Q.DiamondValue = GiftValue;
                    Q.GiftNameRaw = GiftNameRaw;

                    // Team at enqueue time
                    ETikTokTeam TeamAtEnqueue = ETikTokTeam::None;
                    Reg->GetUserTeam(UserID, TeamAtEnqueue);
                    Q.Team = TeamAtEnqueue;

                    // Resolve tag
                    Q.GiftTag = ResolveGiftTagFromName(GiftNameRaw);
                    const bool bHasValidTag = Q.GiftTag.IsValid();

                    // Attach a snapshot of the current user record
                    FTikTokUserRecord RecCopy;
                    Reg->GetUser(UserID, RecCopy);
                    Q.UserRecordSnapshot = RecCopy;

                    // Fire the uniform gift event (now includes user record + full payload)
                    Handler->TriggerGiftEvent(Name, UserID, Avatar, GiftNameRaw, GiftValue, RecCopy, Q);

                    // Auto-queue by configured mode
                    if (UTikTokGiftQueueManager* GM = UTikTokGiftQueueManager::Get())
                    {
                        const EGiftAutoQueueMode Mode = GM->GetAutoQueueMode();
                        switch (Mode)
                        {
                        case EGiftAutoQueueMode::None:
                            break;

                        case EGiftAutoQueueMode::ByName:
                        {
                            UE_LOG(LogTikTokJsonParser, Verbose, TEXT("ByName autoqueue: '%s' -> %s (valid=%d) Team=%s"),
                                *GiftNameRaw,
                                bHasValidTag ? *Q.GiftTag.ToString() : TEXT("Gift.INVALID"),
                                bHasValidTag ? 1 : 0,
                                *StaticEnum<ETikTokTeam>()->GetDisplayNameTextByValue((int64)TeamAtEnqueue).ToString());

                            if (bHasValidTag)
                            {
                                // enqueue into the user's team scope (or None if unassigned)
                                if (UTikTokGiftQueue* Queue = GM->GetGiftQueueByName(Q.GiftTag, /*bCreateIfMissing*/ true, /*bAutoProcess*/ false, /*Interval*/ 0.0f, TeamAtEnqueue))
                                {
                                    Queue->AddGift(Q);
                                }
                            }
                        }
                        break;

                        case EGiftAutoQueueMode::ByValueBucket:
                        {
                            const EGiftValueBucket Bucket = ResolveBucketFromDiamonds(Q.DiamondValue);
                            // enqueue into the user's team scope
                            if (UTikTokGiftQueue* Queue = GM->GetGiftQueueByValue(Bucket, /*bCreateIfMissing*/ true, /*bAutoProcess*/ false, /*Interval*/ 0.0f, TeamAtEnqueue))
                            {
                                Queue->AddGift(Q);
                            }
                        }
                        break;

                        default:
                            break;
                        }
                    }
                });

            continue;
        }

        // --- COMMENT --- //
        if (Type.Equals(TEXT("WebcastChatMessage"), ESearchCase::IgnoreCase))
        {
            FString Name, UserID, Avatar;
            const TSharedPtr<FJsonObject>* UserObjPtr = nullptr;
            if (Data->TryGetObjectField(TEXT("user"), UserObjPtr) && UserObjPtr)
            {
                ExtractUserCommon(UserObjPtr->Get(), Name, UserID, Avatar);
            }

            FString CommentText;
            Data->TryGetStringField(TEXT("comment"), CommentText);

            AsyncTask(ENamedThreads::GameThread, [Handler, Name, UserID, Avatar, CommentText]()
                {
                    UTikTokUserRegistry* Reg = UTikTokUserRegistry::Get();
                    Reg->AddOrUpdateUser(UserID, Name, Avatar);
                    UTikTokUserRegistry::Get()->TryAssignTeamFromComment(UserID, CommentText);

                    FTikTokUserRecord RecCopy;
                    Reg->GetUser(UserID, RecCopy);

                    Handler->TriggerCommentEvent(Name, UserID, Avatar, CommentText, RecCopy);
                });

            continue;
        }

        // --- SOCIAL (SHARE / FOLLOW) --- //
        if (Type.Equals(TEXT("WebcastSocialMessage"), ESearchCase::IgnoreCase))
        {
            const TSharedPtr<FJsonObject>* EventObjPtr = nullptr;
            const TSharedPtr<FJsonObject>* DetailsPtr = nullptr;
            FString DisplayType;
            if (Data->TryGetObjectField(TEXT("event"), EventObjPtr) && EventObjPtr &&
                EventObjPtr->Get()->TryGetObjectField(TEXT("eventDetails"), DetailsPtr) && DetailsPtr)
            {
                DetailsPtr->Get()->TryGetStringField(TEXT("displayType"), DisplayType);
            }

            FString Name, UserID, Avatar;
            const TSharedPtr<FJsonObject>* UserObjPtr = nullptr;
            if (Data->TryGetObjectField(TEXT("user"), UserObjPtr) && UserObjPtr)
            {
                ExtractUserCommon(UserObjPtr->Get(), Name, UserID, Avatar);
            }

            FString DTLower = DisplayType;
            DTLower.ToLowerInline();

            const bool bIsFollow = !DTLower.Contains(TEXT("unfollow")) && DTLower.Contains(TEXT("follow"));
            const bool bIsShare = DTLower.Contains(TEXT("share"));

            if (bIsShare)
            {
                const int32 Shares = 1;
                AsyncTask(ENamedThreads::GameThread, [Handler, Name, UserID, Avatar, Shares]()
                    {
                        UTikTokUserRegistry* Reg = UTikTokUserRegistry::Get();
                        Reg->AddOrUpdateUser(UserID, Name, Avatar);
                        Reg->AddShares(UserID, Shares);

                        FTikTokUserRecord RecCopy;
                        Reg->GetUser(UserID, RecCopy);

                        Handler->TriggerShareEvent(Name, UserID, Avatar, Shares, RecCopy);
                    });
                continue;
            }

            if (bIsFollow)
            {
                AsyncTask(ENamedThreads::GameThread, [Handler, Name, UserID, Avatar]()
                    {
                        UTikTokUserRegistry* Reg = UTikTokUserRegistry::Get();

                        FTikTokUserRecord Existing;
                        const bool bHadRecord = Reg->GetUser(UserID, Existing);
                        const bool AlreadyFollowed = bHadRecord && Existing.bFollowed;

                        Reg->AddOrUpdateUser(UserID, Name, Avatar);
                        Reg->SetFollowed(UserID);

                        FTikTokUserRecord RecCopy;
                        Reg->GetUser(UserID, RecCopy);

                        Handler->TriggerFollowEvent(Name, UserID, Avatar, AlreadyFollowed, RecCopy);
                    });
                continue;
            }

            continue;
        }

        // --- JOIN --- //
        if (Type.Equals(TEXT("SyntheticJoinMessage"), ESearchCase::IgnoreCase) ||
            Type.Equals(TEXT("WebcastMemberMessage"), ESearchCase::IgnoreCase))
        {
            FString Name, UserID, Avatar;
            const TSharedPtr<FJsonObject>* UserObjPtr = nullptr;
            if (Data->TryGetObjectField(TEXT("user"), UserObjPtr) && UserObjPtr)
            {
                ExtractUserCommon(UserObjPtr->Get(), Name, UserID, Avatar);
            }

            if (!UserID.IsEmpty())
            {
                AsyncTask(ENamedThreads::GameThread, [Handler, Name, UserID, Avatar]()
                    {
                        UTikTokUserRegistry* Reg = UTikTokUserRegistry::Get();
                        Reg->AddOrUpdateUser(UserID, Name, Avatar);

                        static TMap<FString, double> LastJoinAtSec;
                        const double Now = FPlatformTime::Seconds();
                        const double* Prev = LastJoinAtSec.Find(UserID);
                        const double Cooldown = 15.0;

                        const bool bShouldFire = (!Prev || (Now - *Prev) > Cooldown);
                        if (bShouldFire)
                        {
                            LastJoinAtSec.Add(UserID, Now);

                            FTikTokUserRecord RecCopy;
                            Reg->GetUser(UserID, RecCopy);

                            Handler->TriggerJoinEvent(Name, UserID, Avatar, RecCopy);
                        }
                    });
            }

            continue;
        }

        // --- ROOM INFO --- //
        if (Type.Equals(TEXT("WebcastRoomUserSeqMessage"), ESearchCase::IgnoreCase))
        {
            int32 ViewerCount = 0;
            TryReadIntFlexible(Data, TEXT("viewerCount"), ViewerCount);
            const FString RoomTitle; // Not present

            AsyncTask(ENamedThreads::GameThread, [Handler, ViewerCount, RoomTitle]()
                {
                    Handler->TriggerRoomInfoEvent(RoomTitle, ViewerCount);
                });

            continue;
        }
    }
}
