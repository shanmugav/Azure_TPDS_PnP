// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "string.h"
#include "azutil.h"
#include "nmdrv.h"
#include "led.h"
#include "dti.h"
#include "debug_print.h"
#include "azure-sdk-for-c/sdk/inc/azure/iot/az_iot_common.h"
#include "click_routines/click_interface.h"


static const az_span twin_request_id_span = AZ_SPAN_LITERAL_FROM_STR("initial_get");

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
extern az_iot_pnp_client pnp_client;
#else
extern az_iot_hub_client iothub_client;
#endif



extern volatile uint32_t telemetryInterval;

extern char deviceIpAddress;
#define MAX_PATIENT_SIZE 256

uint8_t strPatientName[MAX_PATIENT_SIZE];


// used by led.c to communicate LED state changes
LED_STATUS led_status;

extern uint16_t packet_identifier;

userdata_status_t userdata_status;

static char pnp_telemetry_topic_buffer[128];
static char pnp_telemetry_payload_buffer[128];

// use another set of buffers in case two telemetry collides
static char pnp_uart_telemetry_topic_buffer[128];
static char pnp_uart_telemetry_payload_buffer[128 + DTI_PAYLOADDATA_NUMBYTES];

static char pnp_property_topic_buffer[128];
static char pnp_property_payload_buffer[512];

static char command_topic_buffer[512];
static char command_resp_buffer[128];

// Plug and Play Connection Values
static uint32_t request_id_int = 0;
static char     request_id_buffer[16];

// IoT Plug and Play properties
#ifndef IOT_PLUG_AND_PLAY_MODEL_ID
static const az_span iot_hub_property_desired         = AZ_SPAN_LITERAL_FROM_STR("desired");
static const az_span iot_hub_property_desired_version = AZ_SPAN_LITERAL_FROM_STR("$version");
#endif

static const az_span telemetry_name_heart_rate_span = AZ_SPAN_LITERAL_FROM_STR("heartRate");

static const az_span telemetry_name_long       = AZ_SPAN_LITERAL_FROM_STR("telemetry_Lng");
static const az_span telemetry_name_bool       = AZ_SPAN_LITERAL_FROM_STR("telemetry_Bool");
static const az_span telemetry_name_string_1   = AZ_SPAN_LITERAL_FROM_STR("telemetry_Str_1");
static const az_span telemetry_name_string_2   = AZ_SPAN_LITERAL_FROM_STR("telemetry_Str_2");
static const az_span telemetry_name_string_3   = AZ_SPAN_LITERAL_FROM_STR("telemetry_Str_3");
static const az_span telemetry_name_string_4   = AZ_SPAN_LITERAL_FROM_STR("telemetry_Str_4");


// Telemetry Interval writable property
static const az_span property_telemetry_interval_span = AZ_SPAN_LITERAL_FROM_STR("telemetryInterval");

// LED Properties
static const az_span led_property_name_span  = AZ_SPAN_LITERAL_FROM_STR("ledStatus");

// Patient Name
static const az_span patient_name_property_name_span = AZ_SPAN_LITERAL_FROM_STR("patientName");

// Debug Level
static const az_span debug_level_property_name_span = AZ_SPAN_LITERAL_FROM_STR("debugLevel");

// IP Address property
static const az_span ip_address_property_name_span = AZ_SPAN_LITERAL_FROM_STR("ipAddress");

#define DISABLE_HEARTRATE       0x1



static const az_span disable_telemetry_name_span = AZ_SPAN_LITERAL_FROM_STR("disableTelemetry");
static uint32_t telemetry_disable_flag = 0;

static const az_span resp_success_span                     = AZ_SPAN_LITERAL_FROM_STR("Success");

// Command
static const az_span command_name_reboot_span              = AZ_SPAN_LITERAL_FROM_STR("reboot");
static const az_span command_reboot_delay_payload_span     = AZ_SPAN_LITERAL_FROM_STR("delay");
static const az_span command_status_span                   = AZ_SPAN_LITERAL_FROM_STR("status");
static const az_span command_resp_empty_delay_payload_span = AZ_SPAN_LITERAL_FROM_STR("Delay time is empty. Specify 'delay' in period format (PT5S for 5 sec)");
static const az_span command_resp_bad_payload_span         = AZ_SPAN_LITERAL_FROM_STR("Delay time in wrong format. Specify 'delay' in period format (PT5S for 5 sec)");
static const az_span command_resp_error_processing_span    = AZ_SPAN_LITERAL_FROM_STR("Error processing command");
static const az_span command_resp_not_supported_span       = AZ_SPAN_LITERAL_FROM_STR("{\"Status\":\"Unsupported Command\"}");

static const az_span command_name_echoMsg_span               = AZ_SPAN_LITERAL_FROM_STR("echoMsg");
static const az_span command_echoMsg_payload_span            = AZ_SPAN_LITERAL_FROM_STR("echoMsgString");
static const az_span command_resp_empty_echoMsg_payload_span = AZ_SPAN_LITERAL_FROM_STR("Message string is empty. Specify string.");
static const az_span command_resp_alloc_error_echoMsg_span   = AZ_SPAN_LITERAL_FROM_STR("Failed to allocate memory for the message.");

static SYS_TIME_HANDLE reboot_task_handle = SYS_TIME_HANDLE_INVALID;

/**********************************************
* Initialize twin property data structure
**********************************************/
void init_twin_data(twin_properties_t* twin_properties)
{
    twin_properties->flag.as_uint16     = LED_FLAG_EMPTY;
    twin_properties->version_num        = 0;
    twin_properties->desired_led_status = LED_OFF_STATUS;
    twin_properties->reported_led_status   = LED_OFF_STATUS;
    twin_properties->desired_debugLevel         = SEVERITY_INFO;
    twin_properties->reported_debugLevel         = SEVERITY_INFO;
    twin_properties->desired_patientName[0]         =  '\0';
    twin_properties->reported_patientName[0]         =  '\0';
    twin_properties->ip_address[0]      = '\0';
    twin_properties->app_property_1     = 0;
    twin_properties->app_property_2     = 0;
    twin_properties->app_property_3     = 0;
    twin_properties->app_property_4     = 0;
    twin_properties->telemetry_disable_flag = 0;
}

/**************************************
 Start JSON_BUILDER for JSON Document
 This creates a new JSON with "{"
*************************************/
az_result start_json_object(
    az_json_writer* jw,
    az_span         az_span_buffer)
{
    RETURN_ERR_IF_FAILED(az_json_writer_init(jw, az_span_buffer, NULL));
    RETURN_ERR_IF_FAILED(az_json_writer_append_begin_object(jw));
    return AZ_OK;
}

/**********************************************
* End JSON_BUILDER for JSON Document
* This adds "}" to the JSON
**********************************************/
az_result end_json_object(
    az_json_writer* jw)
{
    RETURN_ERR_IF_FAILED(az_json_writer_append_end_object(jw));
    return AZ_OK;
}

