#include "MuServerReceiveThread.h"
#include "MuServerGameInstance.h"
#include "Async/Async.h"

// Construtor
MuServerReceiveThread::MuServerReceiveThread(FSocket * InSocket, UMuServerGameInstance * InServer)
{
    ServerSocket = InSocket;
    OwnerServer = InServer;
    bRunning = true;
}

// Destrutorqqqqqqq
MuServerReceiveThread::~MuServerReceiveThread()
{
    bRunning = false;
}

uint32 MuServerReceiveThread::Run()
{
    while (bRunning && ServerSocket)
    {
        // 📌 1️⃣ Verificar novas conexões antes de processar pacotes
        bool bPendingConnection = false;
        if (ServerSocket->HasPendingConnection(bPendingConnection) && bPendingConnection) // ✅ Agora só tenta aceitar 1 conexão por iteração
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

        // 🔹 Criamos uma cópia temporária dos clientes conectados
        TArray<TPair<FString, FSocket*>> ActiveClients;
        OwnerServer->ClientSocketsMutex.Lock();
        for (auto& Pair : OwnerServer->ClientSockets)
        {
            ActiveClients.Add(Pair);
        }
        OwnerServer->ClientSocketsMutex.Unlock();

        // 📌 Processar pacotes de cada cliente
        TArray<FString> ClientsToRemove; // ✅ Lista temporária para armazenar clientes desconectados

        for (auto& Pair : ActiveClients)
        {
            FString SocketID = Pair.Key;
            FSocket* Client = Pair.Value;

            // 🔹 Verificar se o cliente ainda está conectado
            if (!Client || Client->GetConnectionState() != ESocketConnectionState::SCS_Connected)
            {
                UE_LOG(LogTemp, Warning, TEXT("⚠️ Cliente desconectado detectado: %s"), *SocketID);
                ClientsToRemove.Add(SocketID); // ✅ Adiciona para remoção segura após a iteração
                continue;
            }

            // 🔹 Verificar se há dados pendentes antes de tentar ler
            uint16 PacketSize = 0;
            uint8 Buffer[2];

            uint32 PendingBytes = 0;  // ✅ Para HasPendingData()
            int32 BytesRead = 0;      // ✅ Para Recv()

            if (Client->HasPendingData(PendingBytes) && PendingBytes >= 2)
            {
                if (Client->Recv(Buffer, 2, BytesRead))
                {
                    FMemory::Memcpy(&PacketSize, Buffer, 2);

                    TArray<uint8> PacketBuffer;
                    PacketBuffer.SetNumUninitialized(PacketSize + 1);

                    if (Client->Recv(PacketBuffer.GetData(), PacketSize, BytesRead) && BytesRead == PacketSize)
                    {
                        PacketBuffer[PacketSize] = '\0';
                        FString ReceivedData = FString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(PacketBuffer.GetData())));

                        OwnerServer->LastPacketMutex.Lock();
                        OwnerServer->LastPacketTime.Add(SocketID, FPlatformTime::Seconds());
                        OwnerServer->LastPacketMutex.Unlock();

                        UE_LOG(LogTemp, Log, TEXT("📩 Pacote recebido de %s (%d bytes): %s"), *SocketID, PacketSize, *ReceivedData);

                        // 🔹 Processar o pacote na GameThread
                        AsyncTask(ENamedThreads::GameThread, [this, ReceivedData, SocketID]()
                            {
                                OwnerServer->OnClientPacketReceived.Broadcast(SocketID, ReceivedData);
                            });
                    }
                }
            }
        }

        // 📌 Criamos um array de clientes para remoção por timeout
        TArray<FString> TimeoutClients;

        // 🔹 Pegamos o tempo atual
        double CurrentTime = FPlatformTime::Seconds();

        OwnerServer->LastPacketMutex.Lock();
        for (const auto& Pair : OwnerServer->LastPacketTime)
        {
            FString SocketID = Pair.Key;
            double LastTime = Pair.Value;

            // 📌 Se passaram mais de 10 segundos desde o último pacote, remover cliente
            if ((CurrentTime - LastTime) > 50.0)
            {
                TimeoutClients.Add(SocketID);
            }
        }
        OwnerServer->LastPacketMutex.Unlock();

        // 📌 Agora removemos os clientes desconectados por timeout
        if (TimeoutClients.Num() > 0)
        {
            OwnerServer->ClientSocketsMutex.Lock();
            for (const FString& SocketID : TimeoutClients)
            {
                FSocket* ClientSocket = OwnerServer->ClientSockets.FindRef(SocketID);
                if (ClientSocket)
                {
                    ClientSocket->Close();
                    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
                }

                OwnerServer->ClientSockets.Remove(SocketID);
                OwnerServer->LastPacketTime.Remove(SocketID); // ✅ Remover do mapa de timestamps também

                // 📌 Emite o evento `OnFinishConnection`
                AsyncTask(ENamedThreads::GameThread, [this, SocketID]()
                    {
                        OwnerServer->OnFinishConnection.Broadcast(SocketID, TEXT("Timeout"));
                    });

                UE_LOG(LogTemp, Log, TEXT("🔴 Cliente removido por timeout: %s"), *SocketID);
            }
            OwnerServer->ClientSocketsMutex.Unlock();
        }

        // 📌 Agora removemos os clientes desconectados fora do loop para evitar problemas de concorrência
        if (ClientsToRemove.Num() > 0)
        {
            OwnerServer->ClientSocketsMutex.Lock();
            for (const FString& SocketID : ClientsToRemove)
            {
                OwnerServer->ClientSockets.Remove(SocketID);

                // 📌 Emite o evento `OnFinishConnection`
                AsyncTask(ENamedThreads::GameThread, [this, SocketID]()
                    {
                        OwnerServer->OnFinishConnection.Broadcast(SocketID, TEXT("Desconhecido"));
                    });

                UE_LOG(LogTemp, Log, TEXT("🔴 Cliente removido: %s"), *SocketID);
            }
            OwnerServer->ClientSocketsMutex.Unlock();
        }

        // Pequena pausa para evitar sobrecarga da CPU
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