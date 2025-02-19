#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"

class UMuServerGameInstance;

class MUSERVER_API MuServerReceiveThread : public FRunnable
{
public:
    MuServerReceiveThread(FSocket* InSocket, UMuServerGameInstance* InServer);
    virtual ~MuServerReceiveThread();

    virtual uint32 Run() override;

    void Stop();

private:
    FSocket* ServerSocket;
    UMuServerGameInstance* OwnerServer;
    TMap<FString, FSocket*> ClientSockets;
    bool bRunning;
};