/**********************************************
*	Add a JSON key-value pair with int32 data
*	e.g. "property_name" : property_val (number)
**********************************************/
az_result append_json_property_int32(
    az_json_writer* jw,
    az_span         property_name_span,
    int32_t         property_val)
{
    RETURN_ERR_IF_FAILED(az_json_writer_append_property_name(jw, property_name_span));
    RETURN_ERR_IF_FAILED(az_json_writer_append_int32(jw, property_val));
    return AZ_OK;
}

/**********************************************
*	Add a JSON key-value pair with float data
*	e.g. "property_name" : property_val (number)
**********************************************/
az_result append_json_property_float(
    az_json_writer* jw,
    az_span         property_name_span,
    float           property_val)
{
    RETURN_ERR_IF_FAILED(az_json_writer_append_property_name(jw, property_name_span));
    RETURN_ERR_IF_FAILED(az_json_writer_append_double(jw, (double)property_val, 5));
    return AZ_OK;
}

/**********************************************
*	Add a JSON key-value pair with double data
*	e.g. "property_name" : property_val (number)
**********************************************/
az_result append_json_property_double(
    az_json_writer* jw,
    az_span         property_name_span,
    double          property_val)
{
    RETURN_ERR_IF_FAILED(az_json_writer_append_property_name(jw, property_name_span));
    RETURN_ERR_IF_FAILED(az_json_writer_append_double(jw, property_val, 5));
    return AZ_OK;
}

/**********************************************
*	Add a JSON key-value pair with long data
*	e.g. "property_name" : property_val (number)
**********************************************/
az_result append_json_property_long(
    az_json_writer* jw,
    az_span         property_name_span,
    int64_t         property_val)
{
    RETURN_ERR_IF_FAILED(az_json_writer_append_property_name(jw, property_name_span));
    RETURN_ERR_IF_FAILED(az_json_writer_append_double(jw, property_val, 5));
    return AZ_OK;
}

/**********************************************
*	Add a JSON key-value pair with bool data
*	e.g. "property_name" : property_val (number)
**********************************************/
az_result append_json_property_bool(
    az_json_writer* jw,
    az_span         property_name_span,
    bool            property_val)
{
    RETURN_ERR_IF_FAILED(az_json_writer_append_property_name(jw, property_name_span));
    RETURN_ERR_IF_FAILED(az_json_writer_append_bool(jw, property_val));
    return AZ_OK;
}

/**********************************************
* Add a JSON key-value pair with string data
* e.g. "property_name" : "property_val (string)"
**********************************************/
az_result append_json_property_string(
    az_json_writer* jw,
    az_span         property_name_span,
    az_span         property_val_span)
{
    RETURN_ERR_IF_FAILED(az_json_writer_append_property_name(jw, property_name_span));
    RETURN_ERR_IF_FAILED(az_json_writer_append_string(jw, property_val_span));
    return AZ_OK;
}

/**********************************************
* Add JSON for writable property response with int32 data
* e.g. "property_name" : property_val_int32
**********************************************/
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
static az_result append_reported_property_response_int32(
    az_json_writer* jw,
    az_span         property_name_span,
    int32_t         property_val,
    int32_t         ack_code,
    int32_t         ack_version,
    az_span         ack_description_span)
{
    RETURN_ERR_IF_FAILED(az_iot_pnp_client_property_builder_begin_reported_status(&pnp_client,
                                                                                  jw,
                                                                                  property_name_span,
                                                                                  ack_code,
                                                                                  ack_version,
                                                                                  ack_description_span));

    RETURN_ERR_IF_FAILED(az_json_writer_append_int32(jw, property_val));
    RETURN_ERR_IF_FAILED(az_iot_pnp_client_property_builder_end_reported_status(&pnp_client, jw));
    return AZ_OK;
}
#endif

/**********************************************
* Add JSON for writable property response with string data
* e.g. "property_name" : property_val_int32
**********************************************/
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
static az_result append_reported_property_response_string(
    az_json_writer* jw,
    az_span         property_name_span,
    az_span         property_val_span,
    int32_t         ack_code,
    int32_t         ack_version,
    az_span         ack_description_span)
{
    RETURN_ERR_IF_FAILED(az_iot_pnp_client_property_builder_begin_reported_status(&pnp_client,
                                                                                  jw,
                                                                                  property_name_span,
                                                                                  ack_code,
                                                                                  ack_version,
                                                                                  ack_description_span));

    RETURN_ERR_IF_FAILED(az_json_writer_append_string(jw, property_val_span));
    RETURN_ERR_IF_FAILED(az_iot_pnp_client_property_builder_end_reported_status(&pnp_client, jw));
    return AZ_OK;
}
#endif
/**********************************************
* Build sensor telemetry JSON
**********************************************/
az_result build_sensor_telemetry_message(
    az_span* out_payload_span,
    int32_t  heartrate)
{
    az_json_writer jw;
    memset(&pnp_telemetry_payload_buffer, 0, sizeof(pnp_telemetry_payload_buffer));
    RETURN_ERR_IF_FAILED(start_json_object(&jw, AZ_SPAN_FROM_BUFFER(pnp_telemetry_payload_buffer)));

    if ((telemetry_disable_flag & DISABLE_HEARTRATE) == 0)
    {
        RETURN_ERR_IF_FAILED(append_json_property_int32(&jw, telemetry_name_heart_rate_span, heartrate));
    }


    RETURN_ERR_IF_FAILED(end_json_object(&jw));
    *out_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);
    return AZ_OK;
}

/**********************************************
* Create JSON document for command error response
* e.g.
* {
*   "status":500,
*   "payload":{
*     "Status":"Unsupported Command"
*   }
* }
**********************************************/
static az_result build_command_error_response_payload(
    az_span  response_span,
    az_span  status_string_span,
    az_span* response_payload_span)
{
    az_json_writer jw;

    // Build the command response payload with error status
    RETURN_ERR_IF_FAILED(start_json_object(&jw, response_span));
    RETURN_ERR_IF_FAILED(append_json_property_string(&jw, command_status_span, status_string_span));
    RETURN_ERR_IF_FAILED(end_json_object(&jw));
    *response_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);
    return AZ_OK;
}

/**********************************************
* Create JSON document for reboot command with status message
* e.g.
* {
*   "status":200,
*   "payload":{
*     "status":"Success",
*     "delay":100
*   }
* }
**********************************************/
static az_result build_reboot_command_resp_payload(
    az_span  response_span,
    int      reboot_delay,
    az_span* response_payload_span)
{
    az_json_writer jw;

    // Build the command response payload
    RETURN_ERR_IF_FAILED(start_json_object(&jw, response_span));
    RETURN_ERR_IF_FAILED(append_json_property_string(&jw, command_status_span, resp_success_span));
    RETURN_ERR_IF_FAILED(append_json_property_int32(&jw, command_reboot_delay_payload_span, reboot_delay));
    RETURN_ERR_IF_FAILED(end_json_object(&jw));
    *response_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);
    return AZ_OK;
}

