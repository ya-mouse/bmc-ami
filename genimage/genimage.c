#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include <errno.h>

#include "iniparser.h"
#include "fmh.h"

typedef struct sc
{
	char Name[9];
	UINT32 Loc;
	UINT32 Size;
	unsigned char Major;
	unsigned char Minor;
	struct sc *Next;
} SECTION_CHAIN;

unsigned char FirmwareInfo[64*1024];

int  ParseIniFile(char * ini_name);
UINT32  FillModuleInfo(MODULE_INFO *Mod,char *InFile);
int WriteFMHtoFile(FILE *fd,FMH *fmh,ALT_FMH *altfmh,UINT32 Start,
													UINT32 BlockSize);
int WriteModuletoFile(FILE *Outfd,char *InFile, UINT32 Location);
int WriteFirmwareInfo(FILE *Outfd,char *Data,UINT32 Size, UINT32 Location);
int CalculateImageChecksum(FILE* fd,unsigned long ImageHeaderStart);

extern UINT32 CreateFirmwareInfo(unsigned char *Data, char *BuildFile,
			unsigned char Major, unsigned char Minor,dictionary *d);

extern unsigned char  CalculateModule100(unsigned char *Buffer, unsigned long Size);
static char CmdInDir[256]; 
static char CmdOutDir[256];
static char CmdCfgFile[256];

static unsigned long gBlkSize;

/* Assumption: We are using only one FilePath and so we assume that 
 * two parallel calls to Convert2FullPath will not be called. Other
 * wise only the last call will have a valid FilePath and the previous
 * ones will be destroyed */
char FilePath[256];

char *
Convert2FullPath(char *Dir, char *FileName)
{
	int len;
		
	/* No Path or Path is empty */
	if (Dir == NULL)
		return FileName;
	if (strlen(Dir) == 0)
		return FileName;

	/* Check if path is given in FileName */
	if (strchr(FileName,'/') != NULL)
		return FileName;

	/* Copy the directory to path */		
	strcpy(&FilePath[0],Dir);

	/* If last char of path is not /, append a / */
	len = strlen(FilePath);
	if (FilePath[len-1] != '/')
		strcat(FilePath,"/");
	
	/* Append the File Name */
	strcat(FilePath,FileName);
			
	return FilePath;
}

static
void
Usage(char *Prog)
{
	printf("Usage is %s <Args>\n",Prog);
	printf("Args are :\n");
	printf("\t -I Input Files Path\n"); 
	printf("\t -O Output Files Path\n"); 
	printf("\t -C Config File Name\n");
	printf("\n");
}

int 
main(int argc, char * argv[])
{
	int	status;
	int opt;
	char *ProgName;

	/* Skip Program Name */
	ProgName = argv[0];

	/* Initialize with empty values */
	CmdInDir[0] = CmdOutDir[0] = CmdCfgFile[0] = 0;

	while ((opt = getopt(argc, argv, "i:o:c:h")) != -1)
	{
		 switch (opt)
		 {
			case 'i':
				strcpy(CmdInDir, optarg);
				break;
			case 'o':
				strcpy(CmdOutDir, optarg);
				break;
			case 'c':
				strcpy(CmdCfgFile, optarg);
				break;
			default:
				Usage(ProgName);
				exit(1);
		}
	}

	if (CmdCfgFile[0] != 0)
		status = ParseIniFile(CmdCfgFile);
	else
		status = ParseIniFile("genimage.ini");
	
	return status ;
}

void
DisplayFlashMap(SECTION_CHAIN *Chain, UINT32 FlashSize)
{
	UINT32 Loc = 0;
	
	printf("\n");	
	printf("-----------------------------------------------\n");
	printf("             Flash Memory Map                  \n");
	printf("-----------------------------------------------\n");
	while (Chain)
	{
		if (Chain->Loc > Loc)		
			printf("0x%07lX - 0x%07lX : *******FREE*******\n",Loc, Chain->Loc);
		printf("0x%07lX - 0x%07lX : %8s : Ver %d.%d\n",Chain->Loc,Chain->Loc+Chain->Size,Chain->Name, Chain->Major,Chain->Minor);
		
		Loc = Chain->Loc+Chain->Size;			
		Chain = Chain->Next;
	}

	if (Loc < FlashSize)
		printf("0x%07lX - 0x%07lX : *******FREE*******\n",Loc, FlashSize);
	printf("-----------------------------------------------\n");
	return;
}

