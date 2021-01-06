/*******************************************************************************
 * Copyright (C) Maxim Integrated Products, Inc., All rights Reserved.
 *
 * This software is protected by copyright laws of the United States and
 * of foreign countries. This material may also be protected by patent laws
 * and technology transfer regulations of the United States and of foreign
 * countries. This software is furnished under a license agreement and/or a
 * nondisclosure agreement and may only be used or reproduced in accordance
 * with the terms of those agreements. Dissemination of this information to
 * any party or parties not specified in the license agreement and/or
 * nondisclosure agreement is expressly prohibited.
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL MAXIM INTEGRATED BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Maxim Integrated
 * Products, Inc. shall not be used except as stated in the Maxim Integrated
 * Products, Inc. Branding Policy.
 *
 * The mere transfer of this software does not imply any licenses
 * of trade secrets, proprietary technology, copyrights, patents,
 * trademarks, maskwork rights, or any other form of intellectual
 * property whatsoever. Maxim Integrated Products, Inc. retains all
 * ownership rights.
 *******************************************************************************
 */

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <board.h>
#include <core1.h>
#include <dma.h>
#include <mxc_delay.h>
#include <mxc_sys.h>
#include <nvic_table.h>
#include <stdint.h>
#include <string.h>
#include <tmr.h>

#include "max32666_ble.h"
#include "max32666_ble_command.h"
#include "max32666_ble_queue.h"
#include "max32666_data.h"
#include "max32666_debug.h"
#include "max32666_expander.h"
#include "max32666_fonts.h"
#include "max32666_i2c.h"
#include "max32666_lcd.h"
#include "max32666_lcd_images.h"
#include "max32666_led_button.h"
#include "max32666_max20303.h"
#include "max32666_max34417.h"
#include "max32666_qspi.h"
#include "max32666_sdcard.h"
#include "max32666_spi_dma.h"
#include "max32666_utils.h"
#include "maxrefdes178_definitions.h"
#include "maxrefdes178_version.h"


//-----------------------------------------------------------------------------
// Defines
//-----------------------------------------------------------------------------
#define S_MODULE_NAME   "main"


//-----------------------------------------------------------------------------
// Typedefs
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------
static volatile int core1_init_done = 0;
static volatile uint32_t timer_ms_tick = 0;

//static const mxc_gpio_cfg_t core1_swd_pin = MAX32666_CORE1_SWD_PIN;


//-----------------------------------------------------------------------------
// Local function declarations
//-----------------------------------------------------------------------------
static void core0_irq_init(void);
static void core1_irq_init(void);
static void run_application(void);
static void refresh_screen(void);
static int ms_timer_init(void);


//-----------------------------------------------------------------------------
// Function definitions
//-----------------------------------------------------------------------------
int main(void)
{
    int ret = 0;
    char version_string[10] = {0};

    // Set PORT1 and PORT2 rail to VDDIO
    MXC_GPIO0->vssel =  0x00;
    MXC_GPIO1->vssel =  0x00;

    ret = MXC_SEMA_Init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("MXC_SEMA_Init failed %d", ret);
        while(1);
    }

    device_info.device_version.max32666.major = S_VERSION_MAJOR;
    device_info.device_version.max32666.minor = S_VERSION_MINOR;
    device_info.device_version.max32666.build = S_VERSION_BUILD;
    snprintf(version_string, sizeof(version_string) - 1, "v%d.%d.%d", S_VERSION_MAJOR, S_VERSION_MINOR, S_VERSION_BUILD);
    PR_INFO("maxrefdes178_max32666 core0 %s [%s]", version_string, S_BUILD_TIMESTAMP);

    ret = i2c_master_init(MAX32666_I2C, I2C_SPEED);
    if (ret != E_NO_ERROR) {
        PR_ERROR("i2c_init failed %d", ret);
        while(1);
    }

    ret = max20303_init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("max20303_init failed %d", ret);
        while(1);
    }
    max20303_led_green(1);

    // To debug Core1 set alternate function 3
