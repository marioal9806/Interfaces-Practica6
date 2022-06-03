#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <windows.h>
#include <SETUPAPI.H>

//----------------------------------------------
//#define RICH_VENDOR_ID			0x0000
//#define RICH_USBHID_GENIO_ID	0x2022

// Global vars for VID/PID
USHORT  RICH_VENDOR_ID;
USHORT  RICH_USBHID_GENIO_ID;

#define INPUT_REPORT_SIZE	64
#define OUTPUT_REPORT_SIZE	64

// Tamaño maximo del buffer de entrada para el usuario
#define MAX_BUFFER_SIZE 10
//----------------------------------------------

typedef struct _HIDD_ATTRIBUTES {
	ULONG   Size; // = sizeof (struct _HIDD_ATTRIBUTES)
	USHORT  VendorID;
	USHORT  ProductID;
	USHORT  VersionNumber;
} HIDD_ATTRIBUTES, *PHIDD_ATTRIBUTES;

typedef VOID(__stdcall *PHidD_GetProductString)(HANDLE, PVOID, ULONG);
typedef VOID(__stdcall *PHidD_GetHidGuid)(LPGUID);
typedef BOOLEAN(__stdcall *PHidD_GetAttributes)(HANDLE, PHIDD_ATTRIBUTES);
typedef BOOLEAN(__stdcall *PHidD_SetFeature)(HANDLE, PVOID, ULONG);
typedef BOOLEAN(__stdcall *PHidD_GetFeature)(HANDLE, PVOID, ULONG);

//----------------------------------------------

HINSTANCE                       hHID = NULL;
PHidD_GetProductString          HidD_GetProductString = NULL;
PHidD_GetHidGuid                HidD_GetHidGuid = NULL;
PHidD_GetAttributes             HidD_GetAttributes = NULL;
PHidD_SetFeature                HidD_SetFeature = NULL;
PHidD_GetFeature                HidD_GetFeature = NULL;
HANDLE                          DeviceHandle = INVALID_HANDLE_VALUE;

unsigned int moreHIDDevices = TRUE;
unsigned int HIDDeviceFound = FALSE;

unsigned int terminaAbruptaEInstantaneamenteElPrograma = 0;

// Buffer para leer el Input del usuario
char InputBuffer[MAX_BUFFER_SIZE];
// Guarda la opcion del menu
char Menu_Choice;

void Load_HID_Library(void) {
	hHID = LoadLibrary("HID.DLL");
	if (!hHID) {
		printf("Failed to load HID.DLL\n");
		return;
	}

	HidD_GetProductString = (PHidD_GetProductString)GetProcAddress(hHID, "HidD_GetProductString");
	HidD_GetHidGuid = (PHidD_GetHidGuid)GetProcAddress(hHID, "HidD_GetHidGuid");
	HidD_GetAttributes = (PHidD_GetAttributes)GetProcAddress(hHID, "HidD_GetAttributes");
	HidD_SetFeature = (PHidD_SetFeature)GetProcAddress(hHID, "HidD_SetFeature");
	HidD_GetFeature = (PHidD_GetFeature)GetProcAddress(hHID, "HidD_GetFeature");

	if (!HidD_GetProductString
		|| !HidD_GetAttributes
		|| !HidD_GetHidGuid
		|| !HidD_SetFeature
		|| !HidD_GetFeature) {
		printf("Couldn't find one or more HID entry points\n");
		return;
	}
}