int
AddToUsedChain(SECTION_CHAIN **pChain,UINT32 Loc, UINT32 Size, char *Name,
				unsigned char Major,unsigned char Minor)
{
	SECTION_CHAIN *newchain, *Prev;
	SECTION_CHAIN *Chain = *pChain;

	/* Allocate memory for new entry */
	newchain = (SECTION_CHAIN *)malloc(sizeof(SECTION_CHAIN));
	if (newchain == NULL)
	{
		printf("INTERNAL ERROR: Unable to allocate memory for Chain\n");
		return 1;			
	}

	/* Fill the Entries */
	newchain->Loc  = Loc;
	newchain->Size = Size;
	newchain->Major = Major;
	newchain->Minor = Minor;
	strncpy(&(newchain->Name[0]),Name,8);
	newchain->Name[8] = 0;
	newchain->Next = NULL;
		
	
	/* If first entry */
	if (Chain == NULL)	
	{
		*pChain = newchain;
		return 0;
	}		

	/* Traverse chain to find the right spot */
	Prev = NULL;
	while (Chain)
	{
		/* Check if overlaps exactly on other section */
		if (Loc == Chain->Loc)
		{
			printf("ERROR: Section %s overlaps with Section %s\n",Name,Chain->Name);
			return 2;			
		}

		/* If our start is below the current entry */
		if (Loc < Chain->Loc)
		{
			/* Check if our section's end overlaps with current section */
			if ((Loc+Size) > Chain->Loc) 				
			{
				printf("ERROR: Section %s overlaps with Section %s\n",Name,Chain->Name);
				return 2;	
			}
			
			/* We got our spot !!*/
			break;			
		}

		/* If our start is above the current entry*/
		if (Loc > Chain->Loc)
		{
			/* Check if our section's begin overlaps with the current section */
			if (Loc < (Chain->Loc + Chain->Size))
			{
				printf("ERROR: Section %s overlaps with Section %s\n",Name,Chain->Name);
				return 2;				
			}
		}

		/* We are well above the current entry. So Continue with the chain traverse */		
		Prev = Chain;
		Chain = Chain->Next;	
	}
	
	/* If new entry is to be added to first of chain */
	if (Prev == NULL)	
	{
		newchain->Next = *pChain;
		*pChain = newchain;
		return 0;
	}

	/* Add to Middle or last of chain */
	newchain->Next = Prev->Next;
	Prev->Next = newchain;
	return 0;
}


