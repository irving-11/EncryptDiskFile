#include <windows.h>
#include <WinIoCtl.h>
#include <stdio.h>


ULONGLONG *GetFileClusters(PCHAR lpFilename, ULONG *ClusterSize, ULONG *ClusterCount, ULONG *FileSize)
{
	HANDLE hFile = NULL;
	//磁盘基本信息变量定义
	ULONG SectorsPerCluster;
	ULONG BytesPerSector;

	STARTING_VCN_INPUT_BUFFER InVcvBuffer;   //输入的开始vcn号
	PRETRIEVAL_POINTERS_BUFFER pOutFileBuffer;  //输出的结果缓冲区
	ULONG OutFileSize;

	LARGE_INTEGER PreVcn,Lcn;

	ULONGLONG *Clusters = NULL;
	BOOLEAN bDeviceIoResult = FALSE;

	//逻辑路径(卷号)
	char DriverPath[8];
	memset(DriverPath, 0, sizeof(DriverPath));
	DriverPath[0] = lpFilename[0];
	DriverPath[1] = ':';
	DriverPath[2] = 0;
	GetDiskFreeSpace(DriverPath, &SectorsPerCluster, &BytesPerSector, NULL, NULL);
	*ClusterSize = SectorsPerCluster * BytesPerSector;

	//定位文件
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
	//初始化IO相关参数
	DWORD dwRead, Cls, CnCount, r;
	OutFileSize = sizeof(RETRIEVAL_POINTERS_BUFFER) + (*FileSize / *ClusterSize) * sizeof(pOutFileBuffer->Extents);  //个人认为这个结果应该比实际所需的缓冲区大
	pOutFileBuffer = (PRETRIEVAL_POINTERS_BUFFER)malloc(OutFileSize);
	InVcvBuffer.StartingVcn.QuadPart = 0;
	//调用函数后去信息
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

	*ClusterCount = (*FileSize + *ClusterSize -1) / *ClusterSize;   //Cluster 数组的大小，一个簇占一个元素
	Clusters = (ULONGLONG *)malloc(*ClusterCount * sizeof(ULONGLONG));   //分配簇数组空间

	//开始遍历返回结果
	PreVcn = pOutFileBuffer->StartingVcn;
	for(r=0,Cls=0; r<pOutFileBuffer->ExtentCount; r++)   //ExtentCount 簇流的个数(每个簇流中有几个连续的簇)
	{
		Lcn = pOutFileBuffer->Extents[r].Lcn;
		//簇流中连续簇的个数等于 下一个簇流的起始 Vcn 号 减去 上一个 簇流的 起始 Vcn号
		for(CnCount = (ULONG)(pOutFileBuffer->Extents[r].NextVcn.QuadPart - PreVcn.QuadPart); CnCount; CnCount--,Cls++,Lcn.QuadPart++)
		{
			Clusters[Cls] = Lcn.QuadPart;  //保存每个簇流中簇的 Lcn 号
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
	PVOID FileBuff;  //存放从扇区中读取的数据
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
	//打开磁盘卷
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
	//存放读出的文件
	hFile = CreateFile(pDstFileName, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, 0);
	if(hFile == INVALID_HANDLE_VALUE)
	{
		printf("ReadFileFromSectors(): Failed to CreateFile %s ...\n|---errrorcode = %d\n",pDstFileName,GetLastError());
		return 0;
	}


	FileBuff = malloc(ClusterSize);
	//开始读扇区文件内容
	for (r=0; r<ClusterCount; r++, FileSize -= BlockSize)
	{
		offset.QuadPart = ClusterSize * Clusters[r];    //确定每个簇的偏移
		SetFilePointer(hDriver, offset.LowPart, &offset.HighPart, FILE_BEGIN);
		ReadFile(hDriver, FileBuff, ClusterSize, &dwReads, NULL);  //每次读一个簇的大小
		BlockSize = FileSize < ClusterSize ? FileSize : ClusterSize;
		WriteFile(hFile, FileBuff, BlockSize, &dwWrites, NULL);  //将读取的文件保存起来
		//下面能不能加密后再写入磁盘？
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

	//读文件
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


