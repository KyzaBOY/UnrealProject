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

                OwnerServer->LastPacketMutex.Lock();
                OwnerServer->LastPacketTime.Add(SocketID, FPlatformTime::Seconds());
                OwnerServer->LastPacketMutex.Unlock();

                UE_LOG(LogTemp, Log, TEXT("📌 Cliente %s adicionado ao LastPacketTime com timestamp inicial."), *SocketID);

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

            // 🔹 Verificar se há pelo menos 3 bytes disponíveis antes de tentar ler
            uint32 PendingBytes = 0;
            if (!Client->HasPendingData(PendingBytes) || PendingBytes < 3)
            {
                continue; // Se não há pelo menos 3 bytes pendentes, pula para o próximo cliente
            }

            // 🔹 Ler cabeçalho do pacote (tamanho, código principal e subcódigo)
            uint8 HeaderBuffer[3];
            int32 BytesRead = 0;
            if (!Client->Recv(HeaderBuffer, 3, BytesRead) || BytesRead != 3)
            {
                // 📌 Incrementa o contador de erros para esse cliente
                OwnerServer->ClientSocketsMutex.Lock();
                int32& ErrorCount = OwnerServer->ClientErrorCount.FindOrAdd(SocketID);
                ErrorCount++;
                OwnerServer->ClientSocketsMutex.Unlock();

                UE_LOG(LogTemp, Warning, TEXT("⚠️ Erro ao ler cabeçalho do pacote de %s (Tentativa %d/10)"), *SocketID, ErrorCount);

                // 📌 Se o cliente atingir 10 erros consecutivos, ele será desconectado
                if (ErrorCount >= 10)
                {
                    UE_LOG(LogTemp, Error, TEXT("❌ Cliente %s removido por excesso de erros de leitura!"), *SocketID);

                    // 🔹 Remover cliente do servidor
                    OwnerServer->DisconnectPlayer(SocketID);

                    // 🔹 Resetar contagem de erros
                    OwnerServer->ClientSocketsMutex.Lock();
                    OwnerServer->ClientErrorCount.Remove(SocketID);
                    OwnerServer->ClientSocketsMutex.Unlock();
                }

                continue; // Pular esse cliente e processar o próximo
            }

            uint8 PacketSize = HeaderBuffer[0];
            uint8 HeadCode = HeaderBuffer[1];
            uint8 SubCode = HeaderBuffer[2];

            // 🔹 Verificar se o tamanho do pacote faz sentido
            if (PacketSize < 3 || PacketSize > 255) // Evita pacotes inválidos
            {
                UE_LOG(LogTemp, Error, TEXT("❌ Pacote com tamanho inválido de %d bytes recebido de %s"), PacketSize, *SocketID);
                continue;
            }

            // 🔹 Verificar novamente se há dados suficientes para o corpo do pacote
            if (!Client->HasPendingData(PendingBytes) || PendingBytes < static_cast<uint32>(PacketSize - 3))
            {
                UE_LOG(LogTemp, Warning, TEXT("⚠️ Dados insuficientes para completar o pacote de %s"), *SocketID);
                continue;
            }

            // 🔹 Ler os dados do pacote
            TArray<uint8> DataBuffer;
            DataBuffer.SetNum(PacketSize - 3);
            if (DataBuffer.Num() > 0)
            {
                if (!Client->Recv(DataBuffer.GetData(), DataBuffer.Num(), BytesRead) || BytesRead != DataBuffer.Num())
                {
                    UE_LOG(LogTemp, Warning, TEXT("⚠️ Falha ao ler dados do pacote de %s"), *SocketID);
                    continue;
                }
            }

            // 🔹 Atualiza o timestamp do último pacote recebido
            OwnerServer->LastPacketMutex.Lock();
            OwnerServer->LastPacketTime.Add(SocketID, FPlatformTime::Seconds());
            OwnerServer->LastPacketMutex.Unlock();

            OwnerServer->ClientSocketsMutex.Lock();
            OwnerServer->ClientErrorCount.Remove(SocketID);
            OwnerServer->ClientSocketsMutex.Unlock();

            // 🔹 Processar pacotes usando switch
            AsyncTask(ENamedThreads::GameThread, [this, SocketID, HeadCode, SubCode, DataBuffer]()
                {
                    ProcessReceivedPacket(SocketID, HeadCode, SubCode, DataBuffer);
                });

            UE_LOG(LogTemp, Log, TEXT("📩 Pacote recebido - Head: %d, Sub: %d"), HeadCode, SubCode);
        }

        UE_LOG(LogTemp, Log, TEXT("⏳ Verificando clientes para timeout..."));

        double CurrentTime = FPlatformTime::Seconds();
        TArray<FString> TimeoutClients;

        OwnerServer->LastPacketMutex.Lock();
        for (const auto& Pair : OwnerServer->LastPacketTime)
        {
            FString SocketID = Pair.Key;
            double LastTime = Pair.Value;

            UE_LOG(LogTemp, Log, TEXT("⏳ Cliente %s foi visto pela última vez há %.2f segundos."),
                *SocketID, CurrentTime - LastTime);

            if ((CurrentTime - LastTime) > 15.0)
            {
                UE_LOG(LogTemp, Warning, TEXT("⚠️ Cliente %s excedeu o tempo limite e será removido."), *SocketID);
                TimeoutClients.Add(SocketID);
            }
        }
        OwnerServer->LastPacketMutex.Unlock();

        // 🔹 Removendo clientes inativos
        if (TimeoutClients.Num() > 0)
        {
            OwnerServer->ClientSocketsMutex.Lock();
            for (const FString& SocketID : TimeoutClients)
            {
                UE_LOG(LogTemp, Warning, TEXT("🔴 Tentando remover cliente: %s"), *SocketID);

                FSocket* ClientSocket = OwnerServer->ClientSockets.FindRef(SocketID);
                if (ClientSocket)
                {
                    ClientSocket->Close();
                    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
                }

                OwnerServer->ClientSockets.Remove(SocketID);
                OwnerServer->LastPacketTime.Remove(SocketID);

                AsyncTask(ENamedThreads::GameThread, [this, SocketID]()
                    {
                        OwnerServer->OnFinishConnection.Broadcast(SocketID, TEXT("Timeout"));
                    });

                UE_LOG(LogTemp, Log, TEXT("✅ Cliente %s removido por timeout com sucesso!"), *SocketID);
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

void MuServerReceiveThread::ProcessReceivedPacket(FString SocketID, uint8 HeadCode, uint8 SubCode, const TArray<uint8>& DataBuffer)
{
    if (DataBuffer.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("⚠️ Pacote recebido vazio. Ignorando."));
        return;
    }

    // 🔹 Criar buffer com espaço extra para o null terminator
    TArray<uint8> ProcessedData = DataBuffer;
    ProcessedData.SetNum(DataBuffer.Num() + 1); // Adiciona 1 byte extra para o '\0'

    // 🔹 Adicionar terminador nulo
    ProcessedData[ProcessedData.Num() - 1] = '\0';

    // 🔹 Converter para FString corretamente
    FString DataString = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(ProcessedData.GetData())));

    // 🔹 Remover espaços ou caracteres extras
    DataString.TrimStartAndEndInline();

    UE_LOG(LogTemp, Log, TEXT("📩 Pacote recebido - Head: %d, Sub: %d, Dados: %s"), HeadCode, SubCode, *DataString);

    // 🔹 Enviar para a Game Thread
    AsyncTask(ENamedThreads::GameThread, [this, SocketID, HeadCode, SubCode, DataString]()
        {
            if (OwnerServer)
            {
                OwnerServer->OnPacketReceived.Broadcast(SocketID, HeadCode, SubCode, DataString);
            }
        });
}








// 📌 Método para parar a thread
void MuServerReceiveThread::Stop()
{
    UE_LOG(LogTemp, Log, TEXT("Parando MuServerReceiveThread..."));
    bRunning = false;  // 🚨 Para a execução do loop principal da thread
}