int 
ParseIniFile(char* ini_name)
{
	/* INI Parser Related */
	dictionary *d;			/* Dictionary */
	int nsecs,i;			/* Number of Sections */
	char Key[80];			/* To Form the Key to be passed to ini functions */
	char *SecName;			/* Section Name */
	
	/* Global Information */	
	char *OutFile;			/* Output Bin File */
	INT32 FlashSize;			/* Size of Flash */
	INT32 BlockSize;			/* Size of each Flash Block */
	char *InDir;			/* Location of Input Files */
	char *OutDir;			/* Location of Output File */

	/* Output File Creation Related */	
	FILE *Outfd;			/* Output File Descriptor */
	UINT32 Erase;	/* Dummy char used to create the output file */
	SECTION_CHAIN *UsedChain;/* Used for checking overlapping sections */

	/* FMH Related */
	FMH fmh;				/* Flash Module Header */	
	ALT_FMH altfmh,*paltfmh;	/* Alternate Flash Module Header */
	MODULE_INFO mod;		/* Module Information */
	char *InFile;			/* Input File for FMH Section */
	UINT32 InFileSize; /* Size of Section File */
	UINT32 AllocSize;/* Total Allocation Size for this FMH */
	UINT32 MinAllocSize;/* Mininmum Calculated Allocation Size */
	UINT32 FMHLoc;	/* Alternate FMH Location */
	char *LocationStr;		/* Flash Location String */ 
	UINT32 Location;	/* Flash Location Value */

	/* RACTRENDS releted */
	char *BuildFile;		/* Build Number File */
	char *VersionStr;		/* Major and Minor String */
	int FirmwareMajor,FirmwareMinor;
	unsigned char ModuleFormat;
	unsigned long ImageHeaderStart = 0xFFFFFFFF; /* This points to the MODULE FIRMWARE start address */
	int UseFMH=1;

	/*Load the ini File into dictionary*/	
	d = iniparser_load(ini_name);
	if (d==NULL) 
	{
		printf("Error:Cannot parse file [%s]\n", ini_name);
		return 1 ;
	}
#if DEBUG	
	iniparser_dump(d, stderr);
#endif	
	

	/* Get the global information */
	OutFile = iniparser_getstr(d,"GLOBAL:Output");
	if (OutFile == NULL)
	{
		printf("Error: Unable to get Output file name\n");
		iniparser_freedict(d);
		return 1;
	}
	FlashSize = iniparser_getlong(d,"GLOBAL:FlashSize",0);
	if (FlashSize == 0)
	{
		printf("Error: Unable to get Flash Size\n");
		iniparser_freedict(d);
		return 1;
	}	
	BlockSize = iniparser_getlong(d,"GLOBAL:BlockSize",0);
	gBlkSize = BlockSize;
	if (BlockSize == 0)
	{
		printf("Error: Unable to get Block Size\n");
		iniparser_freedict(d);
		return 1;
	}	

	UseFMH = iniparser_getlong(d,"GLOBAL:FMHEnable",1);
	printf("The Image file %s have FMH\n",UseFMH?"will":"will not");

	/* Get the Input and Output Directory Path */
	if (CmdOutDir[0] == 0)
		OutDir = iniparser_getstr(d,"GLOBAL:OutDir");
	else
		OutDir = &CmdOutDir[0];
	
	if (CmdInDir[0] == 0)
		InDir  = iniparser_getstr(d,"GLOBAL:OutDir");
	else
		InDir = &CmdInDir[0];

	/* Convert OutputFile to Full Path */
	OutFile = Convert2FullPath(OutDir,OutFile);

	printf("\nCreating \"%s\" ...\n",OutFile);
	printf("FlashSize = 0x%lx BlockSize = 0x%lx\n",FlashSize,BlockSize);
	
	/* Create and open the output file */
	Outfd = fopen(OutFile,"w+b");
	if (Outfd == NULL)
	{
		printf("Error: Unable to get Create Output file %s\n",OutFile);
		iniparser_freedict(d);
		return 1;
	}
	Erase = 0xFFFFFFFF;
	for (i=0;i<FlashSize/4;i++)
		fwrite(&Erase,4,1,Outfd);
	

	/* Initialize */
	UsedChain = NULL;

	/* Get the number of sections */
	nsecs = iniparser_getnsec(d);
	for(i=0;i<nsecs;i++)
	{
		memset(&mod,0,sizeof(MODULE_INFO));
			
		/* Get the Section Name */
		SecName = iniparser_getsecname(d,i);		
		if (SecName == NULL)
		{
			printf("ERROR: Unable to get Name for Section %d\n",i);
			break;
		}
		
		/* Skip GLOBAL Section. We already processed it */
		if (strcasecmp(SecName,"GLOBAL") == 0)
			continue;
		
		/* Save Section Name in Module Information. Strip to max 8 characters */
		if (strlen(SecName) > 8)
			strncpy((char *)mod.Module_Name,SecName,8);
		else
			strcpy((char *)mod.Module_Name,SecName);		

		/* Get Module Version */
		sprintf(Key,"%s:Major",SecName);
		mod.Module_Ver_Major = iniparser_getint(d,Key,0); 
		sprintf(Key,"%s:Minor",SecName);
		mod.Module_Ver_Minor = iniparser_getint(d,Key,0); 

		/* Get Module Type */
		sprintf(Key,"%s:Type",SecName);
		mod.Module_Type = iniparser_getint(d,Key,0x0000);
		ModuleFormat = (mod.Module_Type >> 8);
		
		/* Check if altFMH to be used */		
		sprintf(Key,"%s:FMHLoc",SecName);
		FMHLoc = iniparser_getlong(d,Key,0);
		/* Get Module Location . Default is 0x40 for normal section 
		 * and one block size for JFFS/JFFS2 */
		sprintf(Key,"%s:Offset",SecName);
		/* If Alternate FMH to be used, offset will be 0 if not specified */
		if (FMHLoc != 0)
			mod.Module_Location = iniparser_getlong(d,Key,0);		
		else
		{
		if ((mod.Module_Type == MODULE_JFFS) || (mod.Module_Type == MODULE_JFFS2)||
		    (mod.Module_Type == MODULE_JFFS_CONFIG) ||
		    (mod.Module_Type == MODULE_JFFS2_CONFIG) ||
		    (ModuleFormat == MODULE_FORMAT_JFFS) ||
		    (ModuleFormat == MODULE_FORMAT_JFFS2))
			mod.Module_Location = iniparser_getlong(d,Key,BlockSize);		
		else
			mod.Module_Location = iniparser_getlong(d,Key,0x40);		
		}
		if (!UseFMH)
			mod.Module_Location = 0;
		

		/* Get Flags */
		sprintf(Key,"%s:BootOS",SecName);
		if (iniparser_getboolean(d,Key,0) == 1)
			mod.Module_Flags |= MODULE_FLAG_BOOTPATH_OS;
		sprintf(Key,"%s:BootDIAG",SecName);
		if (iniparser_getboolean(d,Key,0) == 1)
			mod.Module_Flags |= MODULE_FLAG_BOOTPATH_DIAG;
		sprintf(Key,"%s:BootRECO",SecName);
		if (iniparser_getboolean(d,Key,0) == 1)
			mod.Module_Flags |= MODULE_FLAG_BOOTPATH_RECOVERY;
		sprintf(Key,"%s:CopyToRAM",SecName);
		if (iniparser_getboolean(d,Key,0) == 1)
			mod.Module_Flags |= MODULE_FLAG_COPY_TO_RAM;
		sprintf(Key,"%s:Execute",SecName);
		if (iniparser_getboolean(d,Key,0) == 1)
			mod.Module_Flags |= MODULE_FLAG_EXECUTE;
		sprintf(Key,"%s:Checksum",SecName);
		if (iniparser_getboolean(d,Key,0) == 1)
			mod.Module_Flags |= MODULE_FLAG_VALID_CHECKSUM;
		sprintf(Key,"%s:Compress",SecName);
		mod.Module_Flags |= iniparser_getint(d,Key,0x00) 
							<< MODULE_FLAG_COMPRESSION_LSHIFT;

		/* Get Load Address */
		sprintf(Key,"%s:Load",SecName);
		mod.Module_Load_Address = iniparser_getlong(d,Key,0xFFFFFFFF);
		if (mod.Module_Load_Address == 0xFFFFFFFF)
			mod.Module_Flags &= (~MODULE_FLAG_COPY_TO_RAM);

		/* Get Allocation Size */
		sprintf(Key,"%s:Alloc",SecName);
		AllocSize = iniparser_getlong(d,Key,0);


		if ((mod.Module_Type == MODULE_FMH_FIRMWARE) || 
		    (mod.Module_Type == MODULE_FIRMWARE_1_4))
			AllocSize = BlockSize;
			
		/* Module Firmware is a dummy section. It does not have any data */
		if ((mod.Module_Type != MODULE_FMH_FIRMWARE) &&
		    (mod.Module_Type != MODULE_FIRMWARE_1_4))
		{
			/* Check for mandatory Field - Input File Name */
			sprintf(Key,"%s:File",SecName);
			InFile = iniparser_getstr(d,Key);
			if (InFile == NULL)
			{
				printf("ERROR: Unable to get Input file for Section %s\n",SecName);
				break;
			}
			
			/* Convert InputFile to Full Path */
			InFile = Convert2FullPath(InDir,InFile);
#if DEBUG	
			printf("Input File = [%s]\n",InFile);
#endif			
			
			/* Fill the remaining Fields of MODULE_INFO - Size, Checksum*/
			InFileSize  = FillModuleInfo(&mod,InFile);
			if (InFileSize == 0)
			{
				printf("ERROR: Input file (%s) size for section %s is 0\n",InFile,SecName);
				break;
			}

#if 0			
			/* For JFFS and JFFS2, it should be a multiple of BlockSize */
			/* Otherwise the mtd will be mounted read only */
			if ((mod.Module_Type == MODULE_JFFS) || 
							(mod.Module_Type == MODULE_JFFS2))
			{		
				InFileSize = (InFileSize+BlockSize-1);
				InFileSize = (InFileSize/BlockSize) * BlockSize;
			}
#endif
			
			mod.Module_Size = InFileSize;
			
			/* Calculate the Min Allocation Size */
			MinAllocSize = mod.Module_Location + InFileSize;			
			MinAllocSize += (BlockSize-1);
			MinAllocSize = (MinAllocSize/BlockSize) * BlockSize;

			if (AllocSize != 0)
			{
				if (AllocSize < MinAllocSize)
					printf("WARNING: Section %s size exceeds the Allocated Size\n",SecName);
			}

			if (AllocSize < MinAllocSize)
				AllocSize = MinAllocSize;
			
		}
		else
		{
			/* Check if Firmware version is overridden by Build script */
			VersionStr = getenv("FW_MAJOR");
			if (VersionStr != NULL)
			{
				if (sscanf(VersionStr,"%d",&FirmwareMajor) == 1)
					mod.Module_Ver_Major = (unsigned char)FirmwareMajor;
			}

			VersionStr = getenv("FW_MINOR");
			if (VersionStr != NULL)
			{
				if (sscanf(VersionStr,"%d",&FirmwareMinor) == 1)
					mod.Module_Ver_Minor = (unsigned char)FirmwareMinor;
			}
			
			/* For Ractrends Series Firmware Section has some vendor specific Info */
			BuildFile = Convert2FullPath(OutDir,"BUILDNO");
			mod.Module_Size = CreateFirmwareInfo(&FirmwareInfo[0],BuildFile,
										mod.Module_Ver_Major,mod.Module_Ver_Minor,d);
										
			if (mod.Module_Size > ((64*1024) - 0x40))
				mod.Module_Size = 0;
		}
	
		/* Read Flash Location .It can be either START or END or numeric value */
		Location = 0xFFFFFFFF;
		sprintf(Key,"%s:Locate",SecName);
		LocationStr = iniparser_getstr(d,Key);
		if (LocationStr == NULL)
		{
			printf("ERROR: Unable to get Module Location in Flash for %s\n",SecName);
			break;
		}
		if (strcasecmp(LocationStr,"START") == 0)
				Location = 0;
		if (strcasecmp(LocationStr,"END") == 0)
				Location = FlashSize-AllocSize;
		if (Location == 0xFFFFFFFF)		
		{
			Location = iniparser_getlong(d,Key,0xFFFFFFFF);
			if (Location == 0xFFFFFFFF)
			{
				printf("ERROR: Unable to get Module Location in Flash for %s\n",SecName);
				break;
			}
		}

		/* Validate Location */
		if ((Location > FlashSize) || (Location+AllocSize > FlashSize))
		{
			printf("ERROR: Module Location %ld, Alloc %ld,  > Flash %ld for %s\n",
							Location,AllocSize,FlashSize,SecName);
			break;
		}

		/* Check for overlapping sections and add location and size 
		 * and section name to the chain of used areas */
		if (AddToUsedChain(&UsedChain,Location,AllocSize,SecName,
						mod.Module_Ver_Major,mod.Module_Ver_Minor) != 0)
				break;

		/* Create FMH and Alternate FMH if required */
		CreateFMH(&fmh,AllocSize,&mod,Location+FMHLoc);
		if ((mod.Module_Type == MODULE_FMH_FIRMWARE) ||
		    (mod.Module_Type == MODULE_FIRMWARE_1_4))
		{
			fmh.FMH_Header_Checksum = 0x00;
			ImageHeaderStart = Location;			
		}
		paltfmh = NULL;
		if (FMHLoc != 0)
		{
			printf("%s: Alternate location @ 0x%lx\n",SecName,FMHLoc);
			CreateAlternateFMH(&altfmh,FMHLoc);
			paltfmh = &altfmh;
		}


		if ((mod.Module_Type != MODULE_FMH_FIRMWARE) &&
		    (mod.Module_Type != MODULE_FIRMWARE_1_4))
		{
			if (WriteModuletoFile(Outfd,InFile,Location+mod.Module_Location)!= 0)
			{
				printf("ERROR: Unable to Write Module of Section %s\n",SecName);
				break;
			}
		}
		else
		{
			if (mod.Module_Size > 0)
			{
				if (WriteFirmwareInfo(Outfd,(char *)FirmwareInfo,mod.Module_Size,
												Location+mod.Module_Location)!= 0)
				{
					printf("ERROR: Unable to Write Firmware Info in Section %s\n",SecName);						 break;
				}	
			}
			else
				printf("INFO: No Firmware Information written to FIRMWARE Section\n");	
		}

// Write FMH/ALTFMH after Module is written 
		/* Write the FMH to output file */
		if (UseFMH)
		{
			if (WriteFMHtoFile(Outfd,&fmh,paltfmh,Location,BlockSize) != 0)
			{
				printf("ERROR: Unable to Write FMH of Section %s\n",SecName);
				break;
			}
		}
	
	}
	/* Calculate complete image checksum now and fill in the MODULE INFO checksum field */
	if (ImageHeaderStart != 0xFFFFFFFF)
	{
		if(CalculateImageChecksum(Outfd,ImageHeaderStart) == 0)
			printf("ERROR: Image Checksum calculation failed\n");
	}

	/* Close the Output File */
	fclose(Outfd);
	
	/* Free the Dictionary */
	iniparser_freedict(d);

	/* Display the allocated and free regions of Flash */
	if (i == nsecs)
	{
		printf("Flash Image created Successfully!\n"); 
		DisplayFlashMap(UsedChain,FlashSize);	
		return 0;
	}
	return 1;
}

