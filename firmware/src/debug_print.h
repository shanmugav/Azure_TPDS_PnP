/*
    \file   debug_print.h

    \brief  debug_console printer

    (c) 2018 Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip software and any
    derivatives exclusively with Microchip products. It is your responsibility to comply with third party
    license terms applicable to your use of third party software (including open source software) that
    may accompany Microchip software.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
    IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
    FOR A PARTICULAR PURPOSE.

    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
    HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
    THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
    CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
    OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
    SOFTWARE.
*/

#ifndef DEBUG_PRINT_H
#define DEBUG_PRINT_H


#include <stdbool.h>

#define CSI_RESET   "\33[0m"
#define CSI_BLACK   "\33[30m"
#define CSI_RED     "\33[31m"
#define CSI_GREEN   "\33[32m"
#define CSI_YELLOW  "\33[33m"
#define CSI_BLUE    "\33[34m"
#define CSI_MAGENTA "\33[35m"
#define CSI_CYAN    "\33[36m"
#define CSI_WHITE   "\33[37m"
#define CSI_INVERSE "\33[7m"
#define CSI_NORMAL  "\33[27m"
#define CSI_CLS     "\33[2J"
#define CSI_HOME    "\33[1;1H"

typedef enum
{
    SEVERITY_NONE,    // No debug trace
    SEVERITY_ERROR,   // Error
    SEVERITY_WARN,    // Error and Warn
    SEVERITY_DEBUG,   // Error, Warn, and Good
    SEVERITY_INFO,    // print everything
    SEVERITY_TRACE
} debug_severity_t;

typedef enum
{
    LEVEL_INFO,
    LEVEL_GOOD,
    LEVEL_WARN,
    LEVEL_ERROR
} debug_errorLevel_t;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
static const char*     severity_strings[] = {
    CSI_WHITE "  NONE" CSI_WHITE,
    CSI_RED " ERROR" CSI_WHITE,
    CSI_YELLOW "  WARN" CSI_WHITE,
    CSI_CYAN " DEBUG" CSI_WHITE,
    CSI_WHITE "  INFO" CSI_NORMAL CSI_WHITE,
    CSI_MAGENTA "  TRACE" CSI_NORMAL CSI_WHITE,
};

static const char* level_strings[] = {
    CSI_WHITE " INFO" CSI_WHITE,
    CSI_GREEN " GOOD" CSI_WHITE,
    CSI_YELLOW " WARN" CSI_WHITE,
    CSI_RED CSI_INVERSE "ERROR" CSI_NORMAL CSI_WHITE,
};
#pragma GCC diagnostic pop

debug_severity_t debug_getSeverity(void);

void debug_setSeverity(debug_severity_t debug_level);
void debug_printer(debug_severity_t debug_severity, debug_errorLevel_t error_level, const char* format, ...);
void debug_setPrefix(const char* prefix);
void debug_init(const char* prefix);
void debug_disable(bool disable);
void debug_printf(const char* format, ...);
//void debug_printf(const char* format, ...);


#define debug_print(fmt, ...)                                                        \
    do                                                                               \
    {                                                                                \
        if (IOT_DEBUG_PRINT)                                                         \
            debug_printer(SEVERITY_DEBUG, LEVEL_INFO, fmt CSI_RESET, ##__VA_ARGS__); \
    } while (0)

#define debug_printGood(fmt, ...)                                                    \
    do                                                                               \
    {                                                                                \
        if (IOT_DEBUG_PRINT)                                                         \
            debug_printer(SEVERITY_DEBUG, LEVEL_GOOD, fmt CSI_RESET, ##__VA_ARGS__); \
    } while (0)

#define debug_printWarn(fmt, ...)                                                   \
    do                                                                              \
    {                                                                               \
        if (IOT_DEBUG_PRINT)                                                        \
            debug_printer(SEVERITY_WARN, LEVEL_WARN, fmt CSI_RESET, ##__VA_ARGS__); \
    } while (0)

#define debug_printError(fmt, ...)                                                    \
    do                                                                                \
    {                                                                                 \
        if (IOT_DEBUG_PRINT)                                                          \
            debug_printer(SEVERITY_ERROR, LEVEL_ERROR, fmt CSI_RESET, ##__VA_ARGS__); \
    } while (0)

#define debug_printInfo(fmt, ...)                                                   \
    do                                                                              \
    {                                                                               \
        if (IOT_DEBUG_PRINT)                                                        \
            debug_printer(SEVERITY_INFO, LEVEL_INFO, fmt CSI_RESET, ##__VA_ARGS__); \
    } while (0)

#define debug_printTrace(fmt, ...)                                                   \
    do                                                                               \
    {                                                                                \
        if (IOT_DEBUG_PRINT)                                                         \
            debug_printer(SEVERITY_TRACE, LEVEL_INFO, fmt CSI_RESET, ##__VA_ARGS__); \
    } while (0)
#endif   // DEBUG_PRINT_H
