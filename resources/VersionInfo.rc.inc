// Shared VERSIONINFO block. The including .rc defines AS_FILE_DESCRIPTION,
// AS_INTERNAL_NAME and AS_ORIGINAL_FILENAME first.

VS_VERSION_INFO VERSIONINFO
 FILEVERSION AUDIOSLAVE_VERSION_RC
 PRODUCTVERSION AUDIOSLAVE_VERSION_RC
 FILEFLAGSMASK 0x3fL
#ifdef _DEBUG
 FILEFLAGS 0x1L
#else
 FILEFLAGS 0x0L
#endif
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_APP
 FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904B0"
        BEGIN
            VALUE "CompanyName", "caio-kenai"
            VALUE "FileDescription", AS_FILE_DESCRIPTION
            VALUE "FileVersion", AUDIOSLAVE_VERSION_STRING
            VALUE "InternalName", AS_INTERNAL_NAME
            VALUE "OriginalFilename", AS_ORIGINAL_FILENAME
            VALUE "ProductName", "Audioslave"
            VALUE "ProductVersion", AUDIOSLAVE_VERSION_STRING
            VALUE "LegalCopyright", "Copyright (C) 2026 caio-kenai. AGPL-3.0."
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
