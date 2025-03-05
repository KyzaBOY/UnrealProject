#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Networking.h"
#include "MuClientReceiveThread.h"
#include "MuClientGameInstance.generated.h"

// 🔹 Evento Blueprint para pacotes recebidos
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPacketReceived, uint8, HeadCode, uint8, SubCode, FString, DataString);


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

    void GracefulShutdown(); // 📌 Novo método para desligamento seguro

    UFUNCTION(BlueprintCallable, Category = "Network")
    void SendAsyncPacket(FString Data);

    UFUNCTION(BlueprintCallable, Category = "Network")
    void SendPacket(uint8 HeadCode, uint8 SubCode, const FString& DataString);

    // 📌 Conversores para os Blueprints
    UFUNCTION(BlueprintCallable, Category = "Network")
    static FString BytesToString(const TArray<uint8>& DataBuffer);

    UFUNCTION(BlueprintCallable, Category = "Network")
    static int32 BytesToInt(const TArray<uint8>& DataBuffer);

private:
    FSocket* ClientSocket;
    FRunnableThread* ReceiveThread;
    MuClientReceiveThread* ReceiveTask;
};
