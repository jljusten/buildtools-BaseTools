/** @file

 Copyright (c) 2007 - 2014, Intel Corporation. All rights reserved.<BR>

 This program and the accompanying materials
 are licensed and made available under the terms and conditions of the BSD License
 which accompanies this distribution.  The full text of the license may be found at
 http://opensource.org/licenses/bsd-license.php

 THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
 WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <Common/UefiBaseTypes.h>

#include "CommonLib.h"
#include "MemoryFile.h"
#include "OsPath.h"
#include "EfiUtilityMsgs.h"

//
// Include miniz code. It doesn't have a separate .h.
//
#include "miniz.c"

#define UTILITY_NAME            "BaseToolsBins"
#define UTILITY_MAJOR_VERSION   0
#define UTILITY_MINOR_VERSION   1

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

//
// Global variables
//
CHAR8 *InstallDir = NULL;
CHAR8 *ZipName = NULL;
CHAR8 *ZipUrl = NULL;
CHAR8 *InstalledZipName = NULL;


//
// BaseToolsBins functions
//

STATIC
VOID 
Version (
  VOID
  )
/*++

Routine Description:

  Print out version information for this utility.

Arguments:

  None
  
Returns:

  None
  
--*/ 
{
  fprintf (stdout, "%s Version %d.%d %s \n", UTILITY_NAME, UTILITY_MAJOR_VERSION, UTILITY_MINOR_VERSION, __BUILD_VERSION);
}


STATIC
VOID
Usage (
  VOID
  )
/*++

Routine Description:

  Print Error / Help message.

Arguments:

  VOID

Returns:

  None

--*/
{
  //
  // Summary usage
  //
  fprintf (stdout, "\nUsage: %s [options]\n\n", UTILITY_NAME);
  
  //
  // Copyright declaration
  // 
  fprintf (stdout, "Copyright (c) 2007 - 2014, Intel Corporation. All rights reserved.\n\n");

  //
  // Details Option
  //
  fprintf (stdout, "Options:\n");
  fprintf (stdout, "  --version             Show program's version number and exit.\n");
  fprintf (stdout, "  -h, --help            Show this help message and exit.\n");
}