//    MXC_GPIO_Config(&core1_swd_pin);

   // BLE should init first since it is mischievous
   // BLE init somehow damages GPIO settings for P0.0, P0.23
    core0_irq_init();
    Core1_Start();

    for(uint32_t cnt = 20000000; !core1_init_done && cnt; cnt--) {
        if (cnt == 1) {
            PR_ERROR("timeout, reset");
            MXC_Delay(MXC_DELAY_MSEC(100));
            MXC_SYS_Reset_Periph(MXC_SYS_RESET_SYSTEM);
//            MXC_GCR->rstr0 = 0xffffffff;
        }
    }

    ret = MXC_SEMA_Init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("MXC_SEMA_Init failed %d", ret);
        while(1);
    }

    ret = expander_init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("expander_init failed %d", ret);
        max20303_led_red(1);
        while(1);
    }

    /* Select USB-TYpe-C Debug Connection to MAX78000-Video on IO expander */
    ret = expander_debug_select(DEBUGGER_SELECT_MAX78000_VIDEO);
    if (ret != E_NO_ERROR) {
        PR_ERROR("expander_debug_select failed %d", ret);
        max20303_led_red(1);
        while(1);
    }

    // Initialize DMA peripheral
    ret = MXC_DMA_Init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("MXC_DMA_Init failed %d", ret);
        max20303_led_red(1);
        while(1);
    }

    ret = lcd_init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("lcd_init failed %d", ret);
        max20303_led_red(1);
        while(1);
    }

//    ret = max34417_init();
//    if (ret != E_NO_ERROR) {
//        PR_ERROR("max34417_init failed %d", ret);
//        max20303_led_red(1);
//        while(1);
//    }

    ret = qspi_init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("qspi_init failed %d", ret);
        max20303_led_red(1);
        while(1);
    }

    ret = sdcard_init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("sdcard_init failed %d", ret);
//        max20303_led_red(1);
    }

    ret = led_button_init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("led_button_init failed %d", ret);
        max20303_led_red(1);
    }

    ret = ble_queue_init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("ble_queue_init failed %d", ret);
        max20303_led_red(1);
    }

    ret = ble_command_init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("ble_command_init failed %d", ret);
        max20303_led_red(1);
    }

    ret = ms_timer_init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("ms_timer_init failed %d", ret);
        max20303_led_red(1);
    }

    ret = MXC_SYS_GetUSN(device_info.device_serial_num.max32666, sizeof(device_info.device_serial_num.max32666));
    if (ret != E_NO_ERROR) {
        PR_ERROR("MXC_SYS_GetUSN failed %d", ret);
        max20303_led_red(1);
    }
    PR_INFO("MAX32666 Serial number: ");
    for (int i = 0; i < sizeof(device_info.device_serial_num.max32666); i++) {
        PR("%02X", device_info.device_serial_num.max32666[i]);
    }
    PR("\n");

    // TODO: get max78000_video and max78000_audio version and serial num
    // QSPI_PACKET_TYPE_VIDEO_VERSION_CMD QSPI_PACKET_TYPE_AUDIO_VERSION_CMD
    // QSPI_PACKET_TYPE_VIDEO_SERIAL_CMD QSPI_PACKET_TYPE_AUDIO_SERIAL_CMD

    // Print logo and version
    fonts_putSubtitle(LCD_WIDTH, LCD_HEIGHT, version_string, Font_16x26, RED, maxim_logo);
    lcd_drawImage(0, 0, LCD_WIDTH, LCD_HEIGHT, maxim_logo);

    PR_INFO("core 0 init completed");

    run_application();

    return E_NO_ERROR;
}