/**********************************************
* Create JSON document for command response with status message
* e.g.
* {
*   "status":200,
*   "payload":{
*     "status":"Success"
*   }
* }
**********************************************/
static az_result build_command_resp_payload(az_span response_span, az_span* response_payload_span)
{
    az_json_writer jw;

    // Build the command response payload
    RETURN_ERR_IF_FAILED(start_json_object(&jw, response_span));
    RETURN_ERR_IF_FAILED(append_json_property_string(&jw, command_status_span, resp_success_span));
    RETURN_ERR_IF_FAILED(end_json_object(&jw));
    *response_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);
    return AZ_OK;
}

#define NUM_PAYLOAD_CHUNKS 8

/**********************************************
* Read sensor data and send telemetry to cloud
**********************************************/
az_result send_telemetry_message(void)
{
    az_result rc = AZ_OK;
    az_span   telemetry_payload_span;
    int32_t heartrate=0;
  
    extern volatile int8_t last_heart_rate;
     heartrate9_example();
     heartrate=last_heart_rate;

    if ((telemetry_disable_flag & (DISABLE_HEARTRATE)))
    {
        debug_printTrace("AZURE: Telemetry disabled");
        return rc;
    }


    RETURN_ERR_WITH_MESSAGE_IF_FAILED(
        build_sensor_telemetry_message(&telemetry_payload_span, heartrate),
        "Failed to build sensor telemetry JSON payload");

    debug_printGood("AZURE: %s", az_span_ptr(telemetry_payload_span));

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    rc = az_iot_pnp_client_telemetry_get_publish_topic(&pnp_client,
                                                       AZ_SPAN_EMPTY,
#else
    rc = az_iot_hub_client_telemetry_get_publish_topic(&iothub_client,
#endif
                                                       NULL,
                                                       pnp_telemetry_topic_buffer,
                                                       sizeof(pnp_telemetry_topic_buffer),
                                                       NULL);

    if (az_result_succeeded(rc))
    {
        CLOUD_publishData((uint8_t*)pnp_telemetry_topic_buffer,
                          az_span_ptr(telemetry_payload_span),
                          az_span_size(telemetry_payload_span),
                          1);
    }

    return rc;
}



/**********************************************
* Process LED Update/Patch
* Sets  LED based on Device Twin from IoT Hub
**********************************************/
void update_leds(
    LED_STATUS status)
{
    // If desired properties are not set, send current LED states.
    // Otherwise, set LED state based on Desired property
    if (status == LED_OFF_STATUS) 
    {
      
            STATUS_LED_Set();
       
    }
    else if (status == LED_ON_STATUS) 
    {
      
            STATUS_LED_Clear();
       
    }

   
}

/**********************************************
* Send the response of the command invocation
**********************************************/
static int send_command_response(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    az_iot_pnp_client_command_request* request,
#else
    az_iot_hub_client_method_request* request,
#endif
    uint16_t status,
    az_span  response)
{
    size_t command_topic_length=0;
    // Get the response topic to publish the command response
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    int32_t rc = az_iot_pnp_client_commands_response_get_publish_topic(
        &pnp_client,
#else
    int rc = az_iot_hub_client_methods_response_get_publish_topic(
        &iothub_client,
#endif
        request->request_id,
        status,
        command_topic_buffer,
        sizeof(command_topic_buffer),
        &command_topic_length);

    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "AZURE: Unable to get command response publish topic");



    CLOUD_publishData((uint8_t*)command_topic_buffer,
                     az_span_ptr(response),
                    az_span_size(response),
                   1);


    return 0;
}

void reboot_task_callback(uintptr_t context)
{
    debug_printWarn("AZURE: Rebooting...");
    NVIC_SystemReset();
}

/**********************************************
*	Handle reboot command
**********************************************/
static az_result process_reboot_command(
    az_span   payload_span,
    az_span   response_span,
    az_span*  out_response_span,
    uint16_t* out_response_status)
{
    az_result      ret              = AZ_OK;
    char           reboot_delay[32] = {0};
    az_json_reader jr;
    *out_response_status = AZ_IOT_STATUS_SERVER_ERROR;

    if (az_span_size(payload_span) > 0)
    {
        debug_printInfo("AZURE: %s() : Payload %s %d", __func__, az_span_ptr(payload_span), az_span_size(payload_span));
        strcpy(reboot_delay,az_span_ptr(payload_span)+1);
        reboot_delay[strlen(reboot_delay)-1]='\0';
        RETURN_ERR_IF_FAILED(az_json_reader_init(&jr, payload_span, NULL));
        
    }
    else
    {
        debug_printError("AZURE: %s() : Payload Empty", __func__);
    }

    if (strlen(reboot_delay) == 0)
    {
        debug_printError("AZURE: Reboot Delay not found");

        ret = build_command_error_response_payload(response_span,
                                                   command_resp_empty_delay_payload_span,
                                                   out_response_span);

        *out_response_status = AZ_IOT_STATUS_BAD_REQUEST;
    }
    else if (reboot_delay[0] != 'P' || reboot_delay[1] != 'T' || reboot_delay[strlen(reboot_delay) - 1] != 'S')
    {
        debug_printError("AZURE: Reboot Delay wrong format %s %d",reboot_delay,strlen(reboot_delay));

        ret = build_command_error_response_payload(response_span,
                                                   command_resp_bad_payload_span,
                                                   out_response_span);

        *out_response_status = AZ_IOT_STATUS_BAD_REQUEST;
    }
    else
    {
        int reboot_delay_seconds = atoi((const char*)&reboot_delay[2]);

        RETURN_ERR_IF_FAILED(build_reboot_command_resp_payload(response_span,
                                                               reboot_delay_seconds,
                                                               out_response_span));

        *out_response_status = AZ_IOT_STATUS_ACCEPTED;

        debug_printInfo("AZURE: Scheduling reboot in %d sec", reboot_delay_seconds);

        reboot_task_handle = SYS_TIME_CallbackRegisterMS(reboot_task_callback,
                                                         0,
                                                         reboot_delay_seconds * 1000,
                                                         SYS_TIME_SINGLE);

        if (reboot_task_handle == SYS_TIME_HANDLE_INVALID)
        {
            debug_printError("AZURE: Failed to schedule reboot timer");
        }
    }

    return ret;
}

/**********************************************
 *	Handle send message command
 **********************************************/
static az_result process_echoMsg_command(
    az_span   payload_span,
    az_span   response_span,
    az_span*  out_response_span,
    uint16_t* out_response_status)
{
    az_result      ret           = AZ_OK;
    size_t         spanSize      = -1;
    char    messageString[256];
    
    az_json_reader jr;

    *out_response_status = AZ_IOT_STATUS_SERVER_ERROR;

    if (az_span_size(payload_span) > 0)
    {
        debug_printInfo("AZURE: %s() : Payload %s", __func__, az_span_ptr(payload_span));

        RETURN_ERR_IF_FAILED(az_json_reader_init(&jr, payload_span, NULL));

        while (jr.token.kind != AZ_JSON_TOKEN_END_OBJECT)
        {
            if (jr.token.kind == AZ_JSON_TOKEN_PROPERTY_NAME)
            {
                if (az_json_token_is_text_equal(&jr.token, command_echoMsg_payload_span))
                {
                    if (az_result_failed(ret = az_json_reader_next_token(&jr)))
                    {
                        debug_printError("AZURE: Error getting next token");
                        break;
                    }
                    else
                    {
                        spanSize = (size_t)az_span_size(jr.token.slice);
                        break;
                    }

                    break;
                }
            } else if (az_result_failed(ret = az_json_reader_next_token(&jr)))
            {
                debug_printError("AZURE: Error getting next token");
                break;
            }
        }
    }
    else
    {
        debug_printError("AZURE: %s() : Payload Empty", __func__);
    }

    if (spanSize == -1)
    {
        debug_printError("AZURE: Message string not found");

        ret = build_command_error_response_payload(response_span,
                                                   command_resp_empty_echoMsg_payload_span,
                                                   out_response_span);

        *out_response_status = AZ_IOT_STATUS_BAD_REQUEST;
    }
    else if (spanSize > APP_BUFFER_SIZE)
    {
        debug_printError("AZURE: Message too big for TX Buffer %lu", spanSize);

        ret = build_command_error_response_payload(response_span,
                                                   command_resp_alloc_error_echoMsg_span,
                                                   out_response_span);

        *out_response_status = AZ_IOT_STATUS_BAD_REQUEST;
    }
   
    else
    {
        RETURN_ERR_IF_FAILED(az_json_token_get_string(&jr.token, messageString, spanSize + 1, NULL));

        strlcat(messageString, "\4", (spanSize + 1)%255);
        debug_disable(true);
        SYS_CONSOLE_Message(0, messageString);
        debug_disable(false);
        RETURN_ERR_IF_FAILED(build_command_resp_payload(response_span, out_response_span));
        //debug_printInfo("AZURE: %s() : Out Payload %s", __func__, az_span_ptr(*out_response_span));
        *out_response_status = AZ_IOT_STATUS_ACCEPTED;
    }

   

    return ret;
}

/**********************************************
* Process Command
**********************************************/
az_result process_direct_method_command(
    uint8_t* payload,
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    az_iot_pnp_client_command_request* command_request)
#else
    az_iot_hub_client_method_request* method_request)
