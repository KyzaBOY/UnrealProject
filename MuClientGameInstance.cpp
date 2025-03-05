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
    UE_LOG(LogTemp, Log, TEXT("📌 Iniciando Shutdown do GameInstance."));

    // 📌 Para a thread primeiro
    if (ReceiveTask)
    {
        UE_LOG(LogTemp, Log, TEXT("🔹 Parando thread de recebimento..."));
        ReceiveTask->StopThread();
    }

    // 📌 Mata a thread de recebimento corretamente
    if (ReceiveThread)
    {
        UE_LOG(LogTemp, Log, TEXT("🔹 Matando thread..."));
        ReceiveThread->Kill(true);
        delete ReceiveThread;
        ReceiveThread = nullptr;
    }

    // 📌 Fecha e destrói o socket por último
    if (ClientSocket)
    {
        UE_LOG(LogTemp, Log, TEXT("🔹 Fechando socket..."));
        ClientSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
        ClientSocket = nullptr;
    }

    // 📌 Libera a memória da task (evita dangling pointer)
    if (ReceiveTask)
    {
        UE_LOG(LogTemp, Log, TEXT("🔹 Deletando task de recebimento..."));
        delete ReceiveTask;
        ReceiveTask = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("✅ Cliente finalizado com sucesso."));
    Super::Shutdown();
}


void UMuClientGameInstance::GracefulShutdown()
{
    if (ReceiveTask)
    {
        ReceiveTask->Stop(); // 📌 Para a thread de recebimento
        FPlatformProcess::Sleep(0.2f); // 🔹 Aguarde um tempo para evitar crash
    }

    if (ReceiveThread)
    {
        ReceiveThread->Kill(true);
        delete ReceiveThread;
        ReceiveThread = nullptr;
    }

    if (ClientSocket)
    {
        ClientSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
        ClientSocket = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("✅ Conexão finalizada com sucesso."));
}

// 📌 Conectar ao Servidor
bool UMuClientGameInstance::ConnectToServer(FString IP, int32 Port)
{
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    ClientSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MuClientSocket"), false);

    if (!ClientSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao criar o socket."));
        return false;
    }

    TSharedPtr<FInternetAddr> ServerAddress = SocketSubsystem->CreateInternetAddr();
    bool bIsValid;
    ServerAddress->SetIp(*IP, bIsValid);
    ServerAddress->SetPort(Port);

    if (!bIsValid || !ClientSocket->Connect(*ServerAddress))
    {
        UE_LOG(LogTemp, Error, TEXT("❌ Falha ao conectar ao servidor %s:%d"), *IP, Port);
        return false;
    }

    ReceiveTask = new MuClientReceiveThread(ClientSocket, this);
    ReceiveThread = FRunnableThread::Create(ReceiveTask, TEXT("MuClientReceiveThread"));

    UE_LOG(LogTemp, Log, TEXT("✅ Conectado ao servidor %s:%d"), *IP, Port);
    return true;
}

// 📌 Desconectar do Servidor
void UMuClientGameInstance::DisconnectFromServer()
{
    GracefulShutdown();
}

// 📌 Enviar pacotes de forma assíncrona
void UMuClientGameInstance::SendAsyncPacket(FString Data)
{
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Data]()
        {
            if (!ClientSocket)
            {
                UE_LOG(LogTemp, Error, TEXT("❌ Socket não conectado!"));
                return;
            }

            FTCHARToUTF8 Converter(*Data);
            int32 DataSize = Converter.Length();

            // Criando buffer de envio
            TArray<uint8> Buffer;
            Buffer.SetNum(2 + DataSize);

            // Inserindo o tamanho do pacote nos primeiros 2 bytes
            uint16 PacketSize = static_cast<uint16>(DataSize);
            FMemory::Memcpy(Buffer.GetData(), &PacketSize, sizeof(uint16));

            // Copiando os dados para o buffer
            FMemory::Memcpy(Buffer.GetData() + 2, Converter.Get(), DataSize);

            // Enviar o pacote completo
            int32 BytesSent = 0;
            bool bSuccess = ClientSocket->Send(Buffer.GetData(), Buffer.Num(), BytesSent);

            if (bSuccess)
            {
                UE_LOG(LogTemp, Log, TEXT("✅ Pacote enviado (%d bytes): %s"), BytesSent, *Data);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("❌ Falha ao enviar pacote!"));
            }
        });
}

void UMuClientGameInstance::SendPacket(uint8 HeadCode, uint8 SubCode, const FString& DataString)
{
    TArray<uint8> Data;
    FTCHARToUTF8 Converter(*DataString);
    Data.Append((uint8*)Converter.Get(), Converter.Length());

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, HeadCode, SubCode, Data]()
        {
            if (!ClientSocket || ClientSocket->GetConnectionState() != ESocketConnectionState::SCS_Connected)
            {
                UE_LOG(LogTemp, Error, TEXT("❌ Socket não conectado!"));
                return;
            }

            int32 PacketSize = 3 + Data.Num();
            TArray<uint8> Buffer;
            Buffer.SetNum(PacketSize);

            Buffer[0] = PacketSize;
            Buffer[1] = HeadCode;
            Buffer[2] = SubCode;

            if (Data.Num() > 0)
            {
                FMemory::Memcpy(Buffer.GetData() + 3, Data.GetData(), Data.Num());
            }

            int32 BytesSent = 0;
            bool bSuccess = ClientSocket->Send(Buffer.GetData(), Buffer.Num(), BytesSent);

            if (bSuccess)
            {
                UE_LOG(LogTemp, Log, TEXT("✅ Pacote enviado - HeadCode: %d, SubCode: %d (%d bytes)"), HeadCode, SubCode, BytesSent);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("❌ Falha ao enviar pacote!"));
            }
        });
}






FString UMuClientGameInstance::BytesToString(const TArray<uint8>& DataBuffer)
{
    return FString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(DataBuffer.GetData())));
}

int32 UMuClientGameInstance::BytesToInt(const TArray<uint8>& DataBuffer)
{
    int32 Value = 0;
    if (DataBuffer.Num() >= 4)
    {
        FMemory::Memcpy(&Value, DataBuffer.GetData(), sizeof(int32));
    }
    return Value;
}