static void run_application(void)
{
    qspi_packet_type_e qspi_packet_type = 0;
    lcd_data.notification_color = BLUE;

    // Main application loop
    while (1) {
        // Handle received QSPI packets
        if (qspi_worker(&qspi_packet_type) == QSPI_STATUS_SUCCESS_RX) {
            switch(qspi_packet_type) {
            case QSPI_PACKET_TYPE_VIDEO_DATA_RES:
                refresh_screen();
                break;
            case QSPI_PACKET_TYPE_VIDEO_CLASSIFICATION_RES:
                if (device_status.classification_video.classification == CLASSIFICATION_UNKNOWN) {
                    lcd_data.subtitle_color = RED;
                    lcd_data.frame_color = RED;
                } else if (device_status.classification_video.classification == CLASSIFICATION_LOW_CONFIDENCE) {
                    lcd_data.subtitle_color = YELLOW;
                    lcd_data.frame_color = YELLOW;
                } else if (device_status.classification_video.classification == CLASSIFICATION_DETECTED) {
                    lcd_data.subtitle_color = GREEN;
                    lcd_data.frame_color = GREEN;
                } else if (device_status.classification_video.classification == CLASSIFICATION_NOTHING) {
                    lcd_data.frame_color = WHITE;
                }

                if (device_settings.enable_ble_send_classification && device_status.ble_connected) {
                    ble_command_send_single_packet(BLE_COMMAND_GET_MAX78000_VIDEO_CLASSIFICATION_RES,
                        sizeof(device_status.classification_video), (uint8_t *) &device_status.classification_video);
                }
                break;
            case QSPI_PACKET_TYPE_AUDIO_CLASSIFICATION_RES:
                timestamps.audio_result_received = timer_ms_tick;

                if (device_status.classification_audio.classification == CLASSIFICATION_UNKNOWN) {
                    lcd_data.toptitle_color = RED;
                } else if (device_status.classification_audio.classification == CLASSIFICATION_LOW_CONFIDENCE) {
                    lcd_data.toptitle_color = YELLOW;
                } else if (device_status.classification_audio.classification == CLASSIFICATION_DETECTED) {
                    lcd_data.toptitle_color = GREEN;

                    if (strcmp(device_status.classification_audio.result, "OFF") == 0) {
                        device_settings.enable_lcd = 0;
                        lcd_backlight(0);
                    } else if(strcmp(device_status.classification_audio.result, "ON") == 0) {
                        device_settings.enable_lcd = 1;
                        lcd_backlight(1);
                    } else if (strcmp(device_status.classification_audio.result, "GO") == 0) {
                        device_settings.enable_max78000_video_cnn = 1;
                    } else if(strcmp(device_status.classification_audio.result, "STOP") == 0) {
                        device_settings.enable_max78000_video_cnn = 0;
                    } else if (strcmp(device_status.classification_audio.result, "UP") == 0) {
                        device_settings.enable_lcd_probabilty = 1;
                    } else if(strcmp(device_status.classification_audio.result, "DOWN") == 0) {
                        device_settings.enable_lcd_probabilty = 0;
                    } else if (strcmp(device_status.classification_audio.result, "YES") == 0) {
                        device_settings.enable_lcd_statistics = 1;
                    } else if(strcmp(device_status.classification_audio.result, "NO") == 0) {
                        device_settings.enable_lcd_statistics = 0;
                    }
                }

//                refresh_screen();

                if (device_settings.enable_ble_send_classification && device_status.ble_connected) {
                    ble_command_send_single_packet(BLE_COMMAND_GET_MAX78000_AUDIO_CLASSIFICATION_RES,
                        sizeof(device_status.classification_audio), (uint8_t *) &device_status.classification_audio);
                }
                break;
            default:
                break;
            }
        }

        // Send periodic statistics
        if (device_settings.enable_ble_send_statistics && device_status.ble_connected) {
            if ((timer_ms_tick - timestamps.statistics_sent) > BLE_STATISTICS_INTERVAL) {
                timestamps.statistics_sent = timer_ms_tick;
                ble_command_send_single_packet(BLE_COMMAND_GET_STATISTICS_RES,
                    sizeof(device_status.statistics), (uint8_t *) &device_status.statistics);
            }
        }

        // If video is disabled, draw logo and refresh periodically
        if (!device_settings.enable_max78000_video &&
                ((timer_ms_tick - timestamps.screen_drew) > LCD_NO_VIDEO_DURATION)) {
            memcpy(lcd_data.buffer, maxim_logo, sizeof(lcd_data.buffer));
            refresh_screen();
        }

        // Handle BLE communication
        if (device_settings.enable_ble && device_status.ble_connected) {
            ble_command_worker();
        }

        // Check if BLE status changed
        if (device_status.ble_status_changed) {
            device_status.ble_status_changed = 0;

            ble_queue_flush();
            ble_command_reset();
            device_settings.enable_ble_send_classification = 0;
            device_settings.enable_ble_send_statistics = 0;

            if (device_status.ble_connected) {
                snprintf(lcd_data.notification, sizeof(lcd_data.notification) - 1,
                        "BLE %02X:%02X:%02X:%02X:%02X:%02X connected!",
                        device_status.ble_connected_peer_mac[5], device_status.ble_connected_peer_mac[4],
                        device_status.ble_connected_peer_mac[3], device_status.ble_connected_peer_mac[2],
                        device_status.ble_connected_peer_mac[1], device_status.ble_connected_peer_mac[0]);
                timestamps.notification_received = timer_ms_tick;
            } else {
                snprintf(lcd_data.notification, sizeof(lcd_data.notification) - 1, "BLE disconnected!");
                timestamps.notification_received = timer_ms_tick;
            }
        }
    }
}

