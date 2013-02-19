#ifdef __KERNEL__
	#ifdef __UBOOT__
		#include <common.h>
	#else
		#include <linux/kernel.h>
		#include <linux/string.h>
	#endif
#else
	#include <stdio.h>
	#include <string.h>
#endif
#include "fmh.h"
#include "crc32.h"


unsigned char  CalculateModule100(unsigned char *Buffer, UINT32 Size);
static FMH * CheckForNormalFMH(FMH *fmh);
static UINT32  CheckForAlternateFMH(ALT_FMH *altfmh);


unsigned char 
CalculateModule100(unsigned char *Buffer, UINT32 Size)
{
	unsigned char Sum=0;

	while (Size--)
	{
		Sum+=(*Buffer);
		Buffer++;
	}	

	return (~Sum)+1;
}

static
unsigned char 
ValidateModule100(unsigned char *Buffer, UINT32 Size)
{
	unsigned char Sum=0;

	while (Size--)
	{
		Sum+=(*Buffer);
		Buffer++;
	}

	return Sum;
}

static
FMH *
CheckForNormalFMH(FMH *fmh)
{
	if (strncmp((char *)fmh->FMH_Signature,FMH_SIGNATURE,sizeof(FMH_SIGNATURE)-1) != 0)
			return NULL;

	if (le16_to_host(fmh->FMH_End_Signature) != FMH_END_SIGNATURE)
			return NULL;

	if (ValidateModule100((unsigned char *)fmh,sizeof(FMH)) != 0)
			return NULL;
	
	return fmh;
			
}

static
UINT32 
CheckForAlternateFMH(ALT_FMH *altfmh)
{

	if (strncmp((char *)altfmh->FMH_Signature,FMH_SIGNATURE,sizeof(FMH_SIGNATURE)-1) != 0)
			return INVALID_FMH_OFFSET;

	if (le16_to_host(altfmh->FMH_End_Signature) != FMH_END_SIGNATURE)
			return INVALID_FMH_OFFSET;

	if (ValidateModule100((unsigned char *)altfmh,sizeof(ALT_FMH)) != 0)
			return INVALID_FMH_OFFSET;
	
	return le32_to_host(altfmh->FMH_Link_Address);

}

FMH *
ScanforFMH(unsigned char *SectorAddr, UINT32 SectorSize)
{
	FMH *fmh;
	ALT_FMH *altfmh;
	UINT32 FMH_Offset;

	/* Check if Normal FMH is found */
	fmh = (FMH *)SectorAddr;
	fmh = CheckForNormalFMH(fmh);
	if (fmh != NULL)
		return fmh;

	/* If Normal FMH is not found, check for alternate FMH */
	altfmh = (ALT_FMH *)(SectorAddr+SectorSize - sizeof(ALT_FMH));
	FMH_Offset = CheckForAlternateFMH(altfmh);
	if (FMH_Offset == INVALID_FMH_OFFSET)
		return NULL;
	fmh = (FMH *)(SectorAddr +FMH_Offset);
	
	/* If alternate FMH is found, validate it */
	fmh = CheckForNormalFMH(fmh);
	return fmh;
}

void
CreateFMH(FMH *fmh,UINT32 AllocatedSize, MODULE_INFO *mod, UINT32 Location)
{
	/* Clear the Structure */	
	memset((void *)fmh,0,sizeof(FMH));

	/* Copy the module information */
	memcpy((void *)&(fmh->Module_Info),(void *)mod,sizeof(MODULE_INFO));
					
	/* Fill the FMH Fields */		
	strncpy((char *)fmh->FMH_Signature,FMH_SIGNATURE,sizeof(FMH_SIGNATURE)-1);
	fmh->FMH_Ver_Major 		= FMH_MAJOR;
	fmh->FMH_Ver_Minor 		= FMH_MINOR;
	fmh->FMH_Size	   		= FMH_SIZE;
	fmh->FMH_End_Signature	= host_to_le16(FMH_END_SIGNATURE);
	
	fmh->FMH_AllocatedSize	= host_to_le32(AllocatedSize);
	fmh-> FMH_Location = host_to_le32(Location);

	/*Calculate Header Checksum*/
	fmh->FMH_Header_Checksum = CalculateModule100((unsigned char *)fmh,sizeof(FMH));
		
	return;
}

void
CreateAlternateFMH(ALT_FMH *altfmh,UINT32 FMH_Offset) 
{
	/* Clear the Structure */	
	memset((void *)altfmh,0,sizeof(ALT_FMH));
					
	/* Fill the FMH Fields */		
	strncpy((char *)altfmh->FMH_Signature,FMH_SIGNATURE,sizeof(FMH_SIGNATURE)-1);
	altfmh->FMH_End_Signature	= host_to_le16(FMH_END_SIGNATURE);
	
	altfmh->FMH_Link_Address	= host_to_le32(FMH_Offset);

	/*Calculate Header Checksum*/
	altfmh->FMH_Header_Checksum = CalculateModule100((unsigned char *)altfmh,
										sizeof(ALT_FMH));
	return;
}



UINT32
CalculateCRC32(unsigned char *Buffer, UINT32 Size)
{
    UINT32 i,crc32 = 0xFFFFFFFF;

	/* Read the data and calculate crc32 */	
    for(i = 0; i < Size; i++)
		crc32 = ((crc32) >> 8) ^ CrcLookUpTable[(Buffer[i]) 
								^ ((crc32) & 0x000000FF)];
	return ~crc32;
}

void
BeginCRC32(UINT32 *crc32)
{
	*crc32 = 0xFFFFFFFF;
	return;
}

void
DoCRC32(UINT32 *crc32, unsigned char Data)
{
	*crc32=((*crc32) >> 8) ^ CrcLookUpTable[Data ^ ((*crc32) & 0x000000FF)];
	return;
}

void
EndCRC32(UINT32 *crc32)
{
	*crc32 = ~(*crc32);
	return;
}

