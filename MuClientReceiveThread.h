#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Sockets.h"
#include "HAL/Event.h"

class UMuClientGameInstance;

class MUCLIENT_API MuClientReceiveThread : public FRunnable
{
public:
    MuClientReceiveThread(FSocket* InSocket, UMuClientGameInstance* InClient);
    virtual ~MuClientReceiveThread();

    virtual uint32 Run() override;
    void StopThread(); // 📌 Método seguro para encerrar a thread

private:
    FSocket* ClientSocket;
    UMuClientGameInstance* OwnerClient;
    bool bRunning;
    FCriticalSection Mutex; // 🔹 Proteção contra concorrência
};