static void refresh_screen(void)
{
    static char statistics_string[20] = {0};

    if (!device_settings.enable_lcd) {
        return;
    }

    if (device_settings.enable_lcd_statistics) {
        int line_pos = 3;

        snprintf(statistics_string, sizeof(statistics_string) - 1, "FPS:%.2f", (double)device_status.statistics.lcd_fps);
        fonts_putString(LCD_WIDTH, LCD_HEIGHT, 3, line_pos, statistics_string, Font_7x10, MAGENTA, 0, 0, lcd_data.buffer);
        line_pos += 12;

        snprintf(statistics_string, sizeof(statistics_string) - 1, "Bat:%d", device_status.statistics.battery_level);
        fonts_putString(LCD_WIDTH, LCD_HEIGHT, 3, line_pos, statistics_string, Font_7x10, MAGENTA, 0, 0, lcd_data.buffer);
        line_pos += 12;

        snprintf(statistics_string, sizeof(statistics_string) - 1, "vCap:%d", device_status.statistics.max78000_video.capture_duration_us / 1000);
        fonts_putString(LCD_WIDTH, LCD_HEIGHT, 3, line_pos, statistics_string, Font_7x10, MAGENTA, 0, 0, lcd_data.buffer);
        line_pos += 12;

        snprintf(statistics_string, sizeof(statistics_string) - 1, "vCNN:%d", device_status.statistics.max78000_video.cnn_duration_us / 1000);
        fonts_putString(LCD_WIDTH, LCD_HEIGHT, 3, line_pos, statistics_string, Font_7x10, MAGENTA, 0, 0, lcd_data.buffer);
        line_pos += 12;

        snprintf(statistics_string, sizeof(statistics_string) - 1, "vCom:%d", device_status.statistics.max78000_video.communication_duration_us / 1000);
        fonts_putString(LCD_WIDTH, LCD_HEIGHT, 3, line_pos, statistics_string, Font_7x10, MAGENTA, 0, 0, lcd_data.buffer);
        line_pos += 12;

        snprintf(statistics_string, sizeof(statistics_string) - 1, "vPow:%d", device_status.statistics.max78000_video_power_uw / 1000);
        fonts_putString(LCD_WIDTH, LCD_HEIGHT, 3, line_pos, statistics_string, Font_7x10, MAGENTA, 0, 0, lcd_data.buffer);
        line_pos += 12;

        snprintf(statistics_string, sizeof(statistics_string) - 1, "aCNN:%d", device_status.statistics.max78000_audio.cnn_duration_us / 1000);
        fonts_putString(LCD_WIDTH, LCD_HEIGHT, 3, line_pos, statistics_string, Font_7x10, MAGENTA, 0, 0, lcd_data.buffer);
        line_pos += 12;

        snprintf(statistics_string, sizeof(statistics_string) - 1, "aPow:%d", device_status.statistics.max78000_audio_power_uw / 1000);
        fonts_putString(LCD_WIDTH, LCD_HEIGHT, 3, line_pos, statistics_string, Font_7x10, MAGENTA, 0, 0, lcd_data.buffer);
        line_pos += 12;
    }

    // Draw FaceID frame and result
    if (device_settings.enable_max78000_video_cnn) {
        if (device_status.classification_video.classification != CLASSIFICATION_NOTHING) {
            strncpy(lcd_data.subtitle, device_status.classification_video.result, sizeof(lcd_data.subtitle) - 1);
            fonts_putSubtitle(LCD_WIDTH, LCD_HEIGHT, lcd_data.subtitle, Font_16x26, lcd_data.subtitle_color, lcd_data.buffer);
        }

        fonts_drawRectangle(LCD_WIDTH, LCD_HEIGHT, FACEID_RECTANGLE_X1 - 0, FACEID_RECTANGLE_Y1 - 0,
                FACEID_RECTANGLE_X2 + 0, FACEID_RECTANGLE_Y2 + 0, lcd_data.frame_color, lcd_data.buffer);
        fonts_drawRectangle(LCD_WIDTH, LCD_HEIGHT, FACEID_RECTANGLE_X1 - 1, FACEID_RECTANGLE_Y1 - 1,
                FACEID_RECTANGLE_X2 + 1, FACEID_RECTANGLE_Y2 + 1, lcd_data.frame_color, lcd_data.buffer);
        fonts_drawRectangle(LCD_WIDTH, LCD_HEIGHT, FACEID_RECTANGLE_X1 - 2, FACEID_RECTANGLE_Y1 - 2,
                FACEID_RECTANGLE_X2 + 2, FACEID_RECTANGLE_Y2 + 2, BLACK, lcd_data.buffer);
        fonts_drawRectangle(LCD_WIDTH, LCD_HEIGHT, FACEID_RECTANGLE_X1 - 3, FACEID_RECTANGLE_Y1 - 3,
                FACEID_RECTANGLE_X2 + 3, FACEID_RECTANGLE_Y2 + 3, BLACK, lcd_data.buffer);
    }

    if ((timestamps.screen_drew - timestamps.audio_result_received) < LCD_CLASSIFICATION_DURATION) {
        if (device_settings.enable_lcd_probabilty) {
            snprintf(lcd_data.toptitle, sizeof(lcd_data.toptitle) - 1, "%s %0.1f",
                    device_status.classification_audio.result, (double) device_status.classification_audio.probabily);
        } else {
            strncpy(lcd_data.toptitle, device_status.classification_audio.result, sizeof(lcd_data.toptitle) - 1);
        }

        fonts_putToptitle(LCD_WIDTH, LCD_HEIGHT, lcd_data.toptitle, Font_16x26, lcd_data.toptitle_color, lcd_data.buffer);
    }

    if ((timestamps.screen_drew - timestamps.notification_received) < LCD_NOTIFICATION_DURATION) {
        fonts_putSubtitle(LCD_WIDTH, LCD_HEIGHT, lcd_data.notification, Font_7x10, lcd_data.notification_color, lcd_data.buffer);
    }

    device_status.statistics.lcd_fps = (float) 1000.0 / (float)(timer_ms_tick - timestamps.screen_drew);
    timestamps.screen_drew = timer_ms_tick;
    lcd_drawImage(0, 0, LCD_WIDTH, LCD_HEIGHT, lcd_data.buffer);
}