UINT32 
FillModuleInfo(MODULE_INFO *mod,char *InFile)
{
	struct stat InStat;
	UINT32 FileSize;
    UINT32 i,crc32;
	int fd;
	unsigned char data;


	/* Get the Module File Size */
	if (stat(InFile,&InStat) != 0)
			return 0;	/* Error in stat. Possibly a bad file */
	FileSize = InStat.st_size;

	/* Open the File for crc32 calculation */
	fd = open(InFile,O_RDONLY);
	if (fd < 0)
		return 0;

	/* Read the data and calculate crc32 */	
	BeginCRC32(&crc32);
    for(i = 0; i < FileSize; i++)
	{
		if (read(fd,&data,1) != 1)
		{
			close(fd);
			return 0;
		}
		DoCRC32(&crc32,data);
	}
	EndCRC32(&crc32);
	close(fd);
	
	/* Fill Module Checksum */
	mod->Module_Checksum = crc32;
	
	
	/* Return Module Size */
	return FileSize;			
}

int 
WriteFMHtoFile(FILE *fd,FMH *fmh,ALT_FMH *altfmh,UINT32 Start,
														UINT32 BlockSize)
{
	UINT32 offset = 0;

	if (altfmh != NULL)
		offset = altfmh->FMH_Link_Address;

	/* Write FMH Header */
	if (fseek(fd,Start+offset,SEEK_SET) != 0)
	{
		printf("ERROR:Unable to seek %ld for FMH Header\n",Start+offset);
		return 1;
	}
	if (fwrite((void *)fmh,sizeof(FMH),1,fd) != 1)
	{
		printf("ERROR:Unable to write FMH Header\n");
		return 1;
	}
			
	/* Write Alternate FMH Header */
	if (altfmh != NULL)
	{
		offset = BlockSize - sizeof(ALT_FMH);
		if (fseek(fd,Start+offset,SEEK_SET) != 0)
		{
			printf("ERROR:Unable to seek %ld for ALTFMH Header\n",Start+offset);
			return 1;
		}
		if (fwrite((void *)altfmh,sizeof(ALT_FMH),1,fd) != 1)
		{
			printf("ERROR:Unable to write ALTFMH Header\n");
			return 1;
		}
	}

	return 0;
}

