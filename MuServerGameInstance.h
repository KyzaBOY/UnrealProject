#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Networking.h"
#include "MuServerReceiveThread.h"
#include "MuServerGameInstance.generated.h"

// 🔹 Eventos para Blueprints
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNewConnection, FString, SocketID, FString, IP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFinishConnection, FString, SocketID, FString, IP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnClientPacketReceived, FString, SocketID, FString, Data);

UCLASS()
class MUSERVER_API UMuServerGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    UMuServerGameInstance();

protected:
    virtual void Init() override;
    virtual void Shutdown() override;

public:
    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnNewConnection OnNewConnection;

    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnFinishConnection OnFinishConnection;

    UPROPERTY(BlueprintAssignable, Category = "Network")
    FOnClientPacketReceived OnClientPacketReceived;

    UFUNCTION(BlueprintCallable, Category = "Network")
    bool StartServer(int32 Port);

    UFUNCTION(BlueprintCallable, Category = "Network")
    void StopServer();

    UFUNCTION(BlueprintCallable, Category = "Network")
    void SendAsyncPacket(FString SocketID, FString PacketData);

private:
    FSocket* ServerSocket;
    TMap<FString, FSocket*> ClientSockets;
    FRunnableThread* ServerThread;
    MuServerReceiveThread* ReceiveThread;
};
