/*
 * UNIMODEM "Fakemodem" controllerless driver illustrative example
 *
 * (C) 2000 Microsoft Corporation
 * All Rights Reserved
 *
 */

#include "fakemodem.h"

#if DBG
ULONG DebugFlags=255;
#endif

UNICODE_STRING   DriverEntryRegPath;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT,DriverEntry)
#pragma alloc_text(PAGE,FakeModemAddDevice)
#endif


NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;

    RTL_QUERY_REGISTRY_TABLE paramTable[3];
    ULONG zero = 0;
    ULONG debugLevel = 0;
    ULONG shouldBreak = 0;
    PWCHAR path;

    D_INIT(DbgPrint("FAKEMODEM: DriverEntry\n");)

    // Since the registry path parameter is a "counted" UNICODE string, it
    // might not be zero terminated.  For a very short time allocate memory
    // to hold the registry path zero terminated so that we can use it to
    // delve into the registry.

    path = ALLOCATE_PAGED_POOL(RegistryPath->Length+sizeof(WCHAR));

    if (path != NULL) 
    {
        RtlZeroMemory(&paramTable[0],sizeof(paramTable));
        RtlZeroMemory(path,RegistryPath->Length+sizeof(WCHAR));
        RtlMoveMemory(path,RegistryPath->Buffer,RegistryPath->Length);

        paramTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT;
        paramTable[0].Name = L"BreakOnEntry";
        paramTable[0].EntryContext = &shouldBreak;
        paramTable[0].DefaultType = REG_DWORD;
        paramTable[0].DefaultData = &zero;
        paramTable[0].DefaultLength = sizeof(ULONG);

        paramTable[1].Flags = RTL_QUERY_REGISTRY_DIRECT;
        paramTable[1].Name = L"DebugFlags";
        paramTable[1].EntryContext = &debugLevel;
        paramTable[1].DefaultType = REG_DWORD;
        paramTable[1].DefaultData = &zero;
        paramTable[1].DefaultLength = sizeof(ULONG);


        // If the Debugflags registry key is not set then
        // provide full debugging information

        if (!NT_SUCCESS(RtlQueryRegistryValues(
                        RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL, 
                        path, &paramTable[0], NULL, NULL))) 
        {
            shouldBreak = 0;
            debugLevel = 255;
        }

        FREE_POOL(path);
    }

#if DBG
    DebugFlags = debugLevel;
#endif

    if (shouldBreak) 
    {
        DbgBreakPoint();
    }

    // Pnp driver entry point

    DriverObject->DriverExtension->AddDevice = FakeModemAddDevice;

    // Initialize the driver object with driver's entry points

    DriverObject->DriverUnload = FakeModemUnload;

    DriverObject->MajorFunction[IRP_MJ_CREATE]            = FakeModemOpen;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]             = FakeModemClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]           = FakeModemCleanup;
    DriverObject->MajorFunction[IRP_MJ_WRITE]             = FakeModemWrite;
    DriverObject->MajorFunction[IRP_MJ_READ]              = FakeModemRead;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]    = FakeModemIoControl;
    DriverObject->MajorFunction[IRP_MJ_PNP]               = FakeModemPnP;
    DriverObject->MajorFunction[IRP_MJ_POWER]             = FakeModemPower;

    D_INIT(DbgPrint("FAKEMODEM: End of DriverEntry\n");)

    return STATUS_SUCCESS;
}

VOID
FakeModemUnload(
    IN PDRIVER_OBJECT DriverObject
    )
{
    D_INIT(DbgPrint("FAKEMODEM: FakeModemUnload()\n");)

    return;
}

