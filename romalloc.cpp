// TestPrfCnv.cpp : Defines the entry point for the console application.
//

#define VERSION_STRING "Rom_Alloc V2.1"

#include "stdafx.h"
#include <stdlib.h> 
#include <string.h>
#pragma warning(disable: 4996)
#pragma warning(disable: 4267)


#ifndef BYTE
#define BYTE unsigned char
#endif

#define MAXMACRO 256
#define MAXNAME	32
#define MAXELEM	32
#define STRMAX	128
#define MAXTEST 64;

#define DNLD_CMD "FR%08X\n"

typedef struct {
	char name[MAXNAME];
	BYTE offs;
	BYTE siz;
} tPrfElem;

typedef struct {
	char name[MAXNAME];
	int val;
} tMacroField;

typedef struct {
	unsigned absSet	  : 1;
	unsigned dataFile : 1;
	unsigned headerFile : 1;
	unsigned inProfile : 1;
	unsigned profileDone : 1;
	unsigned cmdDnLd : 1;
} tBitFlags;
static tBitFlags _flags;

static tMacroField aMacros[MAXMACRO];
static char errMsg[STRMAX*4];

static tPrfElem aPrfItem[MAXELEM];
static FILE *fi;
static FILE *fo;
static FILE *fHeader;

static unsigned _nFields;
static unsigned _nMacros;
static unsigned _nItemSiz;
static unsigned _nTests;
static unsigned _strnum;

static char _newSectName[MAXNAME];
static unsigned _romAddr;
static unsigned short _checkSum;
static BYTE _pad;
//-----------------------------------
const char *aReservedWords[] = { "Absolute", "Align", "Padding", "Declare", "DataFile", "HeaderFile","Repeat"};
typedef enum                   { RSWD_ABSOL,RSWD_ALIGN,RSWD_PADDI,RSWD_DECLA,RSWD_DATAF, RSWD_HEADE, RSWD_REPE, RSWD_NONE };
//-----------------------------------
const char *skipWhiteSpaces(const char *s);
int getLiteralSring(const char *s, char *d, int siz);
unsigned iniNameVal(const char *name, const char *buf, unsigned *pVal);
unsigned iniNameLiteralStr(const char *name, const char *s, char *d, int maxSiz);
int parseDataLine(const char *s);
int getValidInputString(char *d, int siz);
int buildProfile(void);
int getNum(const char *s, unsigned *pVal);
int getNumStop(const char *s, unsigned *pVal, char **stop);

//-----------------------------------
int exitWithError(void)
{
	puts(errMsg);
	sprintf(errMsg, " at line # %d\n", _strnum);
	puts(errMsg);

	if (NULL != fi)
		fclose(fi);
	if (NULL != fo)
		fclose(fo);
	if (NULL != fHeader)
		fclose(fHeader);
	getchar();
	exit(1);
	return 1;
}
//-----------------------------------
void outDataFile(BYTE *s, int len)
{
	char buf[20];
	int i;
	char *cpO;


	if (!(_flags.dataFile))
	{ 
		strcpy(errMsg, "no data file.");
		exitWithError();
	}
	if (!(_flags.cmdDnLd))
	{
		sprintf(buf, "FR%08X\n", _romAddr);
		fputs(buf, fo);
		_flags.cmdDnLd = 1;
	}

	_romAddr += len;
	while (len)
	{
		if (len > 8)
			i = 8; // 8 bytes in line.
		else
			i = len;
		len -= i;
		for (cpO = buf; i; i--)
		{
			_checkSum += *s;
			cpO += sprintf(cpO, "%02X", *s);
			s++;
		}
		strcpy(cpO, "\n");
		fputs(buf, fo);
	}
}
//-----------------------------------
void closeDataFile(void)
{
	char buf[32];

	if (_flags.dataFile)
	{
		fputs("---end---\n", fo);
		sprintf(buf, "CS%04X\n", _checkSum);
		fputs(buf, fo);
		fclose(fo);
	}
}
//-----------------------------------
void padOutFile(int len)
{
	BYTE buf[256];
	memset(buf, _pad, len);
	outDataFile(buf, len);
}
//-----------------------------------
void dataRepeat(BYTE rpt, const char *s)
{
	BYTE buf[256];
	unsigned val;
	const char *cp;

	cp = strchr(s, ',');
	if (NULL == cp)
	{
		strcpy(errMsg, s);
		strcat(errMsg, " ???");
		exitWithError();
	}
	cp = skipWhiteSpaces(cp + 1);
	if (NULL == cp)
	{
		strcpy(errMsg, s);
		strcat(errMsg, " ???");
		exitWithError();
	}
	getNum(cp, &val);
	memset(buf, val, rpt);
	outDataFile(buf, rpt);
}
//-----------------------------------
void padAlign(unsigned nAlign)
{
	unsigned ui;
	unsigned bitmask;

	bitmask = 0;
	for (ui = 0; ui < 8; ui++)
	{
		if (nAlign & (1 << ui))
			break;
		bitmask |= 1 << ui;
	}
	if (ui > 8)
	{
		sprintf(errMsg, "align too big: %X", nAlign);
		exitWithError();
	}

	if (_romAddr & bitmask)
		padOutFile((1 << ui) - (_romAddr & bitmask));
}
//-----------------------------------