int
WriteModuletoFile(FILE *Outfd,char *InFile, UINT32 Location)
{
	int Infd;
	char data;

	/* Open Module File */
	Infd = open(InFile,O_RDONLY);
	if (Infd < 0)
		return 1;

	/* Seek Output File */
	if (fseek(Outfd,Location,SEEK_SET) != 0)
	{
		printf("ERROR:Unable to seek %ld for Module\n",Location);
		close(Infd);
		return 1;
	}

	while (1)
	{
		if (read(Infd,&data,1) != 1)
				break;
		if (fwrite((void *)&data,1,1,Outfd) != 1)
		{
//			printf("ERROR:Unable to write Module \n");
			return 1;
		}
	}
	return 0;
}

int
WriteFirmwareInfo(FILE *Outfd,char *Data,UINT32 Size, UINT32 Location)
{
	/* Seek Output File */
	if (fseek(Outfd,Location,SEEK_SET) != 0)
	{
		printf("ERROR:Unable to seek %ld for Module\n",Location);
		return 1;
	}

	/* Write Data */	
	if (fwrite((void *)Data,Size,1,Outfd) != 1)
	{
//		printf("ERROR:Unable to write Module\n");
		return 1;
	}
	return 0;
}
int CalculateImageChecksum(FILE* fd,unsigned long ImageHeaderStart)
{
	unsigned long FileSize;
	unsigned long i,crc32;
	unsigned char data;
	unsigned char Buffer[128];
	unsigned char Mod100Checksum = 0;


	/* We want to calculate the checksum until the end of FIRMWARE MODULE section. */
	FileSize = ImageHeaderStart+gBlkSize;
	printf("FileSize = 0x%lX\n",FileSize);

	/* Rewind file to make sure that it's at the begining */
	rewind(fd);
	/* Read the data and calculate crc32 */	
	BeginCRC32(&crc32);
	for(i = 0; i < FileSize; i++)
	{
		if (fread(&data,sizeof(unsigned char),1,fd) != 1)
		{
			printf("ERROR: fread failed while reading image to calculate checksum\n");
			return 0;
		}
		if((i >= (ImageHeaderStart+FMH_MODULE_CHECKSUM_START_OFFSET) && i <= (ImageHeaderStart+FMH_MODULE_CHCKSUM_END_OFFSET)) || (i == (ImageHeaderStart+FMH_FMH_HEADER_CHECKSUM_OFFSET)))
			continue;
		DoCRC32(&crc32,data);
	}
	EndCRC32(&crc32);
	crc32 = host_to_le32(crc32);
	printf("Image checksum is 0x%lX\n",crc32);
	/* Fill this image checksum in Module Checksum field of MODULE FIRMWARE */
	fseek(fd,ImageHeaderStart+FMH_MODULE_CHECKSUM_START_OFFSET,SEEK_SET);
	if(fwrite(&crc32,sizeof(unsigned long),1,fd) == 0)
	if(fwrite(&crc32,sizeof(unsigned long),1,fd) == 0)
	{
		printf("ERROR: fwrite failed while updating image checksum field\n");
		return 0;
	}
	/* Update FMH Modulo100 checksum now */
	/* Again, the assumption here is that the firmware module is at 
	the begining (START) of the image. And also, that there's no alternate
	FMH header for firmware module */
	fseek(fd,ImageHeaderStart,SEEK_SET);
	fread(Buffer,sizeof(unsigned char),sizeof(FMH),fd);
	Mod100Checksum = CalculateModule100(Buffer,sizeof(FMH));
	//printf("Mod100 Checksum is 0x%X\n",Mod100Checksum);

	fseek(fd,ImageHeaderStart+FMH_FMH_HEADER_CHECKSUM_OFFSET,SEEK_SET);
	if(fwrite(&Mod100Checksum,sizeof(unsigned char),1,fd) == 0)
	{
		printf("ERROR: fwrite failed while updating FMH modulo checksum field\n");
		return 0;
	}

	return 1;
}