int Open_Device(void) {
	HDEVINFO							DeviceInfoSet;
	GUID								InterfaceClassGuid;
	SP_DEVICE_INTERFACE_DATA			DeviceInterfaceData;
	PSP_DEVICE_INTERFACE_DETAIL_DATA	pDeviceInterfaceDetailData;
	HIDD_ATTRIBUTES						Attributes;
	DWORD								Result;
	DWORD								MemberIndex = 0;
	DWORD								Required;

	//Validar si se "cargó" la biblioteca (DLL)
	if (!hHID)
		return (0);

	//Obtener el Globally Unique Identifier (GUID) para dispositivos HID
	HidD_GetHidGuid(&InterfaceClassGuid);
	//Sacarle a Windows la información sobre todos los dispositivos HID instalados y activos en el sistema
	// ... almacenar esta información en una estructura de datos de tipo HDEVINFO
	DeviceInfoSet = SetupDiGetClassDevs(&InterfaceClassGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
	if (DeviceInfoSet == INVALID_HANDLE_VALUE)
		return (0);

	//Obtener la interfaz de comunicación con cada uno de los dispositivos para preguntarles información específica
	DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	while (!HIDDeviceFound) {
		// ... utilizando la variable MemberIndex ir preguntando dispositivo por dispositivo ...
		moreHIDDevices = SetupDiEnumDeviceInterfaces(DeviceInfoSet, NULL, &InterfaceClassGuid,
			MemberIndex, &DeviceInterfaceData);
		if (!moreHIDDevices) {
			// ... si llegamos al fin de la lista y no encontramos al dispositivo ==> terminar y marcar error
			SetupDiDestroyDeviceInfoList(DeviceInfoSet);
			return (0); //No more devices found
		}
		else {
			//Necesitamos preguntar, a través de la interfaz, el PATH del dispositivo, para eso ...
			// ... primero vamos a ver cuántos caracteres se requieren (Required)
			Result = SetupDiGetDeviceInterfaceDetail(DeviceInfoSet, &DeviceInterfaceData, NULL, 0, &Required, NULL);
			pDeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(Required);
			if (pDeviceInterfaceDetailData == NULL) {
				printf("Error en SetupDiGetDeviceInterfaceDetail\n");
				return (0);
			}
			//Ahora si, ya que el "buffer" fue preparado (pDeviceInterfaceDetailData{DevicePath}), vamos a preguntar PATH
			pDeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
			Result = SetupDiGetDeviceInterfaceDetail(DeviceInfoSet, &DeviceInterfaceData, pDeviceInterfaceDetailData,
				Required, NULL, NULL);
			if (!Result) {
				printf("Error en SetupDiGetDeviceInterfaceDetail\n");
				free(pDeviceInterfaceDetailData);
				return(0);
			}
			//Para este momento ya sabemos el PATH del dispositivo, ahora hay que preguntarle ...
			// ... su VID y PID, para ver si es con quien nos interesa comunicarnos
			printf("Found? ==> ");
			printf("Device: %s\n", pDeviceInterfaceDetailData->DevicePath);

			//Obtener un "handle" al dispositivo
			DeviceHandle = CreateFile(pDeviceInterfaceDetailData->DevicePath,
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				(LPSECURITY_ATTRIBUTES)NULL,
				OPEN_EXISTING,
				0,
				NULL);

			if (DeviceHandle == INVALID_HANDLE_VALUE) {
				printf("¡¡¡Error en el CreateFile!!!\n");
			}
			else {
				//Preguntar por los atributos del dispositivo
				Attributes.Size = sizeof(Attributes);
				Result = HidD_GetAttributes(DeviceHandle, &Attributes);
				if (!Result) {
					printf("Error en HIdD_GetAttributes\n");
					CloseHandle(DeviceHandle);
					free(pDeviceInterfaceDetailData);
					return(0);
				}
				//Analizar los atributos del dispositivo para verificar el VID y PID
				printf("MemberIndex=%d,VID=%04x,PID=%04x\n", MemberIndex, Attributes.VendorID, Attributes.ProductID);
				if ((Attributes.VendorID == RICH_VENDOR_ID) && (Attributes.ProductID == RICH_USBHID_GENIO_ID)) {
					printf("USB/HID GenIO ==> ");
					printf("Device: %s\n", pDeviceInterfaceDetailData->DevicePath);
					HIDDeviceFound = TRUE;
				}
				else
					CloseHandle(DeviceHandle);

			}
			MemberIndex++;
			free(pDeviceInterfaceDetailData);
			if (HIDDeviceFound) {
				printf("Dispositivo HID solicitado ... ¡¡¡localizado!!!, presione <ENTER>\n");
				getchar();
				printf("Vamos bien\n");
			}
		}
	}
	return(1);
}

void Close_Device(void) {
	if (DeviceHandle != NULL) {
		CloseHandle(DeviceHandle);
		DeviceHandle = NULL;
	}
}

int Touch_Device(void) {
	DWORD BytesRead = 0;
	DWORD BytesWritten = 0;
	unsigned char reporteEntrada[INPUT_REPORT_SIZE + 1];
	unsigned char reporteSalida[OUTPUT_REPORT_SIZE + 1];
	int status = 0;
	static unsigned char dato = 0x01;
	static unsigned char numLED = 1;

	if (DeviceHandle == NULL)	//Validar que haya comunicacion con el dispositivo
		return 0;

	printf("Voy por un reporte ...\n");

	reporteSalida[0] = 0x00; // Dummy
	reporteSalida[1] = 0x00; // Resetear comando a enviar
	reporteSalida[2] = 0;	 // Resetear payload del comando
	
	// Dependiendo de la opcion seleccionada, armar el reporte correspondiente
	// Dentro de reporteSalida[1] enviar el valor del comando deseado
	switch (Menu_Choice) {
	case 1: // Modificar LED
		break;
	case 2: // Leer estado de los Switches
		reporteSalida[1] = 0x81;
		break;
	case 3: // Leer matriculas
		break;
	}

	// Escribir el comando al micro
	status = WriteFile(DeviceHandle, reporteSalida, OUTPUT_REPORT_SIZE + 1, &BytesWritten, NULL);
	if (!status)
		printf("Error en el WriteFile %d %d\n", GetLastError(), BytesWritten);
	else
		printf("Escritos %d\n", BytesWritten);
	
	// Vaciar reporteEntrada para leer nuevos datos
	memset(&reporteEntrada, 0, INPUT_REPORT_SIZE + 1);
	
	// Leer la respuesta del micro para el comando enviado
	status = ReadFile(DeviceHandle, reporteEntrada, INPUT_REPORT_SIZE + 1, &BytesRead, NULL);
	if (!status)
		printf("Error en el ReadFile: %d\n", GetLastError());
	else
		// Procesar los datos que el micro regresa dependiendo de la opcion seleccionada
		switch (Menu_Choice) {
		case 1: // Modificar LED
			break;
		case 2: // Leer estado de los Switches
			printf("--------------------------------------------------------\n");
			printf("Current Switch Status:\n");
			printf("Switch 1: %s\n", (((unsigned char)reporteEntrada[2] & 0x01) == 0x01) ? "OFF" : "ON");
			printf("Switch 2: %s\n", (((unsigned char)reporteEntrada[2] & 0x02) == 0x02) ? "OFF" : "ON");
			printf("Switch 3: %s\n", (((unsigned char)reporteEntrada[2] & 0x04) == 0x04) ? "OFF" : "ON");
			printf("--------------------------------------------------------\n");

			break;
		case 3: // Leer matriculas
			break;
		}

	return status;
}

void Request_VID_PID() {
	
	memset(InputBuffer, 0, MAX_BUFFER_SIZE);

	printf("--------------------------------------------------------\n");
	printf("Ingresa el Vendor ID (VID):\n");
	printf("> ");
	if (fgets(InputBuffer, MAX_BUFFER_SIZE, stdin) == NULL) {
		printf("fgets error\numChars");
	}
	else {
		char* ptr = strchr(InputBuffer, '\n');
		if (ptr)
		{
			//if new line found replace with null character
			*ptr = '\0';
		}
	}
	// Convertir de ASCII HEX a USHORT
	RICH_VENDOR_ID = (USHORT)strtol(InputBuffer, NULL, 16);

	printf("Ingresa el Product ID (PID)\n");
	printf("> ");
	if (fgets(InputBuffer, MAX_BUFFER_SIZE, stdin) == NULL) {
		printf("fgets error\numChars");
	}
	else {
		char* ptr = strchr(InputBuffer, '\n');
		if (ptr)
		{
			//if new line found replace with null character
			*ptr = '\0';
		}
	}
	// Convertir de ASCII HEX a USHORT
	RICH_USBHID_GENIO_ID = (USHORT)strtoul(InputBuffer, NULL, 16);
}

void Request_Menu_Choice() {
	memset(InputBuffer, 0, MAX_BUFFER_SIZE);

	printf("--------------------------------------------------------\n");
	printf("Ingresa el numero de la operacion a realizar:\n");
	printf("1 Modificar LED\n");
	printf("2 Leer Switches\n");
	printf("3 Leer Matriculas\n");
	printf("4 Salir del programa\n");
	printf("> ");
	if (fgets(InputBuffer, MAX_BUFFER_SIZE, stdin) == NULL) {
		printf("fgets error\numChars");
	}
	else {
		char* ptr = strchr(InputBuffer, '\n');
		if (ptr)
		{
			//if new line found replace with null character
			*ptr = '\0';
		}
	}
	// Convertir de ASCII a int
	Menu_Choice = atoi(InputBuffer);
	printf("--------------------------------------------------------\n");
}

void main() {
	Load_HID_Library();
	Request_VID_PID();
	if (Open_Device()) {
		/*while ((!_kbhit())
			&& (!terminaAbruptaEInstantaneamenteElPrograma)) {
			Touch_Device();
			Sleep(500);
		}*/

		// Ejecuta la opcion del usuario
		while (1) {
			Request_Menu_Choice();
			if (Menu_Choice > 3 || Menu_Choice < 1) {
				break;
			}
			Touch_Device();
		}
	}
	else {
		printf(">:(\n");
	}
	Close_Device();
}