#define N_ELEM(arrName) ( sizeof(arrName) / sizeof(arrName[0]) )
int isReservedWord(const char *s)
{
	int i;
	unsigned val;
	int len;
	char iBuf[STRMAX];
	char oBuf[STRMAX];

	if(*s == '[')
		return 0;

	for (i = 0; i < N_ELEM(aReservedWords); i++)
		if (!strncmp(aReservedWords[i], s, 5))
			break;
	switch (i) {
	  case RSWD_ABSOL:
		  iniNameVal(aReservedWords[i], s, &val);
		  if (_flags.absSet)
		  {
			  if (val < _romAddr || (val - _romAddr > 0x100))
			  {
				  sprintf(errMsg, "bad absolute: %X", val);
				  exitWithError();
			  }
			  padOutFile(val - _romAddr);
		  }
		  else
		  {
			  _romAddr = val;
			  _flags.absSet = 1;
		  }
		  break;
	  case RSWD_ALIGN:
		  iniNameVal(aReservedWords[i], s, &val);
		  padAlign(val);
		  break;
	  case RSWD_PADDI:
		  iniNameVal(aReservedWords[i], s, &val);
		  _pad = val;
		  break;
	  case RSWD_DATAF:
		  if (iniNameLiteralStr(aReservedWords[i], s, iBuf, STRMAX)) {
			  if (!(_flags.dataFile)) {
				  fo = fopen(iBuf, "w");
				  if (NULL == fo)  {
					  strcpy(errMsg, iBuf);
					  strcat(errMsg, ": error open for write.");
					  return exitWithError();
				  }
				  else
				  {
					  _flags.dataFile = 1;
					  strcpy(oBuf, "Created data file: ");
					  strcat(oBuf, iBuf);
					  puts(oBuf);
				  }
			  }
			  else
			  {
				  strcpy(errMsg, s);
				  exitWithError();
			  }
		  }
		  break;
	  case RSWD_HEADE:
		  if (iniNameLiteralStr(aReservedWords[i], s, iBuf, STRMAX)) {
			  if (!(_flags.headerFile)) {
				  fHeader = fopen(iBuf, "w");
				  if (NULL == fHeader)  {
					  strcpy(errMsg, iBuf);
					  strcat(errMsg, ": error open for write.");
					  return exitWithError();
				  }
				  else
				  {
					  _flags.headerFile = 1;
					  strcpy(oBuf, "Created 'C' header file: ");
					  strcat(oBuf, iBuf);
					  puts(oBuf);
				  }
			  }
			  else
			  {
				  strcpy(errMsg, s);
				  exitWithError();
			  }
		  }
		  break;
	  case RSWD_DECLA:
		  len = iniNameLiteralStr(aReservedWords[i], s, iBuf, STRMAX);
		  if (len)
		  {
			  iBuf[len] = '\0';
			  sprintf(oBuf, "#define %s 0x%X\n", iBuf, _romAddr);
			  if (_flags.headerFile)
				  fputs(oBuf, fHeader);
			  else
			  {
				  strcpy(errMsg, s);
				  strcpy(errMsg, ": header not opened");
				  exitWithError();
			  }
			  if (!strcmp(iBuf, "PROFILE_ADDR"))
			  {
				  if (_flags.inProfile || _flags.profileDone)
				  {
					  strcpy(errMsg, s);
					  strcpy(errMsg, ": profile already done.");
					  exitWithError();
				  }
				  else
				  {
					  if (buildProfile())
						  exitWithError();
	  				  _flags.profileDone = 1;
				  }
			  }
		  }
		  break;
	  case RSWD_REPE:
		  iniNameVal(aReservedWords[i], s, &val);
		  dataRepeat(val,s);
		  break;
	  default:
		  parseDataLine(s);
		  return 1;
	}
	return 0;
}
/*-----------------------------------


-----------------------------------*/
int isQuoteStr(char *s) // ret len of quote str, or 0.
{
  char *cpE;

  if('"' != *s)
	 return 0;
// run to end of quote string
  cpE = s+1;
  while('"' != *cpE) {
	if(!*cpE)
	  return 0;
	cpE++;
  }
  return (cpE - s) + 1; // including "", so empty "" return 2;
}
//-----------------------------------
int delimStr(char *s, char delim, char **stop)
{
  while(*s)
  {
    if('"' == *s)
	  s += isQuoteStr(s);
	else if(delim != *s)
	  s++;
  }
  *stop = s;
  return 0;
}
//-----------------------------------
int parseDataLine(const char *s)
{
	BYTE buf[64];
	unsigned val;
	int len;
	int cnt;
	char *stp;

	cnt = 0;
	while (1)
	{
		s = skipWhiteSpaces(s);
		if(NULL==s || '\0' == *s || ';' == *s)
			break;
		if(*s == ',')
			s = skipWhiteSpaces(s+1);
		if(getNumStop(s, &val, &stp))
 		{
			buf[cnt] = val;
			cnt++;
			s = stp;
		}
		else if(len = isQuoteStr((char *)s))			// ret len of quote str, or 0.
		{
			if(len > 2)
			{
				memcpy(buf + cnt, s+1, len-2);
				cnt += len-2;
			}
			s+=len+1;
		}
		else
			break;
	}
	if(cnt)
		outDataFile(buf, cnt);
	return 0;
}
//-----------------------------------
int iniOpenSec(char *sec)
{
	char buf[STRMAX];

	if (fi == NULL)
		return 1; // file not open
	rewind(fi);
	_strnum = 0;
	while (1)
	{
		if (0 == getValidInputString(buf, STRMAX))
			return 2; // not found;
		if (buf[0] != '[')
			continue;
		if (!strncmp(sec, &buf[1], strlen(sec)))
			return 0;
	}
}
//-----------------------------------
int getValidInputString(char *d, int siz) {
	int len;
	while (1)
	{
		if (feof(fi))
			return 0;
		fgets(d, siz, fi);
		_strnum++;
		len = strlen(d);
		if (len < 3)
			continue;
		if (d[0] == ';')
			continue;
		return len;
	}
}
//-----------------------------------
int iniReadSec(char *d, int *pProceed)
{
	char buf[STRMAX];
	int len;
	char *s, *cp;

	if (NULL == fi)
		return 0;
	while (1)
	{
		len = getValidInputString(buf, STRMAX);
		if (0 == len)
		{
			*pProceed = 0;
			return 0;
		}
		if (buf[0] == '[')//new section start
		{
			s = &buf[1];
			cp = _newSectName;
			while (*s != ']')
				*cp++ = *s++;
			*cp = '\0';
			return 0;
		}
		strcpy(d, buf);
		return len;
	}
}
//-----------------------------------
char *iniName(char *d, char *s)
{
	while (*s != '=' && *s != '\n' && *s != ' ' && *s != '\0')
		*d++ = *s++;
	*d = '\0';
	if (*s == '=')
		return s + 1;
	else
		return NULL;
}
//-----------------------------------
int getNum(const char *s, unsigned *pVal)
{
	*pVal = 0;

	if ( *s == '0' && ( *(s + 1) == 'x' || *(s + 1) == 'X') )
		*pVal = strtoul(s + 2, NULL, 16);
	else if (*s >= '0' && *s <= '9')
		*pVal = atoi(s);
	else
		return 1; // not number
	return 0;//OK
}
//-----------------------------------
int getNumStop(const char *s, unsigned *pVal, char **stop)
{
	*pVal = 0;

	if ( *s == '0' && ( *(s + 1) == 'x' || *(s + 1) == 'X') )
		*pVal = strtoul(s + 2, stop, 16);
	else if (*s >= '0' && *s <= '9')
		*pVal = strtoul(s, (char **)stop, 10);//		*pVal = atoi(s);
	else {
        *stop = (char *)s;
		return 0; // not number
	}
	return 1;//OK
}
//-----------------------------------
unsigned iniNameVal(const char *name, const char *buf, unsigned *pVal)
{
	const char *p;

	*pVal = 0;
	p = strstr(buf, name);
	if (NULL == p)
		return 1;	// Not found;

	p = strchr(buf, '=');
	if (NULL == p)
		return 1;	// Not found;
    
	return getNum(p+1, pVal);
}
//-----------------------------------
const char *skipWhiteSpaces(const char *s)
{
	if (NULL == s)
		return s;
	while (*s != '\0')
	{
		if (*s == ' ' || *s == '\t' || *s == '\n')
			s++;
		else
			return s;
	}
	return NULL;
}
//-----------------------------------
int getLiteralSring(const char *s, char *d, int siz)
{
  int len;

  len = 0;

  s = strchr(s, '=');
  if (NULL == s)
	return 0;
  s = skipWhiteSpaces(s+1); // after '='
  if(NULL == s)
    return 0;
  if ('"' == *s) {
	len = isQuoteStr((char *)s);
	if(len > 2) {
	  len-=2;
	  memcpy(d,s+1,len); // after '"', without """".
	  *(d+len)='\0';
	  return len;
	}
  }
  return 0;
}
/*		


  if ('"' == *s) {
	 s++;
	 while ('"' != *s) {
	   *d++ = *s++;
	   len++;
	   if (len >= siz)
	 	  break;
	 }
	 return len;
   }
   else
	 return 0;
*/
//-----------------------------------
unsigned iniNameLiteralStr(const char *name, const char *s, char *d, int maxSiz)
{
	const char *p;//	char buf[MAXNAME];
	int len;

	p = strstr(s, name);
	if(p)
	{
		len = getLiteralSring(p, d, maxSiz);
		if(len)
			return len;
	}
	strcpy(errMsg, "Bad literal: ");
	strcat(errMsg, s);
	return exitWithError();
}
/*

	if (p == NULL)
		return 0;	// Not found;
	return getLiteralSring(p, d, maxSiz);
}*/
//-----------------------------------
int createElem(void)
{
	int i;
	int nField;
	char buf[STRMAX];
	char name[MAXNAME];
	char *p;
	unsigned val;
	unsigned offs;
	int proceed;

	offs = nField = 0;
	proceed = 1;
	memset(aPrfItem, 0, sizeof(aPrfItem));

	aPrfItem[0].offs = 0;
	for (i = 0; i < MAXELEM; i++)
		aPrfItem[i].siz = 0;

	i=iniOpenSec("ItemMap");
	if (i)
		return i;
	while (iniReadSec(buf, &proceed))
	{
		p=iniName(name, buf);
		if (NULL == p)
			break;
		if (strlen(name)>(MAXNAME - 1))
			name[MAXNAME - 1] = '\0';
		strcpy(aPrfItem[nField].name, name);
		getNum(p, &val);
		aPrfItem[nField].offs = offs;
		aPrfItem[nField].siz = val;
		offs += val; // for next elem.
		nField++;
	}
	_nFields = nField;
	_nItemSiz = offs;
	return 0;
}
//-----------------------------------
int getMacro(void)
{
	int i;
	int nMacro;
	char buf[STRMAX];
	char name[MAXNAME];
	char *p;
	unsigned val;
	int proceed;

	nMacro = 0;
	memset(aMacros, 0, sizeof(aMacros));
	i = iniOpenSec("MacroDefine");
	if (i)
		return i;
	while (iniReadSec(buf, &proceed))
	{
		if (nMacro > MAXMACRO)
		{
			strcpy(errMsg, "MAXMACRO over.");
			strcat(errMsg, buf);
			return 1;
		}

		p = iniName(name, buf);
		if (NULL == p)
		{
			strcpy(errMsg, buf);
			return 1;
		}
		if (strlen(name)>(MAXNAME - 1))
			name[MAXNAME - 1] = '\0';
		strcpy(aMacros[nMacro].name, name);
		getNum(p, &val);
		aMacros[nMacro].val = val;
		nMacro++;
	}
	_nMacros=nMacro;
	return 0;
}
//-----------------------------------
int isMacro(const char *s, unsigned *pVal)
{
	unsigned i;
	int len;
	for (i = 0; i < _nMacros; i++)
	{
		len = strlen(aMacros[i].name);
		if (!strncmp(aMacros[i].name, s, len))
		{
			*pVal = aMacros[i].val;
			return 1;
		}
	}
	return 0;
}
//-----------------------------------
tPrfElem * getField(char *fldName)
{
	unsigned i;
	for (i = 0; i < _nFields; i++)
	{
		if (!strcmp(aPrfItem[i].name, fldName))
			return &aPrfItem[i];
	}
	return NULL;
}
//-----------------------------------
void prnHex(char *d, unsigned val, unsigned siz)
{
	char buf[16];

	switch (siz)
	{
	case 1: // byte
		sprintf(buf, "%02X", (val & 0xFF));
		break;
	case 2: // half
		sprintf(buf, "%02X%02X", (val & 0xFF), (val >> 8));
		break;
	case 4:
		sprintf(buf, "%02X%02X%02X%02X", (val & 0xFF), ((val >> 8) & 0xFF), ((val >> 16) & 0xFF), ((val >> 24) & 0xFF));
		break;
	default:
		break;
	}
	memcpy(d, buf, siz * 2);
}
//-----------------------------------
int buildProfile(void)
{
	BYTE itemBuf[256];
	char buf[128];
	char name[MAXNAME];
	int nTest;
	tPrfElem *pField;
	char *cpI;
	BYTE *bp;
	int len;
	int i;
	unsigned val;
	int proceed;

	if (createElem())
	{
		return exitWithError();
	}
	if (getMacro())
	{
		return exitWithError();
	}

	for (nTest = 0, proceed = 1; proceed; nTest++)
	{
		memset(itemBuf, 0, 256); // initially fill 0
// fill 'title' with section name. If get 'title' field, re-write afterwards.
		pField = getField("title");
		if (NULL == pField)
		{
			strcpy(errMsg, "field 'title' not defined.");
			return 1;
		}
		len = strlen(_newSectName);
		if (len > pField->siz)
			len = pField->siz;
		memcpy(itemBuf + (pField->offs), _newSectName, len);
		while(iniReadSec(buf, &proceed))
		{
			cpI=iniName(name, buf);
			pField = getField(name);
			if (NULL == pField)
			{
				proceed = 0;
				strcpy(errMsg, name);
				strcat(errMsg, " field not defined.");
				return 1;
			}
			if (!isMacro(cpI, &val)) // first look for the name in macros
				getNum(cpI, &val);				// then try to convert number directly.
			bp = itemBuf + (pField->offs);
			for (i = 0; i < pField->siz; i++)
			{
				*bp++ = (BYTE)(val & 0xFF); // in little-endian format.
				val >>= 8;
			}
		}
		if (!strcmp("EndOfProfile", _newSectName))
			proceed = 0;
		outDataFile(itemBuf, _nItemSiz);
	}
	padOutFile(40); // as 'FF's kork, flag the last test done.
	_nTests = nTest;
	return 0;
}
//-----------------------------------
void cutFileExt(char *s)
{
	int len;
	char *cp;

	len = strlen(s);
	if (0 == len)
		return;
	len--;
	cp = s + len;
	for (; len; len--)
	{
		if (*cp == '.')
		{
			*cp = '\0';
			break;
		}
		else
			cp--;
	}
}
//-----------------------------------
int _tmain(int argc, _TCHAR* argv[])
{
	char cb[STRMAX*2];
	char fName[64];
	int len;


	puts(VERSION_STRING);
	puts("Convert <Name>.RAL to .TXT for donwloading by RB tester");
	if (argc < 2)
	{
//		puts("-----------------");		wcstombs(cb, argv[0], 255);		puts(cb);		puts("Convert <Name>.prf to binary profile <Name>.txt for donwloading");
		strcpy(errMsg, "---\n input .RAL file not found.");
		return exitWithError();//		getchar();		return 1;
	}

	_romAddr = 0;
	_checkSum = 0;
	memset(&_flags, 0, sizeof(tBitFlags));

	wcstombs(cb, argv[1], 255);
//	strcpy(cb,argv[1]);

	cutFileExt(cb);
	strcpy(fName, cb);
	strcat(fName, ".ral");
	fi = fopen(fName, "r");
	if (NULL == fi)
	{
		strcpy(errMsg, fName);
		strcat(errMsg, ":input file open error");
		return exitWithError();
	}
	while (1)
	{
		len = getValidInputString(cb, STRMAX);
		if (0 == len)
			break;
		isReservedWord(cb);
	}

	closeDataFile();
	fclose(fi);
	fclose(fHeader);
	// Wait for ENTER press.
	sprintf(cb, "Tests: %d, size: %d fields: %d", _nTests, _nItemSiz, _nFields);
	puts(cb);
	getchar();
	return 0;
}

