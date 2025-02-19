#include "MuServerGameInstance.h"
#include "Async/Async.h"

UMuServerGameInstance::UMuServerGameInstance()
{
    ServerSocket = nullptr;
    ReceiveThread = nullptr;
    ServerThread = nullptr;
}

void UMuServerGameInstance::Init()
{
    Super::Init();
}

void UMuServerGameInstance::Shutdown()
{
    StopServer();
    Super::Shutdown();
}

// 📌 Inicia o Servidor
bool UMuServerGameInstance::StartServer(int32 Port)
{
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

    ServerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MuServerSocket"), false);
    if (!ServerSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao criar o socket do servidor."));
        return false;
    }

    TSharedPtr<FInternetAddr> ServerAddress = SocketSubsystem->CreateInternetAddr();
    ServerAddress->SetAnyAddress();
    ServerAddress->SetPort(Port);

    if (!ServerSocket->Bind(*ServerAddress))
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao vincular o servidor à porta %d."), Port);
        return false;
    }

    if (!ServerSocket->Listen(8))  // Permitir até 8 conexões simultâneas
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao colocar o servidor em modo de escuta."));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("Servidor iniciado na porta %d!"), Port);

    ReceiveThread = new MuServerReceiveThread(ServerSocket, this);
    ServerThread = FRunnableThread::Create(ReceiveThread, TEXT("MuServerReceiveThread"));

    return true;
}

void UMuServerGameInstance::StopServer()
{
    UE_LOG(LogTemp, Log, TEXT("Finalizando o servidor..."));

    // 🚨 Primeiro, pare a thread de recebimento
    if (ReceiveThread)
    {
        ReceiveThread->Stop();
        ServerThread->WaitForCompletion(); // Aguarde a thread finalizar
        delete ServerThread;
        ServerThread = nullptr;

        delete ReceiveThread;
        ReceiveThread = nullptr;
    }

    // 🚨 Agora feche os sockets APÓS garantir que a thread parou
    if (ServerSocket)
    {
        ServerSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ServerSocket);
        ServerSocket = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("Servidor desligado com sucesso."));
}

// 📌 Enviar pacotes de forma assíncrona
void UMuServerGameInstance::SendAsyncPacket(FString SocketID, FString PacketData)
{
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, SocketID, PacketData]()
        {
            if (ClientSockets.Contains(SocketID))
            {
                FSocket* ClientSocket = ClientSockets[SocketID];
                if (ClientSocket)
                {
                    FTCHARToUTF8 Converter(*PacketData);
                    int32 BytesSent = 0;
                    bool bSuccess = ClientSocket->Send((uint8*)Converter.Get(), Converter.Length(), BytesSent);

                    if (bSuccess)
                    {
                        UE_LOG(LogTemp, Log, TEXT("Pacote enviado para %s (%d bytes): %s"), *SocketID, BytesSent, *PacketData);
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("Falha ao enviar pacote para %s!"), *SocketID);
                    }
                }
            }
        });
}