#endif
{
    az_result rc                = AZ_OK;
    uint16_t  response_status   = AZ_IOT_STATUS_BAD_REQUEST;   // assume error
    az_span   command_resp_span = AZ_SPAN_FROM_BUFFER(command_resp_buffer);
    az_span   payload_span      = az_span_create_from_str((char*)payload);

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    debug_printInfo("AZURE: Processing Command '%s'", command_request->command_name);
#else
    debug_printInfo("AZURE: Processing Command '%s'", method_request->name);
#endif

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    if (az_span_is_content_equal(command_name_reboot_span, command_request->command_name))
#else
    if (az_span_is_content_equal(command_name_reboot_span, method_request->name))
#endif
    {
        rc = process_reboot_command(payload_span, command_resp_span, &command_resp_span, &response_status);

        if (az_result_failed(rc))
        {
            debug_printError("AZURE: Failed process_reboot_command, status 0x%08x", rc);
            if (az_span_size(command_resp_span) == 0)
            {
                // if response is empty, payload was not in the right format.
                if (az_result_failed(rc = build_command_error_response_payload(command_resp_span,
                                                                               command_resp_error_processing_span,
                                                                               &command_resp_span)))
                {
                    debug_printError("AZURE: Failed to build error response. (0x%08x)", rc);
                }
            }
        }
    }
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    else if (az_span_is_content_equal(command_name_echoMsg_span, command_request->command_name))
#else
    else if (az_span_is_content_equal(command_name_echoMsg_span, method_request->name))
#endif
    {
        rc = process_echoMsg_command(payload_span, command_resp_span, &command_resp_span, &response_status);

        if (az_result_failed(rc))
        {
            debug_printError("AZURE: Failed process_echoMsg_command, status 0x%08x", rc);
            if (az_span_size(command_resp_span) == 0)
            {
                // if response is empty, payload was not in the right format.
                if (az_result_failed(rc = build_command_error_response_payload(
                            command_resp_span, command_resp_error_processing_span, &command_resp_span)))
                {
                    debug_printError("AZURE: Failed to build error response. (0x%08x)", rc);
                }
            }
        }
    }
    else
    {
        rc = AZ_ERROR_NOT_SUPPORTED;
        // Unsupported command
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
        debug_printError("AZURE: Unsupported command received: %s.", az_span_ptr(command_request->command_name));
#else
        debug_printError("AZURE: Unsupported command received: %s.", az_span_ptr(method_request->name));
#endif
        // if response is empty, payload was not in the right format.
        if (az_result_failed(rc = build_command_error_response_payload(command_resp_span,
                                                                       command_resp_not_supported_span,
                                                                       &command_resp_span)))
        {
            debug_printError("AZURE: Failed to build error response. (0x%08x)", rc);
        }
    }
    
    #ifdef IOT_PLUG_AND_PLAY_MODEL_ID
        rc = send_command_response(command_request, response_status, command_resp_span);
        if (rc != 0)
    #else
        if ((rc = send_command_response(method_request, response_status, command_resp_span)) != 0)
    #endif
        {
        debug_printError("AZURE: Unable to send %d response, status %d", response_status, rc);
        }
        
    return rc;
}

#ifndef IOT_PLUG_AND_PLAY_MODEL_ID
// from az_iot_pnp_client_property.c
az_result json_child_token_move(az_json_reader* ref_jr, az_span property_name)
{
    do
    {
        if ((ref_jr->token.kind == AZ_JSON_TOKEN_PROPERTY_NAME) && az_json_token_is_text_equal(&(ref_jr->token), property_name))
        {
            RETURN_ERR_IF_FAILED(az_json_reader_next_token(ref_jr));

            return AZ_OK;
        }
        else if (ref_jr->token.kind == AZ_JSON_TOKEN_BEGIN_OBJECT)
        {
            if (az_result_failed(az_json_reader_skip_children(ref_jr)))
            {
                return AZ_ERROR_UNEXPECTED_CHAR;
            }
        }
        else if (ref_jr->token.kind == AZ_JSON_TOKEN_END_OBJECT)
        {
            return AZ_ERROR_ITEM_NOT_FOUND;
        }
    } while (az_result_succeeded(az_json_reader_next_token(ref_jr)));

    return AZ_ERROR_ITEM_NOT_FOUND;
}


