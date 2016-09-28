/*++
 
    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
    ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
    THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
    PARTICULAR PURPOSE.

    Copyright (c) Microsoft Corporation. All rights reserved

Module Name:

    StorageAdapter.h

Abstract:

    This module contains a stub implementation of a Storage Adapter
    plug-in for the Windows Biometric service.

Author:

    -

Environment:

    Win32, user mode only.

Revision History:

NOTES:

    (None)

--*/
#pragma once

#include "winbio_adapter.h"

///////////////////////////////////////////////////////////////////////////////
//
// The WINIBIO_STORAGE_CONTEXT structure is privately-defined by each
// Storage Adapter. Its purpose is to maintain any information that
// should persist across Storage Adapter API calls. 
//
// The Adapter allocates and initializes one of these structures in its
// 'Attach' routine and saves its address in the Pipeline->StorageContext 
// field.
//
// The Storage Adapter's 'Detach' routine cleans up and deallocates the
// structure and sets the PipelineContext->StorageContext field to NULL.
//
///////////////////////////////////////////////////////////////////////////////
typedef struct _WINIBIO_STORAGE_CONTEXT {
    //
    // The following fields illustrate the kind of information 
    // the Storage Adapter needs to keep in this structure:
    //
    //      DatabaseId      - Identify of the Adapter's 
    //                        currently-opened database.
    //
    //      DatabaseHandle  - A handle to the Storage Adapter's 
    //                        currently-open database.
    //
    //      ResultSet       - A collection of records generated by
    //                        a database query operation. 
    //
    //      ResultSetCursor - Current location in the result 
    //                        set. Used to iterate through the
    //                        result set and make individual
    //                        records available.
    //
    WINBIO_UUID DatabaseId;
    PVOID DatabaseHandle;
    PVOID ResultSet;
    PVOID ResultSetCursor;

} WINIBIO_STORAGE_CONTEXT, *PWINIBIO_STORAGE_CONTEXT;



