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
                OwnerServer->ClientSocketsMutex.Lock();
                OwnerServer->ClientSockets.Add(SocketID, NewClient);
                OwnerServer->ClientSocketsMutex.Unlock();

                FString IP = TEXT("Desconhecido");
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

        // 📌 2️⃣ Criamos uma cópia temporária dos clientes para evitar bloqueio do mapa
        TArray<TPair<FString, FSocket*>> ActiveClients;

        OwnerServer->ClientSocketsMutex.Lock();
        for (auto& Pair : OwnerServer->ClientSockets)
        {
            ActiveClients.Add(Pair);
        }
        OwnerServer->ClientSocketsMutex.Unlock();

        // 📌 3️⃣ Processamos pacotes de cada cliente sem travar o mapa global
        for (auto& Pair : ActiveClients)
        {
            FSocket* Client = Pair.Value;
            FString SocketID = Pair.Key;

            if (!Client || Client->GetConnectionState() != ESocketConnectionState::SCS_Connected)
            {
                FString IP = TEXT("Desconhecido");

                AsyncTask(ENamedThreads::GameThread, [this, SocketID, IP]()
                    {
                        if (OwnerServer)
                        {
                            OwnerServer->OnFinishConnection.Broadcast(SocketID, IP);
                        }
                    });

                OwnerServer->ClientSocketsMutex.Lock();
                OwnerServer->ClientSockets.Remove(SocketID);
                OwnerServer->ClientSocketsMutex.Unlock();

                continue;
            }

            // 📡 LOG IMPORTANTE
            UE_LOG(LogTemp, Log, TEXT("📡 Servidor esperando pacotes do cliente %s..."), *SocketID);

            uint8 Buffer[1024];
            int32 BytesRead = 0;

            if (Client->Recv(Buffer, sizeof(Buffer), BytesRead))
            {
                if (BytesRead > 0)
                {
                    FString ReceivedData = FString(UTF8_TO_TCHAR((const char*)Buffer));

                    UE_LOG(LogTemp, Log, TEXT("📩 Pacote recebido de %s: %s"), *SocketID, *ReceivedData);

                    // 🔹 Criamos uma Tarefa Assíncrona para processar o pacote
                    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, ReceivedData, SocketID]()
                        {
                            if (OwnerServer)
                            {
                                UE_LOG(LogTemp, Log, TEXT("📦 Processando pacote de %s na Thread de Processamento"), *SocketID);
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