static az_result get_twin_version(
    az_json_reader*                      ref_json_reader,
    az_iot_hub_client_twin_response_type response_type,
    int32_t*                             out_version)
{
    RETURN_ERR_IF_FAILED(az_json_reader_next_token(ref_json_reader));

    if (ref_json_reader->token.kind != AZ_JSON_TOKEN_BEGIN_OBJECT)
    {
        return AZ_ERROR_UNEXPECTED_CHAR;
    }

    RETURN_ERR_IF_FAILED(az_json_reader_next_token(ref_json_reader));

    if (response_type == AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_GET)
    {
        RETURN_ERR_IF_FAILED(json_child_token_move(ref_json_reader, iot_hub_property_desired));
        RETURN_ERR_IF_FAILED(az_json_reader_next_token(ref_json_reader));
    }

    RETURN_ERR_IF_FAILED(json_child_token_move(ref_json_reader, iot_hub_property_desired_version));
    RETURN_ERR_IF_FAILED(az_json_token_get_int32(&ref_json_reader->token, out_version));

    return AZ_OK;
}

static az_result get_twin_desired(
    az_json_reader*                      ref_json_reader,
    az_iot_hub_client_twin_response_type response_type)
{
    RETURN_ERR_IF_FAILED(az_json_reader_next_token(ref_json_reader));

    if (ref_json_reader->token.kind != AZ_JSON_TOKEN_BEGIN_OBJECT)
    {
        return AZ_ERROR_UNEXPECTED_CHAR;
    }

    RETURN_ERR_IF_FAILED(az_json_reader_next_token(ref_json_reader));

    if (response_type == AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_GET)
    {
        RETURN_ERR_IF_FAILED(json_child_token_move(ref_json_reader, iot_hub_property_desired));
    }

    return AZ_OK;
}

#endif
/**********************************************
* Parse Desired Property (Writable Property)
* Respond by updating Writable Property with IoT Plug and Play convention
* https://docs.microsoft.com/en-us/azure/iot-pnp/concepts-convention#writable-properties
* e.g.
* "reported": {
*   "telemetryInterval": {
*     "ac": 200,
*     "av": 13,
*     "ad": "Success",
*     "value": 60
* }
**********************************************/
az_result process_device_twin_property(
    uint8_t*           topic,
    uint8_t*           payload,
    twin_properties_t* twin_properties)
{
    az_result rc;
    az_span   property_topic_span;
    az_span   payload_span;
   

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    az_span component_name_span;

    az_iot_pnp_client_property_response property_response;
#else
    az_iot_hub_client_twin_response property_response;
#endif
    az_json_reader jr;

    property_topic_span = az_span_create(topic, strlen((char*)topic));
    payload_span        = az_span_create_from_str((char*)payload);

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    rc = az_iot_pnp_client_property_parse_received_topic(&pnp_client,
#else
    rc = az_iot_hub_client_twin_parse_received_topic(&iothub_client,
#endif
                                                         property_topic_span,
                                                         &property_response);

    if (az_result_succeeded(rc))
    {
        debug_printTrace("AZURE: Property Topic   : %s", az_span_ptr(property_topic_span));
        debug_printTrace("AZURE: Property Type    : %d", property_response.response_type);
        debug_printTrace("AZURE: Property Payload : %s", (char*)payload);
    }
    else
    {
        debug_printError("AZURE: Failed to parse property topic 0x%08x.", rc);
        debug_printError("AZURE: Topic: '%s' Payload: '%s'", (char*)topic, (char*)payload);
        return rc;
    }

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    if (property_response.response_type == AZ_IOT_PNP_CLIENT_PROPERTY_RESPONSE_TYPE_GET)
#else
    if (property_response.response_type == AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_GET)
#endif
    {
        if (az_span_is_content_equal_ignoring_case(property_response.request_id, twin_request_id_span))
        {
            debug_printInfo("AZURE: INITIAL GET Received");
            twin_properties->flag.is_initial_get = 1;
        }
        else
        {
            debug_printInfo("AZURE: Property GET Received");
        }
    }
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    else if (property_response.response_type == AZ_IOT_PNP_CLIENT_PROPERTY_RESPONSE_TYPE_DESIRED_PROPERTIES)
#else
    else if (property_response.response_type == AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_DESIRED_PROPERTIES)
#endif
    {
        debug_printInfo("AZURE: Property DESIRED Status %d Version %s",
                        property_response.status,
                        az_span_ptr(property_response.version));
    }
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    else if (property_response.response_type == AZ_IOT_PNP_CLIENT_PROPERTY_RESPONSE_TYPE_REPORTED_PROPERTIES)
#else
    else if (property_response.response_type == AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_REPORTED_PROPERTIES)
#endif
    {
        if (!az_iot_status_succeeded(property_response.status))
        {
            debug_printInfo("AZURE: Property REPORTED Status %d Version %s",
                            property_response.status,
                            az_span_ptr(property_response.version));
        }

        // This is an acknowledgement from the service that it received our properties. No need to respond.
        return rc;
    }
    else
    {
        debug_printInfo("AZURE: Type %d Status %d ID %s Version %s",
                        property_response.response_type,
                        property_response.status,
                        az_span_ptr(property_response.request_id),
                        az_span_ptr(property_response.version));
    }

    rc = az_json_reader_init(&jr,
                             payload_span,
                             NULL);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "az_json_reader_init() for get version failed");

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID

    rc = az_iot_pnp_client_property_get_property_version(&pnp_client,
                                                         &jr,
                                                         property_response.response_type,
                                                         &twin_properties->version_num);

    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "az_iot_pnp_client_property_get_property_version() failed");
    twin_properties->flag.version_found = 1;

#else
    rc = get_twin_version(&jr,
                          property_response.response_type,
                          &twin_properties->version_num);

    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "get_twin_version() failed");
    twin_properties->flag.version_found = 1;

#endif

    rc = az_json_reader_init(&jr,
                             payload_span,
                             NULL);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "az_json_reader_init() failed");


    while (az_result_succeeded(az_iot_pnp_client_property_get_next_component_property(
        &pnp_client,
        &jr,
        property_response.response_type,
        &component_name_span)))
    {
        if (az_json_token_is_text_equal(&jr.token, property_telemetry_interval_span))
        {
            uint32_t data;
            // found writable property to adjust telemetry interval
            RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
            RETURN_ERR_IF_FAILED(az_json_token_get_uint32(&jr.token, &data));
            twin_properties->flag.telemetry_interval_found = 1;
            telemetryInterval                              = data;
        }
       
        else if (az_json_token_is_text_equal(&jr.token, debug_level_property_name_span))
        {
            // found writable property to control Yellow LED
            RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
            RETURN_ERR_IF_FAILED(az_json_token_get_int32(&jr.token,
                                                         &twin_properties->desired_debugLevel));
            
            twin_properties->flag.debug_level_found = 1;
            
        }

        else if (az_json_token_is_text_equal(&jr.token, disable_telemetry_name_span))
        {
            // found writable property : Disable Telemetry
            RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
            RETURN_ERR_IF_FAILED(az_json_token_get_uint32(&jr.token,
                                                          &twin_properties->telemetry_disable_flag));
            twin_properties->flag.telemetry_disable_found = 1;
        }

       else if (az_json_token_is_text_equal(&jr.token, patient_name_property_name_span))
        {
            // found writable property to control Yellow LED
            RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
            size_t spanSize = (size_t)az_span_size(jr.token.slice);
            size_t size     = (MAX_PATIENT_SIZE < (spanSize+1)) ?  MAX_PATIENT_SIZE : (spanSize + 1);
            snprintf(strPatientName, size, "%s", az_span_ptr(jr.token.slice));
            RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
            strcpy(twin_properties->desired_patientName,strPatientName);
            twin_properties->flag.patient_name_found=1;
        }
         else if (az_json_token_is_text_equal(&jr.token, led_property_name_span))
        {
            RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
            RETURN_ERR_IF_FAILED(az_json_token_get_int32(&jr.token,
                                                         &twin_properties->desired_led_status));
            led_status = twin_properties->desired_led_status;
            twin_properties->flag.led_found = 1;
        }
        else
        {
            char   buffer[32];
            size_t spanSize = (size_t)az_span_size(jr.token.slice);
            size_t size     = sizeof(buffer) < spanSize ? sizeof(buffer) : spanSize;
            snprintf(buffer, size, "%s", az_span_ptr(jr.token.slice));

            debug_printWarn("AZURE: Received unknown property '%s'", buffer);
        }
        RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
    }

    return rc;
}



