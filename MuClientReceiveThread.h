#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"

class UMuClientGameInstance;

class MUCLIENT_API MuClientReceiveThread : public FRunnable
{
public:
    MuClientReceiveThread(FSocket* InSocket, UMuClientGameInstance* InClient);
    virtual ~MuClientReceiveThread();

    virtual uint32 Run() override;

private:
    FSocket* ClientSocket;
    UMuClientGameInstance* OwnerClient;
    bool bRunning;
};