void ms_timer(void)
{
    // Clear interrupt
    MXC_TMR_ClearFlags(MAX32666_TIMER_MS);

    timer_ms_tick += 1;
}

static int ms_timer_init(void)
{
    // Init ms timer
    mxc_tmr_cfg_t tmr;
    MXC_TMR_Shutdown(MAX32666_TIMER_MS);
    tmr.pres = TMR_PRES_1;
    tmr.mode = TMR_MODE_CONTINUOUS;
    tmr.cmp_cnt = PeripheralClock / 1000;
    tmr.pol = 0;
    NVIC_SetVector(MXC_TMR_GET_IRQ(MXC_TMR_GET_IDX(MAX32666_TIMER_MS)), ms_timer);
    NVIC_EnableIRQ(MXC_TMR_GET_IRQ(MXC_TMR_GET_IDX(MAX32666_TIMER_MS)));
    MXC_TMR_Init(MAX32666_TIMER_MS, &tmr);
    MXC_TMR_Start(MAX32666_TIMER_MS);

    return E_NO_ERROR;
}

// Similar to Core 0, the entry point for Core 1
// is Core1Main()
// Execution begins when the CPU1 Clock is enabled
int Core1_Main(void)
{
    //  __asm__("BKPT");

    int ret = 0;

    PR_INFO("maxrefdes178_max32666 core1");

    ret = ble_init();
    if (ret != E_NO_ERROR) {
        PR_ERROR("ble_init %d", ret);
    }

    core1_irq_init();

    core1_init_done = 1;

    PR_INFO("core 1 init completed");

    while (1) {
        ble_worker();
    }

    return E_NO_ERROR;
}

