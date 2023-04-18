/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/

#ifndef SAMPLE_CONFIG_H
#define SAMPLE_CONFIG_H

#ifdef __cplusplus
extern   "C" {
#endif

#define ENABLE_ATECC608B        1
 
/*
 * enables code that reads temp/pressure/humidity from Weather Click sensor
 */    
//#define USE_WEATHER_CLICK       

 /*
 * define USE_THERMOSTAT_MODELID allows use of publicly published thermostat
 * DTDL model
 * 
 * comment USE_THERMOSTAT_MODELID to send pressure and humidity telemetry.
 * developer must define a "Weather Click" DTDL model.  
 * Example Model provided with instructions
 */
#define USE_THERMOSTAT_MODELID   

#define ENABLE_DPS_SAMPLE
#define USE_DEVICE_CERTIFICATE  1
    
#ifdef ENABLE_DPS_SAMPLE
/* Required when DPS is used.  */
#ifndef ENDPOINT
#define ENDPOINT                                    "global.azure-devices-provisioning.net"
#endif /* ENDPOINT */


    
//#ifndef ID_SCOPE
//#define ID_SCOPE                                    "0ne0060E2E7"
//#endif /* ID_SCOPE */
    
    
extern size_t idScope_len;
extern UCHAR* idScope;



extern UCHAR g_registration_id[32];
extern UINT g_registration_id_length;
#else
/* Required when DPS is not used.  */
/* These values can be picked from device connection string which is of format : HostName=<host1>;DeviceId=<device1>;SharedAccessKey=<key1>
HOST_NAME can be set to <host1>,
DEVICE_ID can be set to <device1>,
DEVICE_SYMMETRIC_KEY can be set to <key1>.  */
#define HOST_NAME                      "IoT-Hub-1213.azure-devices.net"
#define DEVICE_ID                      "U5-SAS"

#endif
#define DEVICE_SYMMETRIC_KEY           "+B8Vv2olUDh16BrsF60ZhfbbwFGxoNYGNLRrT0So8lQ="    
    
/*** Azure IoT embedded C SDK Configuration ***/
#define MODULE_ID              ""
          
    
#define NX_AZURE_IOT_STACK_SIZE                (2048)
#define NX_AZURE_IOT_THREAD_PRIORITY           (4) 
#define SAMPLE_STACK_SIZE                      (2048)
#define SAMPLE_THREAD_PRIORITY                 (16)
#define MAX_PROPERTY_COUNT                     (2)
#define SAMPLE_MAX_BUFFER                      (256)

#if (USE_DEVICE_CERTIFICATE == 1)

/* Using X509 certificate authenticate to connect to IoT Hub,
   set the device certificate as your device.  */

/* Device Key type. */
#ifndef DEVICE_KEY_TYPE
//#define DEVICE_KEY_TYPE                             NX_SECURE_X509_KEY_TYPE_RSA_PKCS1_DER
#ifdef ENABLE_ATECC608B
#define DEVICE_KEY_TYPE                             NX_SECURE_X509_KEY_TYPE_HARDWARE
#else
#define DEVICE_KEY_TYPE                             NX_SECURE_X509_KEY_TYPE_RSA_PKCS1_DER
#endif

#endif /* DEVICE_KEY_TYPE */

/* Required to print out certificate read from device */
#define ENABLE_PRINT_CERTIFICATE            1

/* Required if you need to read customer provisioned certificate. */
//#define GET_CUSTOMER_RE_PROVISONED_CERTIFICATE 1

#endif /* USE_DEVICE_CERTIFICATE */

#ifdef __cplusplus
}
#endif
#endif /* SAMPLE_CONFIG_H */