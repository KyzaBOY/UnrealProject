#include "MuServerReceiveThread.h"
#include "MuServerGameInstance.h"
#include "Async/Async.h"

// Construtor
MuServerReceiveThread::MuServerReceiveThread(FSocket* InSocket, UMuServerGameInstance* InServer)
{
    ServerSocket = InSocket;
    OwnerServer = InServer;
    bRunning = true;
}

// Destrutor
MuServerReceiveThread::~MuServerReceiveThread()
{
    bRunning = false;
}

// 📌 Método principal da thread
uint32 MuServerReceiveThread::Run()
{
    while (bRunning && ServerSocket)
    {
        // 📌 1️⃣ Verificar novas conexões
        bool bPendingConnection = false;
        if (ServerSocket->HasPendingConnection(bPendingConnection) && bPendingConnection)
        {
            FSocket* NewClient = ServerSocket->Accept(TEXT("MuServerClientSocket"));
            if (NewClient)
            {
                FString SocketID = FString::Printf(TEXT("%p"), NewClient);
                ClientSockets.Add(SocketID, NewClient);

                FString IP = TEXT("Desconhecido"); // Podemos melhorar isso depois
                AsyncTask(ENamedThreads::GameThread, [this, SocketID, IP]()
                    {
                        if (OwnerServer)
                        {
                            OwnerServer->OnNewConnection.Broadcast(SocketID, IP);
                        }
                    });

                UE_LOG(LogTemp, Log, TEXT("✅ Novo cliente conectado: %s"), *SocketID);
            }
        }

        // 📌 2️⃣ Verificar pacotes recebidos de cada cliente
        for (auto& Pair : ClientSockets)
        {
            FSocket* Client = Pair.Value;
            if (!Client || Client->GetConnectionState() != ESocketConnectionState::SCS_Connected)
            {
                FString SocketID = Pair.Key;
                FString IP = TEXT("Desconhecido");

                AsyncTask(ENamedThreads::GameThread, [this, SocketID, IP]()
                    {
                        if (OwnerServer)
                        {
                            OwnerServer->OnFinishConnection.Broadcast(SocketID, IP);
                        }
                    });

                ClientSockets.Remove(SocketID);
                continue;
            }

            // 📌 Receber dados do cliente
            uint8 Buffer[1024];
            int32 BytesRead = 0;
            if (Client->Recv(Buffer, sizeof(Buffer), BytesRead))
            {
                if (BytesRead > 0)
                {
                    FString ReceivedData = FString(UTF8_TO_TCHAR((const char*)Buffer));

                    UE_LOG(LogTemp, Log, TEXT("📩 Pacote recebido de %s: %s"), *Pair.Key, *ReceivedData);

                    // 🔹 Garantir que o processamento do pacote aconteça na GameThread!
                    AsyncTask(ENamedThreads::GameThread, [this, ReceivedData, SocketID = Pair.Key]()
                        {
                            if (OwnerServer)
                            {
                                OwnerServer->OnClientPacketReceived.Broadcast(SocketID, ReceivedData);
                            }
                        });
                }
            }
        }

        FPlatformProcess::Sleep(0.01);
    }

    return 0;
}

// 📌 Método para parar a thread
void MuServerReceiveThread::Stop()
{
    UE_LOG(LogTemp, Log, TEXT("Parando MuServerReceiveThread..."));
    bRunning = false;  // 🚨 Para a execução do loop principal da thread
}