/**********************************************
* Create AZ Span for Reported Property Request ID 
**********************************************/
static az_span get_request_id(void)
{
    az_span remainder;
    az_span out_span = az_span_create((uint8_t*)request_id_buffer,
                                      sizeof(request_id_buffer));

    az_result rc = az_span_u32toa(out_span,
                                  request_id_int++,
                                  &remainder);

    EXIT_WITH_MESSAGE_IF_FAILED(rc, "Failed to get request id");

    return az_span_slice(out_span, 0, az_span_size(out_span) - az_span_size(remainder));
}

/**********************************************
* Send Reported Property 
**********************************************/
az_result send_reported_property(
    twin_properties_t* twin_properties)
{
   az_result      rc;
    az_json_writer jw;
    az_span        identifier_span;


    if (twin_properties->flag.as_uint16 == 0)
    {
        // Nothing to do.
        debug_printTrace("AZURE: No property update");
        return AZ_OK;
    }

    debug_printTrace("AZURE: Sending Property flag 0x%x", twin_properties->flag.as_uint16);

    // Clear buffer and initialize JSON Payload. This creates "{"
    memset(pnp_property_payload_buffer, 0, sizeof(pnp_property_payload_buffer));
    az_span payload_span = AZ_SPAN_FROM_BUFFER(pnp_property_payload_buffer);

    rc = start_json_object(&jw, payload_span);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "AZURE:Unable to initialize json writer for property PATCH");

if (twin_properties->flag.is_initial_get || twin_properties->flag.ip_address_updated != 0)
    {
        
        
         if (az_result_failed(
                rc = append_reported_property_response_string(
                    &jw,
                    ip_address_property_name_span,
                    az_span_create_from_str((char*)&deviceIpAddress),
                    AZ_IOT_STATUS_OK,
                    1,
                    resp_success_span)))
        {
            debug_printError("AZURE: Unable to add property for IP Address, return code  0x%08x", rc);
            return rc;
        }
       
    }
    
    if (twin_properties->flag.is_initial_get)
    {
        if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
                rc = append_reported_property_response_int32(
                    &jw,
                    property_telemetry_interval_span,
                    telemetryInterval,
                    AZ_IOT_STATUS_OK,
                    1,
                    resp_success_span)))
#else
                rc = append_json_property_int32(
                    &jw,
                    property_telemetry_interval_span,
                    telemetryInterval)))
#endif
        {
            debug_printError("AZURE: Unable to add property for telemetry interval, return code 0x%08x", rc);
            return rc;
        }
                
      if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
                rc = append_reported_property_response_int32(
                    &jw,
                    debug_level_property_name_span,
                    (uint32_t)debug_getSeverity(),
                    AZ_IOT_STATUS_OK,
                    1,
                    resp_success_span)))
#else
                rc = append_json_property_int32(
                    &jw,
                    debug_level_property_name_span,
                    (uint32_t)debug_getSeverity)))
#endif
        {
            debug_printError("AZURE: Unable to add property for Debug Level, return code 0x%08x", rc);
            return rc;
        }
                
                if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
                rc = append_reported_property_response_int32(
                    &jw,
                    led_property_name_span,
                    (uint32_t)1,
                    AZ_IOT_STATUS_OK,
                    1,
                    resp_success_span)))

#endif
        {
            debug_printError("AZURE: Unable to add property for Debug Level, return code 0x%08x", rc);
            return rc;
        }

                if (az_result_failed(
                rc = append_reported_property_response_string(
                    &jw,
                    patient_name_property_name_span,
                    az_span_create_from_str((char*)&twin_properties->desired_patientName),
                    AZ_IOT_STATUS_OK,
                    1,
                    resp_success_span)))
        {
            debug_printError("AZURE: Unable to add property for IP Address, return code  0x%08x", rc);
            return rc;
        }

        
    }
    else
    {
       
        if (twin_properties->flag.telemetry_interval_found)
    {
        if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
                rc = append_reported_property_response_int32(
                    &jw,
                    property_telemetry_interval_span,
                    telemetryInterval,
                    AZ_IOT_STATUS_OK,
                    twin_properties->version_num,
                    resp_success_span)))
#else
                rc = append_json_property_int32(
                    &jw,
                    property_telemetry_interval_span,
                    telemetryInterval)))
#endif
        {
            debug_printError("AZURE: Unable to add property for telemetry interval, return code 0x%08x", rc);
            return rc;
        }
    }
   
    // Add Yellow LED to the reported property
    // Example with integer Enum
    
    

    // Set debug level
    if (twin_properties->flag.debug_level_found)
    {
        debug_setSeverity((debug_severity_t) (twin_properties->desired_debugLevel)%6);

        if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
                rc = append_reported_property_response_int32(
                    &jw,
                    debug_level_property_name_span,
                    (uint32_t)debug_getSeverity(),
                    AZ_IOT_STATUS_OK,
                    twin_properties->version_num,
                    resp_success_span)))
#else
                rc = append_json_property_int32(
                    &jw,
                    debug_level_property_name_span,
                    (uint32_t)debug_getSeverity)))
#endif
        {
            debug_printError("AZURE: Unable to add property for Debug Level, return code 0x%08x", rc);
            return rc;
        }
    }
          // Get/Set Patient Name.
    if (twin_properties->flag.patient_name_found)
    {
        if (az_result_failed(
                rc = append_reported_property_response_string(
                    &jw,
                    patient_name_property_name_span,
                    az_span_create_from_str((char*)&twin_properties->desired_patientName),
                    AZ_IOT_STATUS_OK,
                    twin_properties->version_num,
                    resp_success_span)))
        {
            debug_printError("AZURE: Unable to add property for IP Address, return code  0x%08x", rc);
            return rc;
        }
  
    }


    // Set debug level
    if (twin_properties->flag.led_found)
    {
        led_status=(twin_properties->desired_led_status%3);
        //update led here...
         update_leds(led_status);

        if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
                rc = append_reported_property_response_int32(
                    &jw,
                    led_property_name_span,
                    (uint32_t)led_status,
                    AZ_IOT_STATUS_OK,
                    twin_properties->version_num,
                    resp_success_span)))