STATIC
EFI_STATUS
GetInstallDir (
  CONST CHAR8* Executable
  )
{
  CONST CHAR8 Seps[] = "/\\";
  UINTN       Loop;
  UINTN       PathLength;

  PathLength = 0;

  for (Loop = 0; Loop < ARRAY_SIZE (Seps) - 1; Loop++) {
    CHAR8 Sep = Seps[Loop];
    CHAR8 *Loc;

    Loc = strrchr(Executable, Sep);
    if (Loc != NULL) {
      PathLength = MAX (PathLength, (UINTN) ((Loc + 1) - Executable));
    }
  }

  if (PathLength > 0) {
    InstallDir = malloc (PathLength + 1);
    if (InstallDir == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    strncpy (InstallDir, Executable, PathLength);
    InstallDir[PathLength] = (CHAR8) 0;
  } else {
    InstallDir = "";
  }

  return EFI_SUCCESS;
}


STATIC
CHAR8*
InstallDirAppend (
  CONST CHAR8* Suffix
  )
{
  UINTN PathLength;
  CHAR8 *Result;

  PathLength = strlen (InstallDir) + strlen (Suffix);
  Result = malloc (PathLength + 1);

  strcpy (Result, InstallDir);
  strcat (Result, Suffix);

  return Result;
}


STATIC
BOOLEAN
StartsWith (
  CONST CHAR8* String,
  CONST CHAR8* Prefix
  )
{
  if (String == NULL || Prefix == NULL) {
    return FALSE;
  }

  if (strncmp(String, Prefix, strlen(Prefix)) == 0) {
    return TRUE;
  }

  return FALSE;
}


STATIC
CHAR8*
StrRStrip (
  CHAR8* String
  )
{
  UINTN Offset;

  if (String == NULL) {
    return NULL;
  }

  Offset = strlen (String);

  do {
    if (Offset == 0) {
      return String;
    }
    Offset--;
    if (!isspace(String[Offset])) {
      break;
    }

    String[Offset] = (CHAR8) 0;
  } while (TRUE);

  return String;
}


STATIC
CHAR8*
GetIniStringValue (
  CONST CHAR8 *IniLine
  )
{
  CHAR8 *Result;
  CHAR8 *Loc;
  UINTN ResultLength;

  Loc = strchr (IniLine, ':');
  if (Loc == NULL) {
    return NULL;
  }

  Loc++;

  while (*Loc == ' ') {
    Loc++;
  }

  ResultLength = strlen(Loc);
  Result = malloc(ResultLength + 1);
  if (Result == NULL) {
    return NULL;
  }

  strcpy (Result, Loc);
  StrRStrip (Result);

  return Result;
}


STATIC
EFI_STATUS
ParseIni (
  BOOLEAN VersionIni
  )
{
  EFI_STATUS Status;
  EFI_HANDLE File;
  CHAR8      *IniFileName;
  CHAR8      *Line;
  BOOLEAN    FoundSection;

  IniFileName = InstallDirAppend (VersionIni ? "version.ini" : "installed.ini");

  if (!OsPathExists (IniFileName)) {
    CHAR8 *ErrorStr;

    if (!VersionIni) {
      //
      // If we are loading installed.ini, then it is okay if it doesn't exist,
      // and we don't need to print an error message.
      //
      free (IniFileName);
      return EFI_NOT_FOUND;
    }

    ErrorStr = malloc (strlen (IniFileName) + 80);
    if (ErrorStr) {
      sprintf (ErrorStr, "file %s not found", IniFileName);
      Error (NULL, 0, 4001, UTILITY_NAME, ErrorStr);
      free (ErrorStr);
    } else {
      Error (NULL, 0, 4001, UTILITY_NAME, "INI file not found");
    }

    free (IniFileName);
    return EFI_NOT_FOUND;
  }

  Status = GetMemoryFile (IniFileName, &File);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  FoundSection = FALSE;
  do {
    Line = ReadMemoryFileLine (File);
    if (Line == NULL) {
      break;
    }

    if (strcmp (Line, "[zip]") == 0) {
      FoundSection = TRUE;
      free (Line);
      break;
    }

    free (Line);
  } while (TRUE);

  if (!FoundSection) {
    FreeMemoryFile (File);
    return EFI_NOT_FOUND;
  }

  do {
    Line = ReadMemoryFileLine (File);
    if (Line == NULL) {
      break;
    }

    if (Line[0] == '[') {
      free (Line);
      break;
    }

    if (StartsWith (Line, "file:")) {
      CHAR8 *Value = GetIniStringValue (Line);
      if (VersionIni) {
        ZipName = Value;
      } else {
        InstalledZipName = Value;
      }
    } else if (VersionIni && StartsWith (Line, "url:")) {
      ZipUrl = GetIniStringValue (Line);
    }

    free (Line);
  } while (TRUE);

  if (ZipName == NULL) {
    Error (NULL, 0, 4001, UTILITY_NAME, "zip filename not found");
  }

  FreeMemoryFile (File);
  return Status;
}


STATIC
EFI_STATUS
InstallBaseToolsFiles (
  VOID
  )
{
  EFI_STATUS     Status;
  EFI_HANDLE     ZipMemoryFile;
  mz_zip_archive ZipFile;
  mz_bool        MzBool;
  UINTN          NumFiles;
  UINTN          FileNum;
  CHAR8          *ZipFullPath;

  ZipFullPath = InstallDirAppend (ZipName);
  if (ZipFullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (!OsPathExists (ZipFullPath)) {
    printf ("Unable to open %s\n", ZipFullPath);
    printf ("You can download this BaseTools bin release at:\n%s\n", ZipUrl);
    free (ZipFullPath);
    return EFI_NOT_FOUND;
  }

  Status = GetMemoryFile (ZipFullPath, &ZipMemoryFile);
  if (EFI_ERROR (Status)) {
    free (ZipFullPath);
    return Status;
  }

  ZeroMem (&ZipFile, sizeof (ZipFile));

  MzBool = mz_zip_reader_init_mem(
             &ZipFile,
             GetMemoryFileContents (ZipMemoryFile),
             GetMemoryFileSize (ZipMemoryFile),
             0);
  if (!MzBool) {
    Error (NULL, 0, 4001, UTILITY_NAME, "failed to open zip file");
    Status = EFI_VOLUME_CORRUPTED;
    goto FreeMemoryFile;
  }

  NumFiles = mz_zip_reader_get_num_files(&ZipFile);
  printf("Zip num files: %d\n", (int) NumFiles);

  for (FileNum = 0; FileNum < NumFiles; FileNum++) {
    CHAR8 *ArchiveName;
    CHAR8 *DestName;
    UINTN ArchiveNameLength;

    ArchiveNameLength = mz_zip_reader_get_filename (
                          &ZipFile,
                          FileNum,
                          NULL,
                          0
                          );

    ArchiveName = malloc (ArchiveNameLength);
    if (ArchiveName == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto FreeZipFile;
    }

    mz_zip_reader_get_filename (
      &ZipFile,
      FileNum,
      ArchiveName,
      ArchiveNameLength
      );

    printf("Zip archive name: %s\n", ArchiveName);

    DestName = InstallDirAppend (ArchiveName);
    free (ArchiveName);

    MzBool = mz_zip_reader_extract_to_file (
               &ZipFile,
               FileNum,
               DestName,
               0
               );
    if (!MzBool) {
      Error (NULL, 0, 4001, UTILITY_NAME, "failed extract zip archive file");
      Status = EFI_VOLUME_CORRUPTED;
      goto FreeZipFile;
    }
  }

  Status = EFI_SUCCESS;

FreeZipFile:
  mz_zip_reader_end(&ZipFile);

FreeMemoryFile:
  FreeMemoryFile (ZipMemoryFile);
  free (ZipFullPath);

  return Status;
}


STATIC
EFI_STATUS
WriteInstalledIni (
  VOID
  )
{
  CHAR8          *IniFullPath;
  FILE           *Ini;

  IniFullPath = InstallDirAppend ("installed.ini");
  if (IniFullPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Ini = fopen (IniFullPath, "wb");
  if (Ini == NULL) {
    free (IniFullPath);
    Error (NULL, 0, 4001, UTILITY_NAME, "failed to open installed.ini file");
    return EFI_WRITE_PROTECTED;
  }

  fprintf (Ini, "[zip]\n");
  fprintf (Ini, "file: %s\n", ZipName);

  fclose (Ini);
  free (IniFullPath);
  return EFI_SUCCESS;
}


/**
  Main function.

  @param argc  Number of command line parameters.
  @param argv  Array of pointers to command line parameter strings.

  @retval STATUS_SUCCESS  Utility exits successfully.
  @retval STATUS_ERROR    Some error occurred during execution.

**/
int
main (
  int  argc,
  char *argv[]
  )
{
  EFI_STATUS Status;

  Status = GetInstallDir (argv[0]);
  if (EFI_ERROR (Status)) {
    return STATUS_ERROR;
  }

  //
  // Parse command line
  //
  argc --;
  argv ++;

  if (argc > 0 && ((stricmp (argv[0], "-h") == 0) || (stricmp (argv[0], "--help") == 0))) {
    Version ();
    Usage ();
    return STATUS_SUCCESS;    
  }

  if (argc > 0 && stricmp (argv[0], "--version") == 0) {
    Version ();
    return STATUS_SUCCESS;    
  }

  //
  // Parse version.ini file
  //
  Status = ParseIni (TRUE);
  if (EFI_ERROR (Status)) {
    return STATUS_ERROR;
  }

  //
  // Parse installed.ini file
  //
  Status = ParseIni (FALSE);

  //
  // If installed.ini exists, then check to see if the BaseTools binaries
  // have already been installed.
  //
  if (!EFI_ERROR (Status)) {
    if (strcmp (ZipName, InstalledZipName) == 0) {
      //
      // The BaseTools binaries are already installed, so we can exit now
      //
      return STATUS_SUCCESS;    
    }
  }

  //
  // Unzip the archive with the BaseTools binaries
  //
  Status = InstallBaseToolsFiles ();
  if (EFI_ERROR (Status)) {
    return STATUS_ERROR;
  }

  Status = WriteInstalledIni ();
  if (EFI_ERROR (Status)) {
    Error (NULL, 0, 4001, UTILITY_NAME, "WriteInstalledIni failed");
    return STATUS_ERROR;
  }

  return STATUS_SUCCESS;    
}