static void core0_irq_init(void)
{
    // Disable all interrupts used by core1
    NVIC_DisableIRQ(BTLE_TX_DONE_IRQn);
    NVIC_DisableIRQ(BTLE_RX_RCVD_IRQn);
    NVIC_DisableIRQ(BTLE_RX_ENG_DET_IRQn);
    NVIC_DisableIRQ(BTLE_SFD_DET_IRQn);
    NVIC_DisableIRQ(BTLE_SFD_TO_IRQn);
    NVIC_DisableIRQ(BTLE_GP_EVENT_IRQn);
    NVIC_DisableIRQ(BTLE_CFO_IRQn);
    NVIC_DisableIRQ(BTLE_SIG_DET_IRQn);
    NVIC_DisableIRQ(BTLE_AGC_EVENT_IRQn); // Disabled
    NVIC_DisableIRQ(BTLE_RFFE_SPIM_IRQn);
    NVIC_DisableIRQ(BTLE_TX_AES_IRQn); // Disabled
    NVIC_DisableIRQ(BTLE_RX_AES_IRQn); // Disabled
    NVIC_DisableIRQ(BTLE_INV_APB_ADDR_IRQn); // Disabled
    NVIC_DisableIRQ(BTLE_IQ_DATA_VALID_IRQn); // Disabled

    NVIC_DisableIRQ(MXC_TMR_GET_IRQ(MXC_TMR_GET_IDX(MAX32666_TIMER_BLE)));
    NVIC_DisableIRQ(MXC_TMR_GET_IRQ(MXC_TMR_GET_IDX(MAX32666_TIMER_BLE_SLEEP)));

    NVIC_DisableIRQ(WUT_IRQn);
}

static void core1_irq_init(void)
{

    NVIC_DisableIRQ(GPIO0_IRQn);
    NVIC_DisableIRQ(GPIO1_IRQn);

    NVIC_DisableIRQ(MXC_TMR_GET_IRQ(MXC_TMR_GET_IDX(MAX32666_TIMER_LED)));
    NVIC_DisableIRQ(MXC_TMR_GET_IRQ(MXC_TMR_GET_IDX(MAX32666_TIMER_MS)));
}