NTSTATUS
FakeModemAddDevice(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT Pdo
    )
{
    NTSTATUS           status=STATUS_SUCCESS;
    PDEVICE_OBJECT     Fdo;
    PDEVICE_EXTENSION  DeviceExtension;
    UNICODE_STRING     DeviceName;

    D_INIT(DbgPrint("FAKEMODEM: Fakemodem Add Device\n");)

    // Create our functional device object (FDO)
    
    status=IoCreateDevice(DriverObject, sizeof(DEVICE_EXTENSION), NULL, 
            FILE_DEVICE_SERIAL_PORT, FILE_AUTOGENERATED_DEVICE_NAME, 
            FALSE, &Fdo);

    if (status != STATUS_SUCCESS) 
    {
        return status;
    }

    Fdo->Flags |= DO_BUFFERED_IO;
    DeviceExtension = Fdo->DeviceExtension;
    DeviceExtension->DeviceObject = Fdo;

    // Attach our FDO to the PDO supplied
    
    DeviceExtension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (NULL == DeviceExtension->LowerDevice) 
    {
        // Could not attach

        IoDeleteDevice(Fdo);
        return STATUS_UNSUCCESSFUL;
    }

    //  Try to create a ComX for it. don't care if it fails
    //  modem.sys creates a name for device that unimodem will use
    

    FakeModemHandleSymbolicLink(Pdo, TRUE, 
            &DeviceExtension->InterfaceNameString, Fdo);

    //  Initialise the spinlock
    
    KeInitializeSpinLock(&DeviceExtension->SpinLock);

    //  Initialize the device extension

    DeviceExtension->ReferenceCount=1;
    DeviceExtension->Removing=FALSE;
    DeviceExtension->Started=FALSE;
    DeviceExtension->OpenCount=0;
    KeInitializeEvent(&DeviceExtension->RemoveEvent, NotificationEvent, FALSE);

    //  Initialize the read and write queues

    InitializeListHead(&DeviceExtension->ReadQueue);
    DeviceExtension->CurrentReadIrp=NULL;

    InitializeListHead(&DeviceExtension->WriteQueue);
    DeviceExtension->CurrentWriteIrp=NULL;

    InitializeListHead(&DeviceExtension->MaskQueue);
    DeviceExtension->CurrentMaskIrp=NULL;

    // Clear this flag so the device object can be used

    Fdo->Flags &= ~(DO_DEVICE_INITIALIZING);

    return STATUS_SUCCESS;

}

NTSTATUS 
GetRegistryKeyValue (
    IN HANDLE Handle,
    IN PWCHAR KeyNameString,
    IN ULONG KeyNameStringLength,
    IN PVOID Data,
    IN ULONG DataLength
    )
{
    UNICODE_STRING keyName;
    ULONG length;
    PKEY_VALUE_FULL_INFORMATION fullInfo;

    NTSTATUS ntStatus = STATUS_NO_MEMORY;

    RtlInitUnicodeString (&keyName, KeyNameString);

    length = sizeof(KEY_VALUE_FULL_INFORMATION) + KeyNameStringLength + 
        DataLength;

    fullInfo = ExAllocatePool(PagedPool, length);

    if (fullInfo) 
    {
        ntStatus = ZwQueryValueKey(Handle, &keyName, 
                KeyValueFullInformation, fullInfo, length, &length);

        if (NT_SUCCESS(ntStatus)) 
        {
            // If there is enough room in the data buffer, copy the output

            if (DataLength >= fullInfo->DataLength) 
            {
                RtlCopyMemory(Data, 
                        ((PUCHAR) fullInfo) + fullInfo->DataOffset, 
                        fullInfo->DataLength);
            }
        }

        ExFreePool(fullInfo);
    }

    return ntStatus;
}

NTSTATUS
FakeModemHandleSymbolicLink(
    PDEVICE_OBJECT      Pdo,
    BOOLEAN             Create,
    PUNICODE_STRING     InterfaceName,
    PDEVICE_OBJECT      Fdo
    )

