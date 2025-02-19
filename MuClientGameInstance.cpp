#include "MuClientGameInstance.h"
#include "Async/Async.h"

UMuClientGameInstance::UMuClientGameInstance()
{
    ClientSocket = nullptr;
    ReceiveThread = nullptr;
    ReceiveTask = nullptr;
}

void UMuClientGameInstance::Init()
{
    Super::Init();
}

void UMuClientGameInstance::Shutdown()
{
    DisconnectFromServer();
    Super::Shutdown();
}

// 📌 Conectar ao Servidor
bool UMuClientGameInstance::ConnectToServer(FString IP, int32 Port)
{
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

    ClientSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MuClientSocket"), false);
    if (!ClientSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao criar o socket do cliente."));
        return false;
    }

    TSharedPtr<FInternetAddr> ServerAddress = SocketSubsystem->CreateInternetAddr();
    bool bIsValid;
    ServerAddress->SetIp(*IP, bIsValid);
    ServerAddress->SetPort(Port);

    if (!bIsValid)
    {
        UE_LOG(LogTemp, Error, TEXT("Endereço IP inválido!"));
        return false;
    }

    if (!ClientSocket->Connect(*ServerAddress))
    {
        UE_LOG(LogTemp, Error, TEXT("Falha ao conectar ao servidor %s:%d"), *IP, Port);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("Conectado ao servidor %s:%d!"), *IP, Port);

    // Criar e iniciar a thread de recebimento
    ReceiveTask = new MuClientReceiveThread(ClientSocket, this);
    ReceiveThread = FRunnableThread::Create(ReceiveTask, TEXT("MuClientReceiveThread"));

    return true;
}

// 📌 Desconectar do Servidor
void UMuClientGameInstance::DisconnectFromServer()
{
    if (ClientSocket)
    {
        ClientSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
        ClientSocket = nullptr;
    }

    if (ReceiveThread)
    {
        ReceiveThread->Kill(true);
        delete ReceiveThread;
        ReceiveThread = nullptr;
    }

    if (ReceiveTask)
    {
        delete ReceiveTask;
        ReceiveTask = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("Desconectado do servidor."));
}

// 📌 Enviar pacotes de forma assíncrona
void UMuClientGameInstance::SendAsyncPacket(FString Data)
{
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Data]()
        {
            if (!ClientSocket)
            {
                UE_LOG(LogTemp, Error, TEXT("Socket não conectado!"));
                return;
            }

            FString PacketData = Data + TEXT("\nEND");
            FTCHARToUTF8 Converter(*PacketData);
            int32 BytesSent = 0;
            bool bSuccess = ClientSocket->Send((uint8*)Converter.Get(), Converter.Length(), BytesSent);

            if (bSuccess)
            {
                UE_LOG(LogTemp, Log, TEXT("Pacote enviado (%d bytes): %s"), BytesSent, *PacketData);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Falha ao enviar pacote!"));
            }
        });
}
