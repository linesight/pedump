//==================================
// PEDUMP - Matt Pietrek 1997
// FILE: LIBDUMP.C
//==================================

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include "common.h"
#include "objdump.h"
#include "libdump.h"
#include "extrnvar.h"

PSTR PszLongnames = 0;

DWORD ConvertBigEndian(DWORD bigEndian);

void DisplayArchiveMemberHeader(
    PIMAGE_ARCHIVE_MEMBER_HEADER pArchHeader,
    DWORD fileOffset )
{
    printf("Archive Member Header (%08X):\n", fileOffset);

    printf("  Name:     %.16s", pArchHeader->Name);
    if ( pArchHeader->Name[0] == '/' && isdigit(pArchHeader->Name[1]) )
        printf( "  (%s)\n", PszLongnames + atoi((char *)pArchHeader->Name+1) );
    printf("\n");

	char szDateAsLong[64];
	sprintf( szDateAsLong, "%.12s", pArchHeader->Date );
	LONG dateAsLong = atol(szDateAsLong);
	
    printf("  Date:     %.12s %s", pArchHeader->Date, ctime(&dateAsLong) );
    printf("  UserID:   %.6s\n", pArchHeader->UserID);
    printf("  GroupID:  %.6s\n", pArchHeader->GroupID);
    printf("  Mode:     %.8s\n", pArchHeader->Mode);
    printf("  Size:     %.10s\n", pArchHeader->Size);
}

void DumpFirstLinkerMember(PVOID p)
{
    DWORD cSymbols = *(PDWORD)p;
    PDWORD pMemberOffsets = MakePtr( PDWORD, p, 4 );
    PSTR pSymbolName;
    unsigned i;

    cSymbols = ConvertBigEndian(cSymbols);
    pSymbolName = MakePtr( PSTR, pMemberOffsets, 4 * cSymbols );
    
    printf("First Linker Member:\n");
    printf( "  Symbols:         %08X\n", cSymbols );
    printf( "  MbrOffs   Name\n  --------  ----\n" );
        
    for ( i = 0; i < cSymbols; i++ )
    {
        DWORD offset;
        
        offset = ConvertBigEndian( *pMemberOffsets );
        
        printf("  %08X  %s\n", offset, pSymbolName);
        
        pMemberOffsets++;
        pSymbolName += strlen(pSymbolName) + 1;
    }
}

void DumpSecondLinkerMember(PVOID p)
{
    DWORD cArchiveMembers = *(PDWORD)p;
    PDWORD pMemberOffsets = MakePtr( PDWORD, p, 4 );
    DWORD cSymbols;
    PSTR pSymbolName;
    PWORD pIndices;
    unsigned i;

    cArchiveMembers = cArchiveMembers;

    // The number of symbols is in the DWORD right past the end of the
    // member offset array.
    cSymbols = pMemberOffsets[cArchiveMembers];

    pIndices = MakePtr( PWORD, p, 4 + cArchiveMembers * sizeof(DWORD) + 4 );

    pSymbolName = MakePtr( PSTR, pIndices, cSymbols * sizeof(WORD) );
    
    printf("Second Linker Member:\n");
    
    printf( "  Archive Members: %08X\n", cArchiveMembers );
    printf( "  Symbols:         %08X\n", cSymbols );
    printf( "  MbrOffs   Name\n  --------  ----\n" );

    for ( i = 0; i < cSymbols; i++ )
    {
        printf("  %08X  %s\n", pMemberOffsets[pIndices[i] - 1], pSymbolName);
        pSymbolName += strlen(pSymbolName) + 1;
    }
}

void DumpLongnamesMember(PVOID p, DWORD len)
{
    PSTR pszName = (PSTR)p;
    DWORD offset = 0;

    PszLongnames = (PSTR)p;     // Save off pointer for use when dumping
                                // out OBJ member names

    printf("Longnames:\n");
    
    // The longnames member is a series of null-terminated string.  Print
    // out the offset of each string (in decimal), followed by the string.
    while ( offset < len )
    {
        unsigned cbString = lstrlen( pszName )+1;

        printf("  %05u: %s\n", offset, pszName);
        offset += cbString;
        pszName += cbString;
    }
}

void DumpLibFile( LPVOID lpFileBase )
{
    PIMAGE_ARCHIVE_MEMBER_HEADER pArchHeader;
    BOOL fSawFirstLinkerMember = FALSE;
    BOOL fSawSecondLinkerMember = FALSE;
    BOOL fBreak = FALSE;

    if ( strncmp((char *)lpFileBase,IMAGE_ARCHIVE_START,
                            		IMAGE_ARCHIVE_START_SIZE ) )
    {
        printf("Not a valid .LIB file - signature not found\n");
        return;
    }
    
    pArchHeader = MakePtr(PIMAGE_ARCHIVE_MEMBER_HEADER, lpFileBase,
                            IMAGE_ARCHIVE_START_SIZE);

    while ( pArchHeader )
    {
        DWORD thisMemberSize;
        
        DisplayArchiveMemberHeader( pArchHeader,
                                    (PBYTE)pArchHeader - (PBYTE) lpFileBase );
        printf("\n");

        if ( !strncmp( 	(char *)pArchHeader->Name,
        				IMAGE_ARCHIVE_LINKER_MEMBER, 16) )
        {
            if ( !fSawFirstLinkerMember )
            {
                DumpFirstLinkerMember( (PVOID)(pArchHeader + 1) );
                printf("\n");
                fSawFirstLinkerMember = TRUE;
            }
            else if ( !fSawSecondLinkerMember )
            {
                DumpSecondLinkerMember( (PVOID)(pArchHeader + 1) );
                printf("\n");
                fSawSecondLinkerMember = TRUE;
            }
        }
        else if( !strncmp(	(char *)pArchHeader->Name,
        					IMAGE_ARCHIVE_LONGNAMES_MEMBER, 16) )
        {
            DumpLongnamesMember( (PVOID)(pArchHeader + 1),
                                 atoi((char *)pArchHeader->Size) );
            printf("\n");
        }
        else    // It's an OBJ file
        {
            DumpObjFile( (PIMAGE_FILE_HEADER)(pArchHeader + 1) );
        }

        // Calculate how big this member is (it's originally stored as 
        // as ASCII string.
        thisMemberSize = atoi((char *)pArchHeader->Size)
                        + IMAGE_SIZEOF_ARCHIVE_MEMBER_HDR;

        thisMemberSize = (thisMemberSize+1) & ~1;   // Round up

        // Get a pointer to the next archive member
        pArchHeader = MakePtr(PIMAGE_ARCHIVE_MEMBER_HEADER, pArchHeader,
                                thisMemberSize);

        // Bail out if we don't see the EndHeader signature in the next record
        __try
        {
            if (strncmp( (char *)pArchHeader->EndHeader, IMAGE_ARCHIVE_END, 2))
                break;
        }
        __except( TRUE )    // Should only get here if pArchHeader is bogus
        {
            fBreak = TRUE;  // Ideally, we could just put a "break;" here,
        }                   // but BC++ doesn't like it.
        
        if ( fBreak )   // work around BC++ problem.
            break;
    }
}

// Routine to convert from big endian to little endian
DWORD ConvertBigEndian(DWORD bigEndian)
{
	DWORD temp = 0;

	// printf( "bigEndian: %08X\n", bigEndian );

	temp |= bigEndian >> 24;
	temp |= ((bigEndian & 0x00FF0000) >> 8);
	temp |= ((bigEndian & 0x0000FF00) << 8);
	temp |= ((bigEndian & 0x000000FF) << 24);

	return temp;
}