{

    UNICODE_STRING     SymbolicLink;
    ULONG              StringLength;
    NTSTATUS           Status;
    WCHAR              ComPort[80];
    HANDLE             keyHandle;
    RTL_QUERY_REGISTRY_TABLE paramTable[1];

    D_INIT(DbgPrint("FAKEMODEM: HandleSymbolicLink\n");)

    Status = IoOpenDeviceRegistryKey(Pdo, PLUGPLAY_REGKEY_DEVICE, 
            STANDARD_RIGHTS_READ, &keyHandle);

    SymbolicLink.Length=0;
    SymbolicLink.MaximumLength=sizeof(WCHAR)*256;
    SymbolicLink.Buffer=ExAllocatePool(PagedPool,
            SymbolicLink.MaximumLength+sizeof(WCHAR));

    if (SymbolicLink.Buffer == NULL) 
    {
        ZwClose(keyHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(SymbolicLink.Buffer, SymbolicLink.MaximumLength);
    RtlAppendUnicodeToString(&SymbolicLink, L"\\");
    RtlAppendUnicodeToString(&SymbolicLink, OBJECT_DIRECTORY);
    RtlAppendUnicodeToString(&SymbolicLink, L"\\");

    Status=GetRegistryKeyValue(keyHandle, L"PortName", 
            sizeof(L"PortName"), ComPort, sizeof(ComPort));

    D_INIT(DbgPrint("FAKEMODEM: PortName %ws\n",ComPort);)

    if (Status != STATUS_SUCCESS) 
    {
        ExFreePool(SymbolicLink.Buffer);
        ZwClose(keyHandle);
        return Status;
    }

    RtlAppendUnicodeToString(&SymbolicLink, ComPort);

    ZwClose(keyHandle);

    if (Create) 
    {
        UNICODE_STRING     PdoName;

        PdoName.Length=0;
        PdoName.MaximumLength=sizeof(WCHAR)*256;
        PdoName.Buffer=ExAllocatePool(PagedPool,
                PdoName.MaximumLength+sizeof(WCHAR));

        if (PdoName.Buffer == NULL) 
        {
            ExFreePool(SymbolicLink.Buffer);

            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(PdoName.Buffer,PdoName.MaximumLength);

        Status=IoGetDeviceProperty(Pdo, DevicePropertyPhysicalDeviceObjectName,
                (ULONG)PdoName.MaximumLength, PdoName.Buffer, &StringLength);

        if (!NT_SUCCESS(Status)) 
        {
            D_INIT(DbgPrint("FAKEMODEM: IoGetDeviceProperty() failed %08lx\n",
                        Status);)

            ExFreePool(SymbolicLink.Buffer);

            return Status;
        }

        PdoName.Length+=(USHORT)StringLength-sizeof(UNICODE_NULL);
        D_INIT(DbgPrint("FAKEMODEM: PdoName: %ws\n",PdoName.Buffer);)
        Status=IoCreateSymbolicLink(&SymbolicLink, &PdoName);
        Status=IoRegisterDeviceInterface(Pdo, &GUID_CLASS_MODEM, NULL, 
                InterfaceName);

        if (NT_SUCCESS(Status))
        {
            IoSetDeviceInterfaceState(InterfaceName, TRUE);
        } else
        {
            D_INIT(DbgPrint("FAKEMODEM: IoRegisterDeviceInterface() failed %08lx\n",Status);)
        }


        Status = RtlWriteRegistryValue(RTL_REGISTRY_DEVICEMAP, L"SERIALCOMM",
                                     PdoName.Buffer, REG_SZ, ComPort,
                                     (wcslen(ComPort) + 1) * sizeof(WCHAR));

        if (!NT_SUCCESS(Status))
        {
            D_INIT(DbgPrint("FAKEMODEM: RtlWriteRegistryValue() failed %08lx\n",Status);)
            ExFreePool(SymbolicLink.Buffer);
            ExFreePool(PdoName.Buffer);

            return Status;
        }
        
        ExFreePool(PdoName.Buffer);

    } else {

        Status=IoDeleteSymbolicLink(&SymbolicLink);

        D_INIT(DbgPrint("FAKEMODEM: Deleted symbolic link\n");)

    }

    ExFreePool(SymbolicLink.Buffer);

    D_INIT(DbgPrint("FAKEMODEM: End of handle symbolic link\n");)
    return Status;

}

NTSTATUS
QueryDeviceCaps(
    PDEVICE_OBJECT    Pdo,
    PDEVICE_CAPABILITIES   Capabilities
    )

{

    PDEVICE_OBJECT       deviceObject=Pdo;
    PIRP                 irp;
    PIO_STACK_LOCATION   NextSp;
    KEVENT               Event;
    NTSTATUS             Status;

    // Get a pointer to the top most device object in the stack of 
    // devices, beginning with the deviceObject.

    while (deviceObject->AttachedDevice) 
    {
        deviceObject = deviceObject->AttachedDevice;
    }

    // Begin by allocating the IRP for this request.  Do not charge 
    // quota to the current process for this IRP.

    irp = IoAllocateIrp(
#if DBG
            (UCHAR)(deviceObject->StackSize+1),
#else
            deviceObject->StackSize,
#endif
            FALSE);

    if (irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

#if DBG
    {
        //  Setup a current stack location, so the debug code can see the 
        //  MJ value

        PIO_STACK_LOCATION   irpSp=IoGetNextIrpStackLocation(irp);

        irpSp->MajorFunction=IRP_MJ_PNP;
        IoSetNextIrpStackLocation(irp);
    }
#endif

    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;

    RtlZeroMemory(Capabilities,sizeof(DEVICE_CAPABILITIES));

    Capabilities->Size=sizeof(DEVICE_CAPABILITIES);
    Capabilities->Version=1;
    Capabilities->Address=-1;
    Capabilities->UINumber=-1;

    // Get a pointer to the stack location of the first driver which will be
    // invoked.  This is where the function codes and parameters are set.

    NextSp = IoGetNextIrpStackLocation(irp);

    NextSp->MajorFunction=IRP_MJ_PNP;
    NextSp->MinorFunction=IRP_MN_QUERY_CAPABILITIES;
    NextSp->Parameters.DeviceCapabilities.Capabilities=Capabilities;

    Status=WaitForLowerDriverToCompleteIrp(deviceObject, irp, FALSE );

    IoFreeIrp(irp);

    return Status;

}

NTSTATUS
ModemSetRegistryKeyValue(
        IN PDEVICE_OBJECT   Pdo,
        IN ULONG            DevInstKeyType,
        IN PWCHAR           KeyNameString,
        IN ULONG            DataType,
        IN PVOID            Data,
        IN ULONG            DataLength)
{

    NTSTATUS ntStatus = STATUS_INSUFFICIENT_RESOURCES;
    HANDLE Handle;
    UNICODE_STRING keyName;

    PAGED_CODE();

    D_ERROR(DbgPrint("MODEM: Current IRQL %d\n",KeGetCurrentIrql());)

    ntStatus = IoOpenDeviceRegistryKey(Pdo, DevInstKeyType, KEY_ALL_ACCESS, 
            &Handle);

    if (NT_SUCCESS(ntStatus))
    {
        RtlInitUnicodeString(&keyName,KeyNameString);

        ntStatus = ZwSetValueKey(Handle, &keyName, 0, DataType, Data, 
                DataLength);

        if (!NT_SUCCESS(ntStatus))
        {
            D_ERROR(DbgPrint("MODEM: Could not set value, %08lx\n",ntStatus);)
        }

    } else
    {
        ZwClose(Handle);

        D_ERROR(DbgPrint("MODEM: Could not open dev registry key, %08lx\n",
                    ntStatus);)
    }


    return ntStatus;
}

NTSTATUS 
ModemGetRegistryKeyValue (
    IN PDEVICE_OBJECT   Pdo,
    IN ULONG            DevInstKeyType,
    IN PWCHAR KeyNameString,
    IN PVOID Data,
    IN ULONG DataLength
    )
{
    UNICODE_STRING              keyName;
    ULONG                       length;
    PKEY_VALUE_PARTIAL_INFORMATION     PartialInfo;

    NTSTATUS                    ntStatus = STATUS_INSUFFICIENT_RESOURCES;
    HANDLE                      Handle;

    PAGED_CODE();


    ntStatus = IoOpenDeviceRegistryKey(Pdo, DevInstKeyType, 
            STANDARD_RIGHTS_READ, &Handle);

    if (NT_SUCCESS(ntStatus)) 
    {

        RtlInitUnicodeString (&keyName, KeyNameString);

        length = sizeof(KEY_VALUE_FULL_INFORMATION) + DataLength;

        PartialInfo = ALLOCATE_PAGED_POOL(length);

        if (PartialInfo) 
        {
            ntStatus = ZwQueryValueKey (Handle, &keyName, 
                    KeyValuePartialInformation, PartialInfo, length, &length);

            if (NT_SUCCESS(ntStatus)) 
            {
                //
                // If there is enough room in the data buffer, copy the output
                //

                if (DataLength >= PartialInfo->DataLength) 
                {
                    RtlCopyMemory (Data, PartialInfo->Data, 
                            PartialInfo->DataLength);
                }
            } else 
            {

                D_ERROR(DbgPrint("MODEM: could not query value, %08lx\n",
                            ntStatus);)
            }

            FREE_POOL(PartialInfo);
        }

        ZwClose(Handle);

    } else {

        D_ERROR(DbgPrint("MODEM: could open device reg key, %08lx\n",ntStatus);)
    }

    return ntStatus;
}

