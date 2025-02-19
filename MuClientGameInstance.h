#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Networking.h"
#include "MuClientReceiveThread.h"
#include "MuClientGameInstance.generated.h"

// 🔹 Evento Blueprint para pacotes recebidos
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPacketReceived, FString, Data);

UCLASS()
class MUCLIENT_API UMuClientGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    UMuClientGameInstance();

protected:
    virtual void Init() override;
    virtual void Shutdown() override;

public:
    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnPacketReceived OnPacketReceived;

    UFUNCTION(BlueprintCallable, Category = "Network")
    bool ConnectToServer(FString IP, int32 Port);

    UFUNCTION(BlueprintCallable, Category = "Network")
    void DisconnectFromServer();

    UFUNCTION(BlueprintCallable, Category = "Network")
    void SendAsyncPacket(FString Data);

private:
    FSocket* ClientSocket;
    FRunnableThread* ReceiveThread;
    MuClientReceiveThread* ReceiveTask;
};
