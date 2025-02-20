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

// 📌 Método principal da thread
uint32 MuClientReceiveThread::Run()
{
    while (bRunning && ClientSocket && ClientSocket->GetConnectionState() == ESocketConnectionState::SCS_Connected)
    {
        uint8 Buffer[1024];
        int32 BytesRead = 0;
        UE_LOG(LogTemp, Log, TEXT("📡 Cliente esperando pacotes do Servidor..."));

        if (ClientSocket->Recv(Buffer, sizeof(Buffer), BytesRead))
        {
            FString ReceivedData = FString(UTF8_TO_TCHAR((const char*)Buffer));

            // 🔹 Separar múltiplos pacotes corretamente usando "\nEND"
            TArray<FString> Packets;
            ReceivedData.ParseIntoArray(Packets, TEXT("\nEND"), true);

            for (FString Packet : Packets)
            {
                if (Packet.IsEmpty()) continue;  // Ignorar pacotes vazios

                UE_LOG(LogTemp, Log, TEXT("Pacote recebido: %s"), *Packet);

                // 📌 Garantir que o evento rode na Game Thread!
                if (OwnerClient)
                {
                    AsyncTask(ENamedThreads::GameThread, [this, Packet]()
                        {
                            OwnerClient->OnPacketReceived.Broadcast(Packet);
                        });
                }
            }
        }
    }

    return 0;
}
