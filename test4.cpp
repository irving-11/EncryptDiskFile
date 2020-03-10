#include <windows.h>
#include <WinIoCtl.h>
#include <stdio.h>


ULONGLONG *GetFileClusters(PCHAR lpFilename, ULONG *ClusterSize, ULONG *ClusterCount, ULONG *FileSize)
{
	HANDLE hFile = NULL;
	//���̻�����Ϣ��������
	ULONG SectorsPerCluster;
	ULONG BytesPerSector;

	STARTING_VCN_INPUT_BUFFER InVcvBuffer;   //����Ŀ�ʼvcn��
	PRETRIEVAL_POINTERS_BUFFER pOutFileBuffer;  //����Ľ��������
	ULONG OutFileSize;

	LARGE_INTEGER PreVcn,Lcn;

	ULONGLONG *Clusters = NULL;
	BOOLEAN bDeviceIoResult = FALSE;

	//�߼�·��(���)
	char DriverPath[8];
	memset(DriverPath, 0, sizeof(DriverPath));
	DriverPath[0] = lpFilename[0];
	DriverPath[1] = ':';
	DriverPath[2] = 0;
	GetDiskFreeSpace(DriverPath, &SectorsPerCluster, &BytesPerSector, NULL, NULL);
	*ClusterSize = SectorsPerCluster * BytesPerSector;

	//��λ�ļ�
	hFile = CreateFile(lpFilename,
					//GENERIC_READ | GENERIC_WRITE,
					FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
					NULL,
					OPEN_EXISTING,
					0,
					0);
	if(hFile == INVALID_HANDLE_VALUE)
	{
		printf("GetFileClusters(): Failed to open file %s ...\n",lpFilename);
		return 0;
	}
	*FileSize = GetFileSize(hFile, NULL);
	//��ʼ��IO��ز���
	DWORD dwRead, Cls, CnCount, r;
	OutFileSize = sizeof(RETRIEVAL_POINTERS_BUFFER) + (*FileSize / *ClusterSize) * sizeof(pOutFileBuffer->Extents);  //������Ϊ������Ӧ�ñ�ʵ������Ļ�������
	pOutFileBuffer = (PRETRIEVAL_POINTERS_BUFFER)malloc(OutFileSize);
	InVcvBuffer.StartingVcn.QuadPart = 0;
	//���ú�����ȥ��Ϣ
	bDeviceIoResult = DeviceIoControl(hFile,
								FSCTL_GET_RETRIEVAL_POINTERS,
								&InVcvBuffer,
								sizeof(InVcvBuffer),
								pOutFileBuffer,
								OutFileSize,
								&dwRead,
								NULL);
	if(!bDeviceIoResult)
	{
		printf("GetFileClusters(): Failed to call DeviceIocontrol with paramter FSCTL_GET_RETRIEVAL_POINTERS...\n|---errorcode = %d\n",GetLastError());
		CloseHandle(hFile);
		return 0;
	}

	*ClusterCount = (*FileSize + *ClusterSize -1) / *ClusterSize;   //Cluster ����Ĵ�С��һ����ռһ��Ԫ��
	Clusters = (ULONGLONG *)malloc(*ClusterCount * sizeof(ULONGLONG));   //���������ռ�

	//��ʼ�������ؽ��
	PreVcn = pOutFileBuffer->StartingVcn;
	for(r=0,Cls=0; r<pOutFileBuffer->ExtentCount; r++)   //ExtentCount �����ĸ���(ÿ���������м��������Ĵ�)
	{
		Lcn = pOutFileBuffer->Extents[r].Lcn;
		//�����������صĸ������� ��һ����������ʼ Vcn �� ��ȥ ��һ�� ������ ��ʼ Vcn��
		for(CnCount = (ULONG)(pOutFileBuffer->Extents[r].NextVcn.QuadPart - PreVcn.QuadPart); CnCount; CnCount--,Cls++,Lcn.QuadPart++)
		{
			Clusters[Cls] = Lcn.QuadPart;  //����ÿ�������дص� Lcn ��
		}

		PreVcn = pOutFileBuffer->Extents[r].NextVcn;
	}

	free(pOutFileBuffer);
	CloseHandle(hFile);
	return Clusters;
}

int ReadFileFromSectors(PCHAR lpFileName, PCHAR pDstFileName)
{
	ULONG ClusterSize, BlockSize, ClusterCount, FileSize;
	ULONGLONG *Clusters = NULL;
	DWORD dwReads,dwWrites;
	HANDLE hDriver, hFile;
	ULONG SectorsPerCluster, BytesPerSector, r;
	PVOID FileBuff;  //��Ŵ������ж�ȡ������
	LARGE_INTEGER offset;
	char DrivePath[10];
	Clusters = GetFileClusters(lpFileName, &ClusterSize, &ClusterCount, &FileSize);
	if(Clusters == NULL)
	{
		printf("ReadFileFromSectors(): Failed to GetFileClusters ...\n|---errrorcode = %d\n",GetLastError());
		return 0;
	}
	DrivePath[0] = '\\';
	DrivePath[1] = '\\';
	DrivePath[2] = '.';
	DrivePath[3] = '\\';
	DrivePath[4] = lpFileName[0];
	DrivePath[5] = ':';
	DrivePath[6] = 0;
	//�򿪴��̾�
	hDriver = CreateFile(DrivePath,
					GENERIC_READ,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					0,
					NULL);
	if(hDriver == INVALID_HANDLE_VALUE)
	{
		printf("ReadFileFromSectors(): Failed to CreateFile %s ...\n|---errrorcode = %d\n",DrivePath,GetLastError());
		return 0;
	}
	//��Ŷ������ļ�
	hFile = CreateFile(pDstFileName, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, 0);
	if(hFile == INVALID_HANDLE_VALUE)
	{
		printf("ReadFileFromSectors(): Failed to CreateFile %s ...\n|---errrorcode = %d\n",pDstFileName,GetLastError());
		return 0;
	}


	FileBuff = malloc(ClusterSize);
	//��ʼ�������ļ�����
	for (r=0; r<ClusterCount; r++, FileSize -= BlockSize)
	{
		offset.QuadPart = ClusterSize * Clusters[r];    //ȷ��ÿ���ص�ƫ��
		SetFilePointer(hDriver, offset.LowPart, &offset.HighPart, FILE_BEGIN);
		ReadFile(hDriver, FileBuff, ClusterSize, &dwReads, NULL);  //ÿ�ζ�һ���صĴ�С
		BlockSize = FileSize < ClusterSize ? FileSize : ClusterSize;
		WriteFile(hFile, FileBuff, BlockSize, &dwWrites, NULL);  //����ȡ���ļ���������
		//�����ܲ��ܼ��ܺ���д����̣�
	}

	free(FileBuff);
	free(Clusters);
	CloseHandle(hFile);
	CloseHandle(hDriver);
}

//--------------------------------------------------------------------
//
// Usage
//
// Tell user how to use the program.
//
//--------------------------------------------------------------------
int Usage( CHAR *ProgramName )
{
	printf("\nusage: %s -f srcfile dstfile ...\n", ProgramName );
	return -1;
}

int main(int argc, char *argv[])
{


	if(argc != 4)
	{
		Usage(argv[0]);
		return 0;
	}

	//���ļ�
	if(strcmp(argv[1], "-f") == 0)
	{
		ReadFileFromSectors(argv[2], argv[3]);
	}
	else
	{
		Usage(argv[0]);
	}


	for(int i=0;i<sizeof(Clusters);i++){
        printf("%02x\n",Clusters[i]);
	}


	system("pause");
	return 1;
}


