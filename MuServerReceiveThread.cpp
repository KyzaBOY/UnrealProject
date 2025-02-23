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

uint32 MuServerReceiveThread::Run()
{
    while (bRunning && ServerSocket)
    {
        // 📌 1️⃣ Verificar novas conexões antes de processar pacotes
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

        TArray<TPair<FString, FSocket*>> ActiveClients;

        // 🔹 Criamos uma cópia temporária dos clientes conectados
        OwnerServer->ClientSocketsMutex.Lock();
        for (auto& Pair : OwnerServer->ClientSockets)
        {
            ActiveClients.Add(Pair);
        }
        OwnerServer->ClientSocketsMutex.Unlock();

        // 📌 Processar pacotes de cada cliente
        for (auto& Pair : ActiveClients)
        {
            FString SocketID = Pair.Key;
            FSocket* Client = Pair.Value;

            if (!Client || Client->GetConnectionState() != ESocketConnectionState::SCS_Connected)
            {
                UE_LOG(LogTemp, Warning, TEXT("⚠️ Cliente desconectado detectado: %s"), *SocketID);
                OwnerServer->ClientSocketsMutex.Lock();
                OwnerServer->ClientSockets.Remove(SocketID);
                OwnerServer->ClientSocketsMutex.Unlock();

                // 📌 Emite o evento `OnFinishConnection`
                AsyncTask(ENamedThreads::GameThread, [this, SocketID]()
                    {
                        OwnerServer->OnFinishConnection.Broadcast(SocketID, TEXT("Desconhecido"));
                    });

                continue;
            }

            // 📌 1️⃣ Primeiro, ler os 2 bytes que indicam o tamanho do pacote
            uint16 PacketSize = 0;
            int32 BytesRead = 0;

            if (!Client->Recv((uint8*)&PacketSize, sizeof(uint16), BytesRead) || BytesRead == 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("⚠️ Cliente %s fechou a conexão inesperadamente. Removendo..."), *SocketID);

                OwnerServer->ClientSocketsMutex.Lock();
                OwnerServer->ClientSockets.Remove(SocketID);
                OwnerServer->ClientSocketsMutex.Unlock();

                // 📌 Emite o evento `OnFinishConnection`
                AsyncTask(ENamedThreads::GameThread, [this, SocketID]()
                    {
                        OwnerServer->OnFinishConnection.Broadcast(SocketID, TEXT("Desconhecido"));
                    });

                continue; // Ignorar leitura de pacotes
            }

            // 📌 2️⃣ Agora aguarda até receber o pacote completo
            TArray<uint8> PacketBuffer;
            PacketBuffer.SetNumUninitialized(PacketSize + 1); // Adiciona espaço para um caractere nulo

            if (!Client->Recv(PacketBuffer.GetData(), PacketSize, BytesRead) || BytesRead != PacketSize)
            {
                UE_LOG(LogTemp, Warning, TEXT("⚠️ Falha ao receber pacote completo de %s. Lidos: %d / %d bytes"), *SocketID, BytesRead, PacketSize);
                continue;
            }

            // 📌 3️⃣ Garantir que o buffer seja tratado corretamente
            PacketBuffer[PacketSize] = '\0'; // Adiciona um terminador nulo para evitar caracteres extras

            FString ReceivedData = FString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(PacketBuffer.GetData())));

            // 🔹 Remover espaços em branco desnecessários e caracteres extras
            ReceivedData.TrimStartAndEndInline();

            UE_LOG(LogTemp, Log, TEXT("📩 Pacote recebido de %s (%d bytes): %s"), *SocketID, PacketSize, *ReceivedData);

            // 🔹 Processar o pacote na Game Thread
            AsyncTask(ENamedThreads::GameThread, [this, ReceivedData, SocketID]()
                {
                    OwnerServer->OnClientPacketReceived.Broadcast(SocketID, ReceivedData);
                });
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