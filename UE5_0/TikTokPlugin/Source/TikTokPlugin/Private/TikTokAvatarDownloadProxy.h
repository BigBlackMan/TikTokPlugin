// TikTokAvatarDownloadProxy.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/Texture2DDynamic.h"
#include "Blueprint/AsyncTaskDownloadImage.h"
#include "TikTokUserRegistry.h" // for UTikTokUserRegistry & OnAvatarDownloaded
#include "TikTokAvatarDownloadProxy.generated.h"

UCLASS()
class UTikTokAvatarDownloadProxy : public UObject
{
    GENERATED_BODY()
public:
    void Begin(UTikTokUserRegistry* InRegistry, const FString& InUserID, const FString& InURL)
    {
        Registry = InRegistry;
        UserID = InUserID;
        URL = InURL;

        Task = UAsyncTaskDownloadImage::DownloadImage(URL);
        if (!Task)
        {
            if (Registry) { Registry->OnAvatarDownloaded(UserID, URL, nullptr, this, /*bSuccess=*/false); }
            return;
        }

        Task->OnSuccess.AddDynamic(this, &UTikTokAvatarDownloadProxy::HandleSuccess);
        Task->OnFail.AddDynamic(this, &UTikTokAvatarDownloadProxy::HandleFail);
    }

private:
    UFUNCTION()
    void HandleSuccess(UTexture2DDynamic* Texture)
    {
        if (Registry) { Registry->OnAvatarDownloaded(UserID, URL, Texture, this, /*bSuccess=*/true); }
    }

    UFUNCTION()
    void HandleFail(UTexture2DDynamic* Texture) // <-- name is required by UHT
    {
        if (Registry) { Registry->OnAvatarDownloaded(UserID, URL, nullptr, this, /*bSuccess=*/false); }
    }

public:
    UPROPERTY() TObjectPtr<UAsyncTaskDownloadImage> Task = nullptr;
    UPROPERTY() TObjectPtr<UTikTokUserRegistry>     Registry = nullptr;
    UPROPERTY() FString UserID;
    UPROPERTY() FString URL;
};
