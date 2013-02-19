#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "fmh.h"
#include "iniparser.h"

#if RELEASE_NAME_REQUIRED
#define RELEASE_FILENAME "/tmp/ReleaseFileName"
#endif


static
char *
GetOemKey(char **OemKeyName)
{
    char *start = NULL;
    int keygot = 0;
    char *Key = *OemKeyName;

    if (*OemKeyName == NULL)
	return NULL;
	
    while (*Key != 0)
    {
	if ((*Key == ' ') || (*Key == '\t') || (*Key == ','))
	{
             if (keygot)
	     {
		*OemKeyName = Key+1;
		*Key = 0;
		return start;
	     }
	}
	else
	{
	   if (!keygot)
	   {
	   	keygot = 1;
	   	start = Key;
	   }
	}
	Key++;
    }

    if (*Key == 0)
	*OemKeyName = 0;

    return start;
}



UINT32 
CreateFirmwareInfo(unsigned char *Data, char * BuildFile,
		unsigned char Major,unsigned char Minor,dictionary *d)
{
	unsigned int BuildNo;
	unsigned long ProductId;
	char *ProductName;
	UINT32 len = 0;
	unsigned char *Desc;
	FILE *Buildfd;
#if RELEASE_NAME_REQUIRED
	unsigned char *ProjName;
	FILE *ReleaseFd;
	time_t tim;
	struct tm *tm;
#endif

	/* Oem Extensions */
	char Key[80];			
	char *OemKeys, *OemKeyName,*OemKeyValue,*Next;
	char OemKeyStr[128];


	/* Get BUILD NO if specificed in config, else try to get from BUILDNO */
	BuildNo = iniparser_getlong(d,"GLOBAL:BuildNo",0);
	if (BuildNo == 0)
	{
		/* Open Module File */
		Buildfd = fopen(BuildFile,"rb");
		if (Buildfd == NULL)
		{
			printf("WARNING: Unable to get the Build File of Firmware\n");
			BuildNo = 0;
		}
		else
		{
			if (fscanf(Buildfd,"%u",&BuildNo) != 1)
			{
				printf("WARNING: Unable to get the Build Number of Firmware\n");
				BuildNo = 0;
			}
			fclose(Buildfd);
		}
	}

	len +=sprintf((char *)Data+len,"FW_VERSION=%d.%d.%d\n",Major,Minor,BuildNo);
	len +=sprintf((char *)Data+len,"FW_DATE=%s\n",__DATE__);
	len +=sprintf((char *)Data+len,"FW_BUILDTIME=%s\n",__TIME__);
	
	Desc = (unsigned char *)getenv("FW_DESC");
	if (Desc != NULL)
		len +=sprintf((char *)Data+len,"FW_DESC=%s\n",Desc);
	else
		len +=sprintf((char *)Data+len,"FW_DESC=WARNING : UNOFFICIAL BUILD!! \n");

	ProductId = iniparser_getlong(d,"GLOBAL:ProductId",0);
	if (ProductId != 0)
		len +=sprintf((char *)Data+len,"FW_PRODUCTID=%ld\n",ProductId);
	ProductName = iniparser_getstr(d,"GLOBAL:ProductName");
	if (ProductName != NULL)
		len +=sprintf((char *)Data+len,"FW_PRODUCTNAME=%s\n",ProductName);

	/* Oem Extensions if any */
	OemKeys = iniparser_getstr(d,"GLOBAL:Oemkeys");
	if (OemKeys != NULL)
	{
		strncpy(OemKeyStr,OemKeys,127);
		OemKeyStr[127]=0;
		OemKeyName=&OemKeyStr[0];

		Next = OemKeyName;
		OemKeyName = GetOemKey(&Next);
		while (OemKeyName != NULL)
		{
			sprintf(Key,"GLOBAL:%s",OemKeyName);
			OemKeyValue = iniparser_getstr(d,Key);
				if (OemKeyValue!= NULL)
			len +=sprintf((char *)Data+len,"OEM_%s=%s\n",OemKeyName,OemKeyValue);
			OemKeyName = GetOemKey(&Next);

		}
	}

#if RELEASE_NAME_REQUIRED
	/* For Official Build Create Output file with detailed name */
	unlink(RELEASE_FILENAME);
	
	if (Desc == NULL)
		return len;
	
	/* Get the Project Name */	
	ProjName = (unsigned char *)getenv("RACTRENDS_PROJECT");
	if (ProjName == NULL)
		return len;		

	/* Get Current Date and Time */
	time(&tim);
	tm = localtime(&tim);

	/* Write the name to file */	
	ReleaseFd =fopen(RELEASE_FILENAME,"wt");
	if (ReleaseFd != NULL)
	{
		fprintf(ReleaseFd,"%sfw-%d.%d.%d-%02d%02d%04d.ima",ProjName,
					Major,Minor,BuildNo,
					tm->tm_mon+1,tm->tm_mday,tm->tm_year+1900);
		fclose(ReleaseFd);
	}
#endif
			
	return len;
}