#endif
        {
            debug_printError("AZURE: Unable to add property for Debug Level, return code 0x%08x", rc);
            return rc;
        }
       
    }
        
    }
   

    // Close JSON Payload (appends "}")
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    if (az_result_failed(rc = az_iot_pnp_client_property_builder_end_reported_status(&pnp_client, &jw)))
#else
    if (az_result_failed(rc = az_json_writer_append_end_object(&jw)))
#endif
    {
        debug_printError("AZURE: Unable to append end object, return code  0x%08x", rc);
        return rc;
    }

    az_span property_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);
   
    if (az_span_size(property_payload_span) > sizeof(pnp_property_payload_buffer))
    {
        debug_printError("AZURE: Payload too long : %d", az_span_size(property_payload_span));
        return AZ_ERROR_NOT_ENOUGH_SPACE;
    }

    // Publish the reported property payload to IoT Hub
    identifier_span = get_request_id();

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    rc = az_iot_pnp_client_property_patch_get_publish_topic(&pnp_client,
#else
    rc = az_iot_hub_client_twin_patch_get_publish_topic(&iothub_client,
#endif
                                                            identifier_span,
                                                            pnp_property_topic_buffer,
                                                            sizeof(pnp_property_topic_buffer),
                                                            NULL);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "AZURE:Failed to get property PATCH topic");

    // Send the reported property

    CLOUD_publishData((uint8_t*)pnp_property_topic_buffer,
                     az_span_ptr(property_payload_span),
                   az_span_size(property_payload_span),
                  1);

    return rc;
}

/**********************************************
* Send Reported Property 
**********************************************/
az_result send_reported_property_twin(
    twin_properties_t* twin_properties)
{
    az_result      rc;
    az_json_writer jw;
    az_span        identifier_span;
    if (twin_properties->flag.as_uint16 == 0)
    {
        // Nothing to do.
        debug_printTrace("AZURE: No property update");
        return AZ_OK;
    }

    debug_printTrace("AZURE: Sending Property flag 0x%x", twin_properties->flag.as_uint16);

    // Clear buffer and initialize JSON Payload. This creates "{"
    memset(pnp_property_payload_buffer, 0, sizeof(pnp_property_payload_buffer));
    az_span payload_span = AZ_SPAN_FROM_BUFFER(pnp_property_payload_buffer);

    rc = start_json_object(&jw, payload_span);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "AZURE:Unable to initialize json writer for property PATCH");

    // Add IP Address
    if (twin_properties->flag.is_initial_get || twin_properties->flag.ip_address_updated != 0)
    {
         if (az_result_failed(
                rc = append_reported_property_response_string(
                    &jw,
                    ip_address_property_name_span,
                    az_span_create_from_str((char*)&deviceIpAddress),
                    AZ_IOT_STATUS_OK,
                    (twin_properties->version_num<=0)?1:twin_properties->version_num,
                    resp_success_span)))
        {
            debug_printError("AZURE: Unable to add property for IP Address, return code  0x%08x", rc);
            return rc;
        }
       
       shared_networking_params.reported=1;
         
    }
 
       
    if (twin_properties->flag.telemetry_interval_found)
  {
    if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
            rc = append_reported_property_response_int32(
                &jw,
                property_telemetry_interval_span,
                telemetryInterval,
                AZ_IOT_STATUS_OK,
                twin_properties->version_num,
                resp_success_span)))
#else
            rc = append_json_property_int32(
                &jw,
                property_telemetry_interval_span,
                telemetryInterval)))
#endif
    {
        debug_printError("AZURE: Unable to add property for telemetry interval, return code 0x%08x", rc);
        return rc;
    }
}
else if (twin_properties->flag.is_initial_get)
{
    if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
            rc = append_reported_property_response_int32(
                &jw,
                property_telemetry_interval_span,
                telemetryInterval,
                AZ_IOT_STATUS_OK,
                1,
                resp_success_span)))
#else
            rc = append_json_property_int32(
                &jw,
                property_telemetry_interval_span,
                telemetryInterval)))
#endif
    {
        debug_printError("AZURE: Unable to add property for telemetry interval, return code 0x%08x", rc);
        return rc;
    }
}
 
// Set debug level
if (twin_properties->flag.debug_level_found)
{
    debug_setSeverity((debug_severity_t) (twin_properties->desired_debugLevel)%6);

    if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
            rc = append_reported_property_response_int32(
                &jw,
                debug_level_property_name_span,
                (uint32_t)debug_getSeverity(),
                AZ_IOT_STATUS_OK,
                twin_properties->version_num,
                resp_success_span)))
#else
            rc = append_json_property_int32(
                &jw,
                debug_level_property_name_span,
                (uint32_t)debug_getSeverity)))
#endif
    {
        debug_printError("AZURE: Unable to add property for Debug Level, return code 0x%08x", rc);
        return rc;
    }
}
else if (twin_properties->flag.is_initial_get)
{


    if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
                rc = append_reported_property_response_int32(
                    &jw,
                    debug_level_property_name_span,
                    (uint32_t)debug_getSeverity(),
                    AZ_IOT_STATUS_OK,
                    1,
                    resp_success_span)))
#else
                rc = append_json_property_int32(
                    &jw,
                    debug_level_property_name_span,
                    (uint32_t)debug_getSeverity)))
#endif
        {
            debug_printError("AZURE: Unable to add property for Debug Level, return code 0x%08x", rc);
            return rc;
        }
}
          // Get/Set Patient Name.
    if (twin_properties->flag.patient_name_found)
    {
        if (az_result_failed(
                rc = append_reported_property_response_string(
                    &jw,
                    patient_name_property_name_span,
                    az_span_create_from_str((char*)&twin_properties->desired_patientName),
                    AZ_IOT_STATUS_OK,
                    twin_properties->version_num,
                    resp_success_span)))
        {
            debug_printError("AZURE: Unable to add property for IP Address, return code  0x%08x", rc);
            return rc;
        }

    }
else if (twin_properties->flag.is_initial_get)
{
       if (az_result_failed(
                rc = append_reported_property_response_string(
                    &jw,
                    patient_name_property_name_span,
                    az_span_create_from_str((char*)&twin_properties->desired_patientName),
                    AZ_IOT_STATUS_OK,
                    1,
                    resp_success_span)))
        {
            debug_printError("AZURE: Unable to add property for IP Address, return code  0x%08x", rc);
            return rc;
        }

        
}


    // Set debug level
    if (twin_properties->flag.led_found)
    {
        led_status=(twin_properties->desired_led_status)%3;
        //update led here...
         update_leds(led_status);

        if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
                rc = append_reported_property_response_int32(
                    &jw,
                    led_property_name_span,
                    (uint32_t)led_status,
                    AZ_IOT_STATUS_OK,
                    twin_properties->version_num,
                    resp_success_span)))

