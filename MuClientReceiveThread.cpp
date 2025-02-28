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
        uint8 HeaderBuffer[3];
        int32 BytesRead = 0;

        // 🔹 Ler cabeçalho do pacote
        if (!ClientSocket->Recv(HeaderBuffer, 3, BytesRead) || BytesRead != 3)
        {
            continue;
        }

        uint8 PacketSize = HeaderBuffer[0];
        uint8 HeadCode = HeaderBuffer[1];
        uint8 SubCode = HeaderBuffer[2];

        if (PacketSize < 3 || PacketSize > 255) // Validar tamanho
        {
            continue;
        }

        // 🔹 Criar buffer com espaço extra para o null terminator
        TArray<uint8> DataBuffer;
        DataBuffer.SetNum(PacketSize - 3 + 1); // +1 para adicionar '\0'

        // 🔹 Ler os dados do pacote
        int32 TotalBytesRead = 0;
        while (TotalBytesRead < (PacketSize - 3))
        {
            int32 BytesReadNow = 0;
            if (!ClientSocket->Recv(DataBuffer.GetData() + TotalBytesRead, (PacketSize - 3) - TotalBytesRead, BytesReadNow) || BytesReadNow <= 0)
            {
                break; // Falha na recepção
            }
            TotalBytesRead += BytesReadNow;
        }

        // 🔹 Se não lemos o pacote inteiro, ignoramos
        if (TotalBytesRead != (PacketSize - 3))
        {
            continue;
        }

        // 🔹 Adicionar terminador nulo para evitar lixo na conversão para FString
        DataBuffer[PacketSize - 3] = '\0';

        // 🔹 Converter para FString corretamente
        FString DataString = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(DataBuffer.GetData())));

        // 🔹 Remover espaços extras e caracteres indesejados
        DataString.TrimStartAndEndInline();

        UE_LOG(LogTemp, Log, TEXT("📩 Pacote recebido - Head: %d, Sub: %d, Dados: %s"), HeadCode, SubCode, *DataString);

        // 🔹 Processar o pacote na Game Thread
        if (OwnerClient)
        {
            AsyncTask(ENamedThreads::GameThread, [this, HeadCode, SubCode, DataString]()
                {
                    OwnerClient->OnPacketReceived.Broadcast(HeadCode, SubCode, DataString);
                });
        }
    }

    return 0;
}



