#include "MuClientReceiveThread.h"
#include "MuClientGameInstance.h"
#include "Async/Async.h"

// Construtor
MuClientReceiveThread::MuClientReceiveThread(FSocket* InSocket, UMuClientGameInstance* InClient)
{
    ClientSocket = InSocket;
    OwnerClient = InClient;
    bRunning = true;
}

// Destrutor
MuClientReceiveThread::~MuClientReceiveThread()
{
    bRunning = false;
}

uint32 MuClientReceiveThread::Run()
{
    while (bRunning && ClientSocket && ClientSocket->GetConnectionState() == ESocketConnectionState::SCS_Connected)
    {
        uint16 PacketSize = 0;
        int32 BytesRead = 0;

        if (!ClientSocket->Recv((uint8*)&PacketSize, sizeof(uint16), BytesRead) || BytesRead != sizeof(uint16))
        {
            continue; // Se falhar, ignoramos
        }

        TArray<uint8> PacketBuffer;
        PacketBuffer.SetNum(PacketSize);
        if (!ClientSocket->Recv(PacketBuffer.GetData(), PacketSize, BytesRead) || BytesRead != PacketSize)
        {
            continue; // Se falhar, ignoramos
        }

        FString ReceivedData = FString(UTF8_TO_TCHAR((const char*)PacketBuffer.GetData()));

        UE_LOG(LogTemp, Log, TEXT("📩 Pacote recebido: %s"), *ReceivedData);

        // 🔹 Processar o pacote na Game Thread
        if (OwnerClient)
        {
            AsyncTask(ENamedThreads::GameThread, [this, ReceivedData]()
                {
                    OwnerClient->OnPacketReceived.Broadcast(ReceivedData);
                });
        }
    }

    return 0;
}