#endif
        {
            debug_printError("AZURE: Unable to add property for Debug Level, return code 0x%08x", rc);
            return rc;
        }
       
    }
    else if (twin_properties->flag.is_initial_get)
{
        led_status=0;
        update_leds(led_status);
                if (az_result_failed(
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
                rc = append_reported_property_response_int32(
                    &jw,
                    led_property_name_span,
                    (uint32_t)led_status,
                    AZ_IOT_STATUS_OK,
                    1,
                    resp_success_span)))

#endif
        {
            debug_printError("AZURE: Unable to add property for Debug Level, return code 0x%08x", rc);
            return rc;
        }
}
   

    // Close JSON Payload (appends "}")
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    if (az_result_failed(rc = az_iot_pnp_client_property_builder_end_reported_status(&pnp_client, &jw)))
#else
    if (az_result_failed(rc = az_json_writer_append_end_object(&jw)))
#endif
    {
        debug_printError("AZURE: Unable to append end object, return code  0x%08x", rc);
        return rc;
    }

    az_span property_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);
   
    if (az_span_size(property_payload_span) > sizeof(pnp_property_payload_buffer))
    {
        debug_printError("AZURE: Payload too long : %d", az_span_size(property_payload_span));
        return AZ_ERROR_NOT_ENOUGH_SPACE;
    }

    // Publish the reported property payload to IoT Hub
    identifier_span = get_request_id();

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    rc = az_iot_pnp_client_property_patch_get_publish_topic(&pnp_client,
#else
    rc = az_iot_hub_client_twin_patch_get_publish_topic(&iothub_client,
#endif
                                                            identifier_span,
                                                            pnp_property_topic_buffer,
                                                            sizeof(pnp_property_topic_buffer),
                                                            NULL);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "AZURE:Failed to get property PATCH topic");

    // Send the reported property

    CLOUD_publishData((uint8_t*)pnp_property_topic_buffer,
                     az_span_ptr(property_payload_span),
                   az_span_size(property_payload_span),
                  1);

    return rc;
}

bool process_telemetry_command(int cmdIndex, char* data)
{
    az_result      rc = AZ_OK;
    az_json_writer jw;
    char           telemetry_name_buffer[64];
    az_span        telemetry_name_span;
    az_span        telemetry_payload_span;

    memset(&pnp_uart_telemetry_payload_buffer, 0, sizeof(pnp_uart_telemetry_payload_buffer));
    start_json_object(&jw, AZ_SPAN_FROM_BUFFER(pnp_uart_telemetry_payload_buffer));

    switch (cmdIndex)
    {
        case 0:
            // Reset the DTI command buffer pointer to the start of the array
            
            return true;
        break;
        case 1:
        case 2:
        case 3:
        case 4:
        {
            // integer
            // A signed 4-byte integer
            int32_t data_i = strtoll(data, 0, 16);
            sprintf(telemetry_name_buffer, "telemetry_Int_%d", cmdIndex);
            telemetry_name_span = az_span_create_from_str((char*)telemetry_name_buffer);
            append_json_property_int32(&jw, telemetry_name_span, data_i);
        }
        break;

        case 5:
        case 6:
        {
            // double
            // An IEEE 8-byte floating point
            uint64_t data_d = strtoll(data, 0, 16);
            sprintf(telemetry_name_buffer, "telemetry_Dbl_%d", cmdIndex - 4);
            telemetry_name_span = az_span_create_from_str((char*)telemetry_name_buffer);
            append_json_property_double(&jw, telemetry_name_span, data_d);
        }
        break;
        case 7:
        case 8:
        {
            // float
            // An IEEE 4-byte floating point
            uint32_t data_l = strtol(data, 0, 16);

            float negative = ((data_l >> 31) & 0x1) ? -1.0000 : 1.0000;
            uint32_t exponent = (data_l & 0x7F800000) >> 23;
            uint32_t mantissa = data_l &  0x007FFFFF;

            float data_f = (float)(1.00000 + mantissa  / (2 << 22));
            data_f  = (float)(negative * (data_f * (2 << ((exponent - 127) - 1))));

            sprintf(telemetry_name_buffer, "telemetry_Flt_%d", cmdIndex - 6);
            telemetry_name_span = az_span_create_from_str((char*)telemetry_name_buffer);
            append_json_property_float(&jw, telemetry_name_span, data_f);
        }
        break;
        case 9:
        {
            // long
            // A signed 8-byte integer
            int64_t data_l = (int64_t)strtoll(data, 0, 16);
            append_json_property_long(&jw, telemetry_name_long, data_l);
        }
        break;
        case 10:
        {
            // bool
            bool bValue;

            if (strcmp(data, "true") == 0)
            {
                bValue = true;
            }
            else if (strcmp(data, "false") == 0)
            {
                bValue = false;
            }
            else
            {
                debug_printError("AZURE: Case sensitive boolean value not 'true' or 'false' : %s", data);
                break;
            }
            append_json_property_bool(&jw, telemetry_name_bool, bValue);
        }
        break;
        case 11:
        {
            // string #1
            append_json_property_string(&jw, telemetry_name_string_1, az_span_create_from_str(data));
        }
        break;
        case 12:
        {
            // string #2
            append_json_property_string(&jw, telemetry_name_string_2, az_span_create_from_str(data));
        }
        break;
        case 13:
        {
            // string #3
            append_json_property_string(&jw, telemetry_name_string_3, az_span_create_from_str(data));
        }
        break;
        case 14:
        {
            // string #4
            append_json_property_string(&jw, telemetry_name_string_4, az_span_create_from_str(data));
        }
        break;
    }

    end_json_object(&jw);
    telemetry_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    rc = az_iot_pnp_client_telemetry_get_publish_topic(&pnp_client,
                                                       AZ_SPAN_EMPTY,
#else
    rc = az_iot_hub_client_telemetry_get_publish_topic(&iothub_client,
#endif
                                                       NULL,
                                                       pnp_uart_telemetry_topic_buffer,
                                                       sizeof(pnp_uart_telemetry_topic_buffer),
                                                       NULL);

    if (az_result_succeeded(rc))
    {

      CLOUD_publishData((uint8_t*)pnp_uart_telemetry_topic_buffer,
                    az_span_ptr(telemetry_payload_span),
                   az_span_size(telemetry_payload_span),
                   1);
    }

    return true;

}

bool send_property_from_uart(int cmdIndex, char* data)
{
    twin_properties_t  twin_properties;
    int32_t data_i;

    init_twin_data(&twin_properties);

    switch (cmdIndex)
    {
        case 1:
            data_i = strtoll(data, 0, 16);
            twin_properties.flag.app_property_1_updated = 1;
            twin_properties.app_property_1 = data_i;
            break;
        case 2:
            data_i = strtoll(data, 0, 16);
            twin_properties.flag.app_property_2_updated = 1;
            twin_properties.app_property_2 = data_i;
            break;
        default:
            debug_printError("AZURE: Unknown property command");
            break;
    }

    if (twin_properties.flag.as_uint16 != 0)
    {
        send_reported_property(&twin_properties);
    }

    return true;
}


