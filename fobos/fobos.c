//==============================================================================
//       _____     __           _______
//      /  __  \  /_/          /  ____/                                __
//     /  /_ / / _   ____     / /__  __  __   ____    ____    ____   _/ /_
//    /    __ / / / /  _  \  / ___/  \ \/ /  / __ \  / __ \  / ___\ /  _/
//   /  /\ \   / / /  /_/ / / /___   /   /  / /_/ / /  ___/ / /     / /_
//  /_ /  \_\ /_/  \__   / /______/ /_/\_\ / ____/  \____/ /_/      \___/
//               /______/                 /_/             
//  Fobos SDR API library
//  Copyright (C) Rig Expert Ukraine Ltd.
//  2024.03.21
//  2024.04.08
//==============================================================================
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "fobos.h"
#ifdef _WIN32
#include <libusb-1.0/libusb.h>
#include <conio.h>
#include <Windows.h>
#pragma comment(lib, "libusb-1.0.lib")                                             
#define printf_internal _cprintf
#else
#include <libusb-1.0/libusb.h>
#include <unistd.h>
#endif
#ifndef printf_internal
#define printf_internal printf
#endif // !printf_internal
//==============================================================================
//#define FOBOS_PRINT_DEBUG
//==============================================================================
#define FOBOS_HW_REVISION "2.0.1"
#define FOBOS_FV_VERSION "1.1.0"
#define LIB_VERSION "2.1.1"
#define DRV_VERSION "libusb"
//==============================================================================
#define FOBOS_VENDOR_ID 0x16d0
#define FOBOS_PRODUCT_ID 0x132e
//==============================================================================
#define FOBOS_DEV_PRESEL_V1 0
#define FOBOS_DEV_PRESEL_V2 1
#define FOBOS_DEV_LNA_LP_SHD 2
#define FOBOS_DEV_LNA_HP_SHD 3
#define FOBOS_DEV_IF_V1 4
#define FOBOS_DEV_IF_V2 5
#define FOBOS_DEV_LPF_A0 6
#define FOBOS_DEV_LPF_A1 7
#define FOBOS_DEV_NENBL_HF 8
#define FOBOS_DEV_CLKSEL 9
#define FOBOS_DEV_ADC_NCS 10
#define FOBOS_DEV_ADC_SCK 11
#define FOBOS_DEV_ADC_SDI 12
#define FOBOS_MAX2830_ANTSEL 13
//==============================================================================
#define bitset(x,nbit)   ((x) |=  (1<<(nbit)))
#define bitclear(x,nbit) ((x) &= ~(1<<(nbit)))
#define FOBOS_DEF_BUF_COUNT 16
#define FOBOS_MAX_BUF_COUNT 64
#define FOBOS_DEF_BUF_LENGTH    (16 * 32 * 512)
#define LIBUSB_BULK_TIMEOUT 0
#define LIBUSB_BULK_IN_ENDPOINT 0x81
#define LIBUSB_DDESCRIPTOR_LEN 64
#ifndef LIBUSB_CALL
#define LIBUSB_CALL
#endif
//==============================================================================
static const float SAMPLE_NORM = (1.0 / (float)(SHRT_MAX >> 2)); // 14 bit ADC
//==============================================================================
enum fobos_async_status
{
    FOBOS_IDDLE = 0,
    FOBOS_STARTING,
    FOBOS_RUNNING,
    FOBOS_CANCELING
};
//==============================================================================
struct fobos_dev_t
{
    //=== libusb ===============================================================
    libusb_context *libusb_ctx;
    struct libusb_device_handle *libusb_devh;
    uint32_t transfer_buf_count;
    uint32_t transfer_buf_size;
    struct libusb_transfer **transfer;
    unsigned char **transfer_buf;
    int transfer_errors;
    int dev_lost;
    int use_zerocopy;
    //=== common ===============================================================
    uint16_t user_gpo;
    uint16_t dev_gpo;
    char manufacturer[LIBUSB_DDESCRIPTOR_LEN];
    char product[LIBUSB_DDESCRIPTOR_LEN];
    char serial[LIBUSB_DDESCRIPTOR_LEN];
    //=== rx stuff =============================================================
    double rx_frequency;
    uint32_t rx_frequency_band;
    double rx_samplerate;
    double rx_bandwidth;
    uint8_t rx_lpf_idx;
    uint8_t rx_lna_gain;
    uint8_t rx_vga_gain;
    uint8_t rx_bw_idx;
    uint8_t rx_direct_sampling;
    fobos_rx_cb_t rx_cb;
    void *rx_cb_ctx;
    enum fobos_async_status rx_async_status;
    int rx_async_cancel;
    uint32_t rx_failures;
    uint32_t rx_buff_counter;
    int rx_swap_iq;
    int rx_calibration_state;
    int rx_calibration_pos;
    float rx_dc_re;
    float rx_dc_im;
    float rx_scale_re;
    float rx_scale_im;
    float * rx_buff;
    uint16_t rffc507x_registers_local[31];
    uint16_t rffc500x_registers_remote[31];
};
//==============================================================================
inline int16_t to_int16(int16_t value)
{
    int16_t result = value & (int16_t)0x3FFF;
    result ^= (int16_t)(1<<13);
    result <<= (int16_t)2;
    return result >> (int16_t)2;
}
//==============================================================================
char * to_bin(uint16_t s16, char * str)
{
    for (uint16_t i = 0; i < 16; i++)
    {
        *str = ((s16 & 0x8000) >> 15) + '0';
        str++;
        s16 <<= 1;
    }
    *str = 0;
    return str;
}
//==============================================================================
void print_buff(void *buff, int size)
{
    int16_t * b16 = (int16_t *)buff;
    int count = size / 4;
    char bin_re[17];
    char bin_im[17];
    for (int i = 0; i < count; i++)
    {
        int16_t re16 = b16[i * 2 + 0] & 0xFFFF;
        int16_t im16 = b16[i * 2 + 1] & 0xFFFF;
        to_bin(re16, bin_re);
        to_bin(im16, bin_im);
        printf_internal("%s % 6d  %s % 6d \r\n", bin_re, re16, bin_im, im16);
    }
}
//==============================================================================
void fobos_rffc507x_register_modify(uint16_t * p_data, uint8_t bit_to, uint8_t bit_from, uint16_t value)
{
    uint16_t mask = (~(~0u << (bit_to - bit_from + 1))) << (bit_from);
    *p_data = (* p_data & (~mask)) | ((value << bit_from) & mask);
}
//==============================================================================
int fobos_rx_get_api_info(char * lib_version, char * drv_version)
{
    if (lib_version)
    {
        strcpy(lib_version, LIB_VERSION);
    }
    if (drv_version)
    {
        strcpy(drv_version, DRV_VERSION);
    }
    return 0;
}
//==============================================================================
int fobos_rx_get_device_count(void)
{
    int i;
    int result;
    libusb_context *ctx;
    libusb_device **list;
    uint32_t device_count = 0;
    struct libusb_device_descriptor dd;
    ssize_t cnt;
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s();\n", __FUNCTION__);
#endif // FOBOS_PRINT_DEBUG
    result = libusb_init(&ctx);
    if (result < 0)
    {
        return 0;
    }
    cnt = libusb_get_device_list(ctx, &list);
    for (i = 0; i < cnt; i++)
    {
        libusb_get_device_descriptor(list[i], &dd);
#ifdef FOBOS_PRINT_DEBUG
        printf_internal("%04x:%04x\n", dd.idVendor, dd.idProduct);
#endif // FOBOS_PRINT_DEBUG        
        if ((dd.idVendor == FOBOS_VENDOR_ID) && (dd.idProduct == FOBOS_PRODUCT_ID))
        {
            device_count++;
        }
    }
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return device_count;
}
//==============================================================================
int fobos_rx_list_devices(char * serials)
{
    int i;
    int result;
    libusb_context *ctx;
    libusb_device **list;
    uint32_t device_count = 0;
    struct libusb_device_descriptor dd;
    ssize_t cnt;
    libusb_device_handle *handle;
    char string[256];
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s();\n", __FUNCTION__);
#endif // FOBOS_PRINT_DEBUG
    memset(string, 0, sizeof(string));
    result = libusb_init(&ctx);
    if (result < 0)
    {
        return 0;
    }
    cnt = libusb_get_device_list(ctx, &list);
    for (i = 0; i < cnt; i++)
    {
        libusb_get_device_descriptor(list[i], &dd);

        if ((dd.idVendor == FOBOS_VENDOR_ID) && (dd.idProduct == FOBOS_PRODUCT_ID))
        {
            if (serials)
            {
                handle = 0;
                result = libusb_open(list[i], &handle);
                if ((result == 0) && (handle))
                {
                    result = libusb_get_string_descriptor_ascii(handle, dd.iSerialNumber, (unsigned char*)string, sizeof(string));
                    if (result > 0)
                    {
                        serials = strcat(serials, string);
                    }
                    libusb_close(handle);
                }
                else
                {
                    serials = strcat(serials, "XXXXXXXXXXXX");
                }
                serials = strcat(serials, " ");
            }
            device_count++;
        }
    }
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return device_count;
}
//==============================================================================
int fobos_check(struct fobos_dev_t * dev)
{
    if (dev != NULL)
    {
        if ((dev->libusb_ctx != NULL) && (dev->libusb_devh != NULL))
        {
            return 0;
        }
        return -2;
    }
    return -1;
}
//==============================================================================
#define CTRLI       (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
#define CTRLO       (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT)
#define CTRL_TIMEOUT    300
void fobos_spi(struct fobos_dev_t * dev, uint8_t* tx, uint8_t* rx, uint16_t size)
{
    int result = fobos_check(dev);
    uint16_t xsize = 0;
    if (result == 0)
    {
        xsize += libusb_control_transfer(dev->libusb_devh, CTRLO, 0xE2, 1, 0, tx, size, CTRL_TIMEOUT);
        xsize += libusb_control_transfer(dev->libusb_devh, CTRLI, 0xE2, 1, 0, rx, size, CTRL_TIMEOUT);
        if (xsize != size * 2)
        {
            result = -6;
        }
    }
    if (result != 0)
    {
        printf_internal("fobos_spi() err %d\n", result);
    }
}
//==============================================================================
void fobos_i2c_transfer(struct fobos_dev_t * dev, uint8_t address, uint8_t* tx_data, uint16_t tx_size, uint8_t* rx_data, uint16_t rx_size)
{
    uint8_t req_code = 0xE7;
    int result = fobos_check(dev);
    uint16_t xsize;
    if (result == 0)
    {
        if ((tx_data != 0) && tx_size > 0)
        {
            xsize = libusb_control_transfer(dev->libusb_devh, CTRLO, req_code, address, 0, tx_data, tx_size, CTRL_TIMEOUT);
            if (xsize != tx_size)
            {
                result = -6;
            }
        }
        if ((rx_data != 0) && rx_size > 0)
        {
            xsize = libusb_control_transfer(dev->libusb_devh, CTRLI, req_code, address, 0, rx_data, rx_size, CTRL_TIMEOUT);
            if (xsize != tx_size)
            {
                result = -6;
            }
        }
    }
    if (result != 0)
    {
        printf_internal("fobos_i2c_transfer() err %d\n", result);
    }
}
//==============================================================================
void fobos_i2c_write(struct fobos_dev_t * dev, uint8_t address, uint8_t* data, uint16_t size)
{
    uint8_t req_code = 0xE7;
    int result = fobos_check(dev);
    uint16_t xsize;
    if (result == 0)
    {
        if ((data != 0) && (size > 0))
        {
            xsize = libusb_control_transfer(dev->libusb_devh, CTRLO, req_code, address, 0, data, size, CTRL_TIMEOUT);
            if (xsize != size)
            {
                result = -6;
            }
        }
    }
    if (result != 0)
    {
        printf_internal("fobos_i2c_write() err %d\n", result);
    }
}
//==============================================================================
void fobos_i2c_read(struct fobos_dev_t * dev, uint8_t address, uint8_t* data, uint16_t size)
{
    uint8_t req_code = 0xE7;
    int result = fobos_check(dev);
    uint16_t xsize;
    if (result == 0)
    {
        if ((data != 0) && (size > 0))
        {
            xsize = libusb_control_transfer(dev->libusb_devh, CTRLI, req_code, address, 0, data, size, CTRL_TIMEOUT);
            if (xsize != size)
            {
                result = -6;
            }
        }
    }
    if (result != 0)
    {
        printf_internal("fobos_i2c_read() err %d\n", result);
    }
}
//==============================================================================
void fobos_max2830_write_reg(struct fobos_dev_t * dev, uint8_t addr, uint16_t data)
{
    uint8_t req_code = 0xE5;
    int result = fobos_check(dev);
    uint8_t tx[3];
    uint16_t xsize;
    tx[0] = addr;
    tx[1] = data & 0xFF;
    tx[2] = (data >> 8) & 0xFF;
    if (result == 0)
    {
        xsize = libusb_control_transfer(dev->libusb_devh, CTRLO, req_code, 1, 0, tx, 3, CTRL_TIMEOUT);
        if (xsize != 3)
        {
            result = -6;
        }
    }
    if (result != 0)
    {
        printf_internal("fobos_max2830_write_reg() err %d\n", result);
    }
}
//==============================================================================
int fobos_max2830_init(struct fobos_dev_t * dev)
{
    fobos_max2830_write_reg(dev, 0, 0x1740);
    fobos_max2830_write_reg(dev, 1, 0x119A);
    fobos_max2830_write_reg(dev, 2, 0x1003);
    fobos_max2830_write_reg(dev, 3, 0x0079);
    fobos_max2830_write_reg(dev, 4, 0x3666);
    fobos_max2830_write_reg(dev, 5, 0x00A0); // Reference Frequency Divider = 1
    fobos_max2830_write_reg(dev, 6, 0x0060);
    fobos_max2830_write_reg(dev, 7, 0x0022);
    fobos_max2830_write_reg(dev, 8, 0x3420);
    fobos_max2830_write_reg(dev, 9, 0x03B5);
    fobos_max2830_write_reg(dev, 10, 0x1DA4);
    fobos_max2830_write_reg(dev, 11, 0x0000);
    fobos_max2830_write_reg(dev, 12, 0x0140);
    fobos_max2830_write_reg(dev, 13, 0x0E92);
    fobos_max2830_write_reg(dev, 14, 0x033B);
    fobos_max2830_write_reg(dev, 15, 0x0145);
    return 0;
}
//==============================================================================
int fobos_max2830_set_frequency(struct fobos_dev_t * dev, double value, double * actual)
{
    double fcomp = 25000000.0;
    double div = value / fcomp;
    uint32_t div_int = (uint32_t)(div) & 0x000000FF;
    uint32_t div_frac = (uint32_t)((div - div_int) * 1048575.0 + 0.5);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%f);\n", __FUNCTION__, value);
#endif // FOBOS_PRINT_DEBUG
    fobos_max2830_write_reg(dev, 5, 0x00A0); // Reference Frequency Divider = 1
    if (actual)
    {
        div = (double)(div_int) + (double)(div_frac) / 1048575.0;
        *actual = div * fcomp;
    }
    fobos_max2830_write_reg(dev, 3, ((div_frac << 8) | div_int) & 0x3FFF);
    fobos_max2830_write_reg(dev, 4, (div_frac >> 6) & 0x3FFF);
    return 0;
}
//==============================================================================
int fobos_rffc507x_write_reg(struct fobos_dev_t * dev, uint8_t addr, uint16_t data)
{
    uint8_t req_code = 0xE6;
    int result = fobos_check(dev);
    uint8_t tx[3];
    uint16_t xsize;
    tx[0] = addr;
    tx[1] = data & 0xFF;
    tx[2] = (data >> 8) & 0xFF;
    if (result == 0)
    {
        xsize = libusb_control_transfer(dev->libusb_devh, CTRLO, req_code, 1, 0, tx, 3, CTRL_TIMEOUT);
        if (xsize != 3)
        {
            result = -6;
        }
    }
    return result;
}
//==============================================================================
int fobos_rffc507x_read_reg(struct fobos_dev_t * dev, uint8_t addr, uint16_t * data)
{
    uint8_t req_code = 0xE6;
    int result = fobos_check(dev);
    uint8_t rx[2];
    uint16_t xsize;
    if ((result == 0) && data)
    {
        xsize = libusb_control_transfer(dev->libusb_devh, CTRLI, req_code, addr, 0, rx, 2, CTRL_TIMEOUT);
        *data = rx[0] | (rx[1] << 8);
        if (xsize != 2)
        {
            result = -6;
        }
    }
    return result;
}
//==============================================================================
#define RFFC507X_REGS_COUNT 31
static const uint16_t rffc507x_regs_default[RFFC507X_REGS_COUNT] =
{
    0xbefa,   /* 0x00   1011 1110 1111 1010*/
    0x4064,   /* 0x01 */
    0x9055,   /* 0x02 */
    0x2d02,   /* 0x03 */
    0xacbf,   /* 0x04 */
    0xacbf,   /* 0x05 */
    0x0028,   /* 0x06 */
    0x0028,   /* 0x07 */
    0xff00,   /* 0x08   1111 1111 0000 0000 */
    0x8220,   /* 0x09   1000 0010 0010 0000 */
    0x0202,   /* 0x0A */
    0x4800,   /* 0x0B   0100 1000 0000 0000*/
    0x1a94,   /* 0x0C */
    0xd89d,   /* 0x0D */
    0x8900,   /* 0x0E */
    0x1e84,   /* 0x0F */
    0x89d8,   /* 0x10 */
    0x9d00,   /* 0x11 */
    0x2a20,   /* 0x12 */
    0x0000,   /* 0x13 */
    0x0000,   /* 0x14 */
    0x0000,   /* 0x15 */
    0x0001,   /* 0x16 */
    0x4900,   /* 0x17 */
    0x0281,   /* 0x18 */
    0xf00f,   /* 0x19 */
    0x0000,   /* 0x1A */
    0x0000,   /* 0x1B */
    0xc840,   /* 0x1C */
    0x1000,   /* 0x1D */
    0x0005,   /* 0x1E */
};
//==============================================================================
int fobos_rffc507x_commit(struct fobos_dev_t * dev, int force)
{
    if (fobos_check(dev) == 0)
    {
        for (int i = 0; i < RFFC507X_REGS_COUNT; i++)
        {
            uint16_t local = dev->rffc507x_registers_local[i];
            if ((dev->rffc500x_registers_remote[i] != local) || force)
            {
                fobos_rffc507x_write_reg(dev, i, local);
            }
            dev->rffc500x_registers_remote[i] = local;
        }
        return 0;
    }
    return -1;
}
//==============================================================================
int fobos_rffc507x_init(struct fobos_dev_t * dev)
{
    int i;
    if (fobos_check(dev) == 0)
    {
        for (i = 0; i < RFFC507X_REGS_COUNT; i++)
        {
            fobos_rffc507x_write_reg(dev, i, rffc507x_regs_default[i]);
            dev->rffc507x_registers_local[i] = rffc507x_regs_default[i];
            dev->rffc500x_registers_remote[i] = rffc507x_regs_default[i];
        }
#ifdef FOBOS_PRINT_DEBUG
        uint16_t data = 0;
        for (i = 0; i < RFFC507X_REGS_COUNT; i++)
        {
            fobos_rffc507x_read_reg(dev, i, &data);
            printf_internal("0x%04x\n", data);
        }
#endif // FOBOS_PRINT_DEBUG
        // ENBL and MODE pins are ignored and become available as GPO5 and GPO6
        fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x15], 15, 15, 1);
        // 0 = half duplex
        fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x0B], 15, 15, 0);
        int MIX1_IDD = 1;
        int MIX2_IDD = 1;
        int mix = (MIX1_IDD << 3) | MIX2_IDD;
        fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x0B], 14, 9, mix);

        // MODE pin = 1, Active PLL Register Bank = 2, Active Mixer = 2;
        fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x15], 13, 13, 1);

        fobos_rffc507x_commit(dev, 0);
        return 0;
    }
    return -1;
}
//==============================================================================
#define FOBOS_RFFC507X_LO_MAX 5400
#define FOBOS_RFFC507X_REF_FREQ 25
int fobos_rffc507x_set_lo_frequency(struct fobos_dev_t * dev, int lo_freq_mhz, uint64_t * tune_freq_hz)
{
    uint32_t lodiv;
    uint16_t fvco;
    uint32_t fbkdiv;
    uint16_t pllcpl;
    uint16_t n;
    uint16_t p1nmsb;
    uint8_t p1nlsb;

    uint8_t n_lo = 0;
    uint16_t x = FOBOS_RFFC507X_LO_MAX / lo_freq_mhz;
    while ((x > 1) && (n_lo < 5))
    {
        n_lo++;
        x >>= 1;
    }

    lodiv = 1 << n_lo;
    fvco = lodiv * lo_freq_mhz;

    if (fvco > 3200)
    {
        fbkdiv = 4;
        pllcpl = 3;
    }
    else
    {
        fbkdiv = 2;
        pllcpl = 2;
    }

    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x15], 14, 14, 0); // enbl = 0
    fobos_rffc507x_commit(dev, 0);

    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x00], 2, 0, pllcpl);

    uint64_t tmp_n = ((uint64_t)fvco << 29ULL) / ((uint64_t)fbkdiv * FOBOS_RFFC507X_REF_FREQ);
    n = tmp_n >> 29ULL;

    p1nmsb = (tmp_n >> 13ULL) & 0xffff;
    p1nlsb = (tmp_n >> 5ULL) & 0xff;
    uint64_t freq_hz= (FOBOS_RFFC507X_REF_FREQ * (tmp_n >> 5ULL) * (uint64_t)fbkdiv * 1000000) / ((uint64_t)lodiv * (1 << 24ULL));
    if (tune_freq_hz)
    {
        *tune_freq_hz = freq_hz;
    }
    // Path 1
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x0C], 6,  4, n_lo);        // p1lodiv
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x0C], 15, 7, n);           // p1n
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x0C], 3,  2, fbkdiv >> 1); // p1presc
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x0D], 15, 0, p1nmsb);      // p1nmsb
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x0E], 15, 8, p1nlsb);      // p1nlsb
    // Path 2
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x0F], 6, 4, n_lo);         // p2lodiv
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x0F], 15, 7, n);           // p1n
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x0F], 3, 2, fbkdiv >> 1);  // p1presc
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x10], 15, 0, p1nmsb);      // p1nmsb
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x11], 15, 8, p1nlsb);      // p1nlsb

    fobos_rffc507x_commit(dev, 0);

    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x15], 14, 14, 1); // enbl = 1
    fobos_rffc507x_commit(dev, 0);
#ifdef FOBOS_PRINT_DEBUG
    double ff = (double)freq_hz;
    printf_internal("rffc507x lo_freq_mhz = %d %f\n", lo_freq_mhz, ff);
#endif // FOBOS_PRINT_DEBUG
    return 0;
}
//==============================================================================
#define SI5351C_ADDRESS 0x60
void fobos_si5351c_write_reg(struct fobos_dev_t * dev, uint8_t reg, uint8_t val)
{
    uint8_t data[] = {reg, val};
    fobos_i2c_write(dev, SI5351C_ADDRESS, data, sizeof(data));
}
//==============================================================================
uint8_t fobos_si5351c_read_reg(struct fobos_dev_t * dev, uint8_t reg)
{
    uint8_t data = 0x00;
    fobos_i2c_write(dev, SI5351C_ADDRESS, &reg, 1);
    fobos_i2c_read(dev, SI5351C_ADDRESS, &data, 1);
    return data;
}
//==============================================================================
void fobos_si5351c_write(struct fobos_dev_t * dev, uint8_t* data, uint16_t size)
{
    fobos_i2c_write(dev, SI5351C_ADDRESS, data, size);
}
//==============================================================================
void fobos_si5351c_read(struct fobos_dev_t * dev, uint8_t* data, uint16_t size)
{
    fobos_i2c_read(dev, SI5351C_ADDRESS, data, size);
}
//==============================================================================
void fobos_si5351c_config_pll(struct fobos_dev_t * dev, uint8_t ms_number, uint32_t p1, uint32_t p2, uint32_t p3)
{
    ms_number &= 0x03;
    uint8_t addr = 26 + (ms_number * 8);
    uint8_t data[] =
    {
        addr,
        (p3 >> 8) & 0xFF,
        (p3 >> 0) & 0xFF,
        (p1 >> 16) & 0x3,
        (p1 >> 8) & 0xFF,
        (p1 >> 0) & 0xFF,
        (((p3 >> 16) & 0xF) << 4) | (((p2 >> 16) & 0xF) << 0),
        (p2 >> 8) & 0xFF,
        (p2 >> 0) & 0xFF
    };
    fobos_si5351c_write(dev, data, sizeof(data));
}
//==============================================================================
void fobos_si5351c_config_msynth(struct fobos_dev_t * dev, uint8_t ms_number, uint32_t p1, uint32_t p2, uint32_t p3, uint8_t r_div)
{
    uint8_t addr = 42 + (ms_number * 8);
    uint8_t data[] =
    {
        addr,
        (p3 >> 8) & 0xFF,
        (p3 >> 0) & 0xFF,
        (r_div << 4) | (0 << 2) | ((p1 >> 16) & 0x3),
        (p1 >> 8) & 0xFF,
        (p1 >> 0) & 0xFF,
        (((p3 >> 16) & 0xF) << 4) | (((p2 >> 16) & 0xF) << 0),
        (p2 >> 8) & 0xFF,
        (p2 >> 0) & 0xFF
    };
    fobos_si5351c_write(dev, data, sizeof(data));
}
//==============================================================================
uint8_t si5351c_compose_clk_ctrl(uint8_t pwr_down, uint8_t int_mode, uint8_t ms_src_pll, uint8_t invert, uint8_t clk_source, uint8_t drv_strength)
{
    uint8_t result = 0;
    result |= ((pwr_down & 1) << 7);
    result |= ((int_mode & 1) << 6);
    result |= ((ms_src_pll & 1) << 5);
    result |= ((invert & 1) << 4);
    result |= ((clk_source & 3) << 2);
    result |= (drv_strength & 3);
    return result;
}
//==============================================================================
int fobos_rffc507x_clock(struct fobos_dev_t * dev, int enabled)
{
    uint8_t pwr_down = (enabled == 0);
    uint8_t data = si5351c_compose_clk_ctrl(pwr_down, 1, 0, 0, 3, 1);
    fobos_si5351c_write_reg(dev, 16, data);
    return 0;
}
//==============================================================================
int fobos_max2830_clock(struct fobos_dev_t * dev, int enabled)
{
    uint8_t pwr_down = (enabled == 0);
    uint8_t data = si5351c_compose_clk_ctrl(pwr_down, 1, 0, 0, 3, 1);
    fobos_si5351c_write_reg(dev, 20, data);
    return 0;
}
//==============================================================================
int fobos_si5351c_init(struct fobos_dev_t * dev)
{
    fobos_si5351c_write_reg(dev, 3, 0xFF); // disable all outputs
    fobos_si5351c_write_reg(dev, 9, 0xFF); // disable oeb pin control
    fobos_si5351c_write_reg(dev, 3, 0x00); // enable all outputs
    fobos_si5351c_write_reg(dev, 15, 0x0C); // clock source = CLKIN
    fobos_si5351c_write_reg(dev, 187, 0xC0); // Fanout Enable

    fobos_si5351c_write_reg(dev, 177, 0xA0); // reset plls

    uint8_t clk_ctrl_data[9];
    clk_ctrl_data[0] = 16;
    clk_ctrl_data[1] = si5351c_compose_clk_ctrl(0, 1, 0, 0, 3, 1); // #0 rffc507x_clk  prw up, int mode, plla, not inv, msx->clkx, 4ma
    clk_ctrl_data[2] = si5351c_compose_clk_ctrl(1, 1, 0, 0, 2, 0); // #1              pwr down
    clk_ctrl_data[3] = si5351c_compose_clk_ctrl(0, 1, 0, 0, 3, 0); // #2 ADC+
    clk_ctrl_data[4] = si5351c_compose_clk_ctrl(1, 1, 0, 0, 3, 0); // #3 ADC-
    clk_ctrl_data[5] = si5351c_compose_clk_ctrl(0, 1, 0, 0, 3, 1); // #4  MAX2830_CLK
    clk_ctrl_data[6] = si5351c_compose_clk_ctrl(1, 1, 0, 0, 2, 0); // #5
    clk_ctrl_data[7] = si5351c_compose_clk_ctrl(1, 1, 0, 0, 2, 0); // #6
    clk_ctrl_data[8] = si5351c_compose_clk_ctrl(1, 1, 0, 0, 2, 0); // #7

    fobos_si5351c_write(dev, clk_ctrl_data, sizeof(clk_ctrl_data));

    fobos_si5351c_config_pll(dev, 0, 80 * 128 - 512, 0, 1);

    // Configure rffc507x_clk to 25 MHz
    fobos_si5351c_config_msynth(dev, 0, 32 * 128 - 512, 0, 1, 0);

    // Configure max2830_clk to 25 MHz
    fobos_si5351c_config_msynth(dev, 4, 32 * 128 - 512, 0, 1, 0);

#ifdef FOBOS_PRINT_DEBUG
    printf_internal("si5351c registers:\n");
    uint8_t addr = 0;
    uint8_t data[32];
    fobos_si5351c_write(dev, &addr, 1);
    fobos_si5351c_read(dev, data, 32);
    for (int i = 0; i < 32; ++i)
    {
        addr = i;
        printf_internal("[%d]=0x%02x\n", addr, data[i]);
    }
#endif // FOBOS_PRINT_DEBUG
    return 0;
}
//==============================================================================
int fobos_fx3_command(struct fobos_dev_t * dev, uint8_t code, uint16_t value, uint16_t index)
{
    int result = fobos_check(dev);
    if (result != 0)
    {
        return result;
    }
    result = libusb_control_transfer(dev->libusb_devh, CTRLO, code, value, index, 0, 0, CTRL_TIMEOUT);
    return result;
}
//==============================================================================
int fobos_rx_set_user_gpo(struct fobos_dev_t * dev, uint8_t value)
{
    return fobos_fx3_command(dev, 0xE3, value, 0);
}
//==============================================================================
int fobos_rx_set_dev_gpo(struct fobos_dev_t * dev, uint16_t value)
{
    return fobos_fx3_command(dev, 0xE4, value, 0);
}
//==============================================================================
int fobos_rx_open(struct fobos_dev_t ** out_dev, uint32_t index)
{
    int result = 0;
    int i = 0;
    struct fobos_dev_t * dev = NULL;
    libusb_device **dev_list;
    libusb_device *device = NULL;
    ssize_t cnt;
    uint32_t device_count = 0;
    struct libusb_device_descriptor dd;
    dev = (struct fobos_dev_t*)malloc(sizeof(struct fobos_dev_t));
    if (NULL == dev)
    {
        return -ENOMEM;
    }
    memset(dev, 0, sizeof(struct fobos_dev_t));
    result = libusb_init(&dev->libusb_ctx);
    if (result < 0)
    {
        free(dev);
        return -1;
    }
    cnt = libusb_get_device_list(dev->libusb_ctx, &dev_list);
    for (i = 0; i < cnt; i++)
    {
        libusb_get_device_descriptor(dev_list[i], &dd);

        if ((dd.idVendor == FOBOS_VENDOR_ID) && (dd.idProduct == FOBOS_PRODUCT_ID))
        {
            if (index == device_count)
            {
                device = dev_list[i];
                break;
            }
            device_count++;
        }
    }
    if (device)
    {
        result = libusb_open(device, &dev->libusb_devh);
        if (result == 0)
        {
            libusb_get_string_descriptor_ascii(dev->libusb_devh, dd.iSerialNumber, (unsigned char*)dev->serial, sizeof(dev->serial));
            libusb_get_string_descriptor_ascii(dev->libusb_devh, dd.iManufacturer, (unsigned char*)dev->manufacturer, sizeof(dev->manufacturer));
            libusb_get_string_descriptor_ascii(dev->libusb_devh, dd.iProduct, (unsigned char*)dev->product, sizeof(dev->product));
            result = libusb_claim_interface(dev->libusb_devh, 0);
            if (result == 0)
            {
                *out_dev = dev;
                //======================================================================
                dev->dev_gpo = 0;
                dev->rx_scale_re = SAMPLE_NORM;
                dev->rx_scale_im = SAMPLE_NORM;
                dev->rx_dc_re = 0.f;
                dev->rx_dc_im = 0.f;
                if (fobos_check(dev) == 0)
                {
                    bitset(dev->dev_gpo, FOBOS_DEV_CLKSEL);
                    bitset(dev->dev_gpo, FOBOS_DEV_LNA_LP_SHD);
                    bitset(dev->dev_gpo, FOBOS_DEV_LNA_HP_SHD);
                    bitset(dev->dev_gpo, FOBOS_DEV_ADC_NCS);
                    bitset(dev->dev_gpo, FOBOS_DEV_ADC_SCK);
                    bitset(dev->dev_gpo, FOBOS_DEV_ADC_SDI);
                    bitset(dev->dev_gpo, FOBOS_DEV_NENBL_HF);
                    fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
                    fobos_si5351c_init(dev);
                    fobos_max2830_init(dev);
                    fobos_rffc507x_init(dev);
                    fobos_rffc507x_set_lo_frequency(dev, 2375, 0);
                    fobos_max2830_set_frequency(dev, 2475000000.0, 0);
                    fobos_rx_set_samplerate(dev, 10000000.0, 0);
                    return 0;
                }
            }
            else
            {
                printf_internal("usb_claim_interface error %d\n", result);
            }
        }
        else
        {
            printf_internal("usb_open error %d\n", result);
#ifndef _WIN32
            if (result == LIBUSB_ERROR_ACCESS)
            {
                printf_internal("Please fix the device permissions by installing fobos-sdr.rules\n");
            }
#endif
        }
    }
    libusb_free_device_list(dev_list, 1);
    if (dev->libusb_devh)
    {
        libusb_close(dev->libusb_devh);
    }
    if (dev->libusb_ctx)
    {
        libusb_exit(dev->libusb_ctx);
    }
    free(dev);
    return -1;
}
//==============================================================================
int fobos_rx_close(struct fobos_dev_t * dev)
{
    int result = fobos_check(dev);
    if (result != 0)
    {
        return result;
    }
    fobos_rx_cancel_async(dev);
    while (FOBOS_IDDLE != dev->rx_async_status)
    {
#ifdef FOBOS_PRINT_DEBUG
        printf_internal("s");
#endif
#ifdef _WIN32
        Sleep(1);
#else
        sleep(1);
#endif
    }
    bitclear(dev->dev_gpo, FOBOS_DEV_LPF_A0);
    bitclear(dev->dev_gpo, FOBOS_DEV_LPF_A1);
    bitset(dev->dev_gpo, FOBOS_DEV_NENBL_HF);
    fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
    // disable rffc507x
    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x15], 14, 14, 0); // enbl = 0
    fobos_rffc507x_commit(dev, 0);
    // disable clocks
    fobos_rffc507x_clock(dev, 0);
    fobos_max2830_clock(dev, 0);
    libusb_close(dev->libusb_devh);
    libusb_exit(dev->libusb_ctx);
    free(dev);
    return 0;
}
//==============================================================================
int fobos_rx_get_board_info(struct fobos_dev_t * dev, char * hw_revision, char * fw_version, char * manufacturer, char * product, char * serial)
{
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s();\n", __FUNCTION__);
#endif // FOBOS_PRINT_DEBUG
    int result = fobos_check(dev);
    if (result != 0)
    {
        return result;
    }
    result = 0;
    if (hw_revision)
    {
        strcpy(hw_revision, FOBOS_HW_REVISION);
    }
    if (fw_version)
    {
        strcpy(fw_version, FOBOS_FV_VERSION);
    }
    if (manufacturer)
    {
        strcpy(manufacturer, dev->manufacturer);
    }
    if (product)
    {
        strcpy(product, dev->product);
    }
    if (serial)
    {
        strcpy(serial, dev->serial);
    }
    return result;
}
//==============================================================================
#define FOBOS_MIN_LP_FREQ_MHZ (40)
#define FOBOS_MAX_LP_FREQ_MHZ (2300)
#define FOBOS_MIN_BP_FREQ_MHZ (2300)
#define FOBOS_MAX_BP_FREQ_MHZ (2550)
#define FOBOS_MIN_HP_FREQ_MHZ (2550)
#define FOBOS_MAX_HP_FREQ_MHZ (6550)
//==============================================================================
int fobos_rx_set_frequency(struct fobos_dev_t * dev, double value, double * actual)
{
    int result = fobos_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%f);\n", __FUNCTION__, value);
#endif // FOBOS_PRINT_DEBUG    
    if (result != 0)
    {
        return result;
    }
    result = 0;
    if (dev->rx_frequency != value)
    {
        double rx_frequency = 0.0;

        uint32_t RFFC5071_freq_mhz;
        uint64_t RFFC5071_freq_hz_actual;

        uint64_t freq = (uint64_t)value;

        uint32_t freq_mhz = freq / 1000000;
        double max2830_freq = 0.0;
        double max2830_freq_actual = 0.0;

        if (freq_mhz < FOBOS_MAX_LP_FREQ_MHZ)
        {
            if (dev->rx_frequency_band != 1)
            {
                dev->rx_frequency_band = 1;
                // set_preselect(lowpass);
                bitset(dev->dev_gpo, FOBOS_DEV_PRESEL_V1);
                bitclear(dev->dev_gpo, FOBOS_DEV_PRESEL_V2);
                // enable lowpass lna
                bitclear(dev->dev_gpo, FOBOS_DEV_LNA_LP_SHD);
                // shut down highpass lna
                bitset(dev->dev_gpo, FOBOS_DEV_LNA_HP_SHD);
                // set_if_filter(high);
                bitclear(dev->dev_gpo, FOBOS_DEV_IF_V1);
                bitset(dev->dev_gpo, FOBOS_DEV_IF_V2);
                // turn on max2830 ant1 (main) input, turn off ant2 (aux) input
                bitclear(dev->dev_gpo, FOBOS_MAX2830_ANTSEL);
                // commit dev_gpo value
                fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
                // enable rffc507x clock
                fobos_rffc507x_clock(dev, 1);
            }
            int upcon = 0;
            if (upcon)
            {
                dev->rx_swap_iq = 1;
                // set frequencies
                uint32_t max2830_mhz = 2450;
                RFFC5071_freq_mhz = max2830_mhz + freq_mhz;
                RFFC5071_freq_mhz = (RFFC5071_freq_mhz / 5) * 5; // spures prevention
                fobos_rffc507x_set_lo_frequency(dev, RFFC5071_freq_mhz, &RFFC5071_freq_hz_actual);
                max2830_freq = (double)(RFFC5071_freq_hz_actual - freq);
                fobos_max2830_set_frequency(dev, max2830_freq, &max2830_freq_actual);
                rx_frequency = RFFC5071_freq_hz_actual - max2830_freq_actual;
            }
            else
            {
                dev->rx_swap_iq = 0;
                // set frequencies
                uint32_t max2830_mhz = 2400;
                RFFC5071_freq_mhz = max2830_mhz - freq_mhz;
                RFFC5071_freq_mhz = (RFFC5071_freq_mhz / 5) * 5; // spures prevention
                fobos_rffc507x_set_lo_frequency(dev, RFFC5071_freq_mhz, &RFFC5071_freq_hz_actual);
                max2830_freq = (double)(RFFC5071_freq_hz_actual + freq);
                fobos_max2830_set_frequency(dev, max2830_freq, &max2830_freq_actual);
                rx_frequency = max2830_freq_actual - RFFC5071_freq_hz_actual;
            }
            result = 0;
        }
        else if ((freq_mhz >= FOBOS_MIN_BP_FREQ_MHZ) && (freq_mhz <= FOBOS_MAX_BP_FREQ_MHZ))
        {
            if (dev->rx_frequency_band != 2)
            {
                dev->rx_frequency_band = 2;
                // set_preselect(bypass);
                bitclear(dev->dev_gpo, FOBOS_DEV_PRESEL_V1);
                bitclear(dev->dev_gpo, FOBOS_DEV_PRESEL_V2);
                // shut down both lnas
                bitclear(dev->dev_gpo, FOBOS_DEV_LNA_LP_SHD);
                bitclear(dev->dev_gpo, FOBOS_DEV_LNA_HP_SHD);
                // set_invert_iq(false);
                dev->rx_swap_iq = 0;
                // set_if_filter(none);
                bitclear(dev->dev_gpo, FOBOS_DEV_IF_V1);
                bitclear(dev->dev_gpo, FOBOS_DEV_IF_V2);
                // turn on max2830 ant2 (diversity) input
                bitset(dev->dev_gpo, FOBOS_MAX2830_ANTSEL);
                // commit dev_gpo value
                fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
                // disable rffc507x
                fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[21], 14, 14, 0); // enbl = 0
                fobos_rffc507x_commit(dev, 0);
                // disable rffc507x clock
                fobos_rffc507x_clock(dev, 0);
            }
            // set frequency direct to max2830
            max2830_freq = value;
            fobos_max2830_set_frequency(dev, max2830_freq, &max2830_freq_actual);
            rx_frequency = max2830_freq_actual;
            result = 0;
        }
        else if ((freq_mhz >= FOBOS_MIN_HP_FREQ_MHZ) && (freq_mhz <= FOBOS_MAX_HP_FREQ_MHZ))
        {
            // set_preselect(hipass);
            bitclear(dev->dev_gpo, FOBOS_DEV_PRESEL_V1);
            bitset(dev->dev_gpo, FOBOS_DEV_PRESEL_V2);
            // set_invert_iq(true);
            dev->rx_swap_iq = 1;
            uint32_t max2830_mhz = 2350;
            if ((freq_mhz >= 4550) && (freq_mhz <= 4750))
            {
                // set_if_filter(high);
                bitclear(dev->dev_gpo, FOBOS_DEV_IF_V1);
                bitset(dev->dev_gpo, FOBOS_DEV_IF_V2);
                max2830_mhz = 2450;
            }
            else
            {
                // set_if_filter(low);
                bitset(dev->dev_gpo, FOBOS_DEV_IF_V1);
                bitclear(dev->dev_gpo, FOBOS_DEV_IF_V2);
                max2830_mhz = 2350;
            }
            // turn on max2830 ant1 (main) input
            bitclear(dev->dev_gpo, FOBOS_MAX2830_ANTSEL);
            // commit dev_gpo value
            fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
            if (dev->rx_frequency_band != 3)
            {
                dev->rx_frequency_band = 3;

                // enable rffc507x clock
                fobos_rffc507x_clock(dev, 1);
            }
            RFFC5071_freq_mhz = freq_mhz - max2830_mhz;
            fobos_rffc507x_set_lo_frequency(dev, RFFC5071_freq_mhz, &RFFC5071_freq_hz_actual);
            max2830_freq = (double)(freq - RFFC5071_freq_hz_actual);
            fobos_max2830_set_frequency(dev, max2830_freq, &max2830_freq_actual);
            rx_frequency = max2830_freq_actual + RFFC5071_freq_hz_actual;
            result = 0;
        }
        else
        {
            result = -5;
        }
        if (result == 0)
        {
            dev->rx_frequency = rx_frequency;
            if (actual)
            {
                *actual = rx_frequency;
            }
        }
    }
    return result;
}
//==============================================================================
int fobos_rx_set_direct_sampling(struct fobos_dev_t * dev, unsigned int enabled)
{
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%d);\n", __FUNCTION__, enabled);
#endif // FOBOS_PRINT_DEBUG
    int result = fobos_check(dev);
    if (result != 0)
    {
        return result;
    }
    result = 0;
    if (dev->rx_direct_sampling != enabled)
    {
        if (enabled)
        {
            bitset(dev->dev_gpo, FOBOS_DEV_LPF_A0);
            bitset(dev->dev_gpo, FOBOS_DEV_LPF_A1);
            bitclear(dev->dev_gpo, FOBOS_DEV_NENBL_HF);
            fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
            // disable clocks
            fobos_rffc507x_clock(dev, 0);
            fobos_max2830_clock(dev, 0);
            // disable rffc507x
            fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x15], 14, 14, 0); // enbl = 0
            fobos_rffc507x_commit(dev, 0);
        }
        else
        {
            bitclear(dev->dev_gpo, FOBOS_DEV_LPF_A0);
            bitclear(dev->dev_gpo, FOBOS_DEV_LPF_A1);
            if (dev->rx_lpf_idx > 2)
            {
                dev->rx_lpf_idx = 2;
            }
            if (dev->rx_lpf_idx & 1)
            {
                bitset(dev->dev_gpo, FOBOS_DEV_LPF_A1);
            }
            if (dev->rx_lpf_idx & 2)
            {
                bitset(dev->dev_gpo, FOBOS_DEV_LPF_A0);
            }
            bitset(dev->dev_gpo, FOBOS_DEV_NENBL_HF);
            fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
            // enable clocks
            fobos_rffc507x_clock(dev, 1);
            fobos_max2830_clock(dev, 1);
            // enable rffc507x
            fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x15], 14, 14, 1); // enbl = 1
            fobos_rffc507x_commit(dev, 0);
        }
        dev->rx_direct_sampling = enabled;
    }
    return result;
}
//==============================================================================
int fobos_rx_set_lna_gain(struct fobos_dev_t * dev, unsigned int value)
{
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%d)\n", __FUNCTION__, value);
#endif // FOBOS_PRINT_DEBUG
    int result = fobos_check(dev);
    if (result != 0)
    {
        return result;
    }
    result = 0;
    if (value > 3) value = 3;
    if (value != dev->rx_lna_gain)
    {
        dev->rx_lna_gain = value;
        int lna_gain = dev->rx_lna_gain & 0x0003;
        int vga_gain = dev->rx_vga_gain & 0x001F;
        fobos_max2830_write_reg(dev, 11, (lna_gain << 5) | vga_gain);
    }
    return result;
}
//==============================================================================
int fobos_rx_set_vga_gain(struct fobos_dev_t * dev, unsigned int value)
{
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%d)\n", __FUNCTION__, value);
#endif // FOBOS_PRINT_DEBUG
    int result = fobos_check(dev);
    if (result != 0)
    {
        return result;
    }
    result = 0;
    if (value > 15) value = 15;
    if (value != dev->rx_vga_gain)
    {
        dev->rx_vga_gain = value;
        int lna_gain = dev->rx_lna_gain & 0x0003;
        int vga_gain = dev->rx_vga_gain & 0x001F;
        fobos_max2830_write_reg(dev, 11, (lna_gain << 5) | vga_gain);
    }
    return result;
}
//==============================================================================
const double fobos_sample_rates[] =
{
    80000000.0,
    50000000.0,
    40000000.0,
    32000000.0,
    25000000.0,
    20000000.0,
    16000000.0,
    12500000.0,
    10000000.0,
    8000000.0,
    6400000.0,
    6250000.0,
    5000000.0,
    4000000.0
};
//==============================================================================
int fobos_rx_get_samplerates(struct fobos_dev_t * dev, double * values, unsigned int * count)
{
    int result = fobos_check(dev);
    if (result != 0)
    {
        return result;
    }
    result = 0;
    if (count)
    {
        *count = sizeof(fobos_sample_rates) / sizeof(fobos_sample_rates[0]);
        if (values)
        {
            memcpy(values, fobos_sample_rates, sizeof(fobos_sample_rates));
        }
    }
    return result;
}
//==============================================================================
const uint32_t fobos_p1s[] =
{
    10, 16, 20, 25, 32, 40, 50, 64, 80, 100, 125, 128, 160, 200
};
//==============================================================================
int fobos_rx_set_samplerate(struct fobos_dev_t * dev, double value, double * actual)
{
    int result = fobos_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%f)\n", __FUNCTION__, value);
#endif // FOBOS_PRINT_DEBUG
    if (result != 0)
    {
        return result;
    }
    result = 0;
    uint32_t p1 = 0;
    size_t count = sizeof(fobos_sample_rates) / sizeof(fobos_sample_rates[0]);
    double df_min = fobos_sample_rates[0];
    size_t i_min = 0;
    for (size_t i = 0; i < count; i++)
    {
        double df = fabs(value - fobos_sample_rates[i]);
        if (df < df_min)
        {
            df_min = df;
            i_min = i;
        }
    }
    p1 = fobos_p1s[i_min] * 128 - 512;
    fobos_si5351c_config_msynth(dev, 2, p1, 0, 1, 0);
    fobos_si5351c_config_msynth(dev, 3, p1, 0, 1, 0);
    value = fobos_sample_rates[i_min];
    if (result == 0)
    {
        int rx_lpf_idx = dev->rx_lpf_idx;
        if (value < 13000000.0)
        {
            rx_lpf_idx = 0;
        }
        else if (value < 26000000.0)
        {
            rx_lpf_idx = 1;
        }
        else
        {
            rx_lpf_idx = 2;
        }
        if (!dev->rx_direct_sampling)
        {
            bitclear(dev->dev_gpo, FOBOS_DEV_LPF_A0);
            bitclear(dev->dev_gpo, FOBOS_DEV_LPF_A1);
            if (rx_lpf_idx & 1)
            {
                bitset(dev->dev_gpo, FOBOS_DEV_LPF_A1);
            }
            if (rx_lpf_idx & 2)
            {
                bitset(dev->dev_gpo, FOBOS_DEV_LPF_A0);
            }
            fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
        }
        dev->rx_lpf_idx = rx_lpf_idx;
        int rx_bw_idx = dev->rx_bw_idx;
        if (value < 15000000.0)
        {
            rx_bw_idx = 0;
        }
        else if (value < 17000000.0)
        {
            rx_bw_idx = 1;
        }
        else if (value < 30000000.0)
        {
            rx_bw_idx = 2;
        }
        else
        {
            rx_bw_idx = 3;
        }
        if (dev->rx_bw_idx != rx_bw_idx)
        {
            dev->rx_bw_idx = rx_bw_idx;
            fobos_max2830_write_reg(dev, 8, rx_bw_idx | 0x3420);
        }
#ifdef FOBOS_PRINT_DEBUG
        printf_internal("lpf_idx =  %i bw_idx = %i\n", rx_lpf_idx, rx_bw_idx);
#endif // FOBOS_PRINT_DEBUG
        dev->rx_samplerate = value;
        if (actual)
        {
            *actual = dev->rx_samplerate;
        }
    }
    return result;
}
//==============================================================================
int fobos_rx_set_lpf(struct fobos_dev_t * dev, int value)
{
    int result = fobos_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%d)\n", __FUNCTION__, value);
#endif // FOBOS_PRINT_DEBUG
    if (result != 0)
    {
        return result;
    }
    if (value < 0) value = 0;
    if (value > 2) value = 2;
    if (dev->rx_lpf_idx != value)
    {
        if (!dev->rx_direct_sampling)
        {
            bitclear(dev->dev_gpo, FOBOS_DEV_LPF_A0);
            bitclear(dev->dev_gpo, FOBOS_DEV_LPF_A1);
            if (value & 1)
            {
                bitset(dev->dev_gpo, FOBOS_DEV_LPF_A1);
            }
            if (value & 2)
            {
                bitset(dev->dev_gpo, FOBOS_DEV_LPF_A0);
            }
            fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
        }
        dev->rx_lpf_idx = value;
    }
    return result;
}
//==============================================================================
int fobos_rx_set_clk_source(struct fobos_dev_t * dev, int value)
{
    int result = fobos_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%d)\n", __FUNCTION__, value);
#endif // FOBOS_PRINT_DEBUG
    if (result != 0)
    {
        return result;
    }
    if (value)
    {
        bitclear(dev->dev_gpo, FOBOS_DEV_CLKSEL);
    }
    else
    {
        bitset(dev->dev_gpo, FOBOS_DEV_CLKSEL);
    }
    result = fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
    return result;
}
//==============================================================================
#define FOBOS_SWAP_IQ_HW 1
void fobos_rx_proceed_rx_buff(struct fobos_dev_t * dev, void * data, size_t size)
{
    size_t complex_samples_count = size / 4;
    int16_t * psample = (int16_t *)data;
    float sample = 0.0f;
    float scale_re = dev->rx_scale_re;
    float scale_im = dev->rx_scale_im;
    if (dev->rx_direct_sampling)
    {
        scale_re = SAMPLE_NORM;
        scale_im = SAMPLE_NORM;
    }
    float k = 0.001f;
    float dc_re = dev->rx_dc_re;
    float dc_im = dev->rx_dc_im;

    float * dst_re = dev->rx_buff;
    float * dst_im = dev->rx_buff + 1;
    int rx_swap_iq = dev->rx_swap_iq ^ FOBOS_SWAP_IQ_HW;
    if (rx_swap_iq)
    {
        dst_re = dev->rx_buff + 1;
        dst_im = dev->rx_buff;
    }
    size_t chunks_count = complex_samples_count / 8;
    for (size_t i = 0; i < chunks_count; i++)
    {
        // 0
        sample = to_int16(psample[0]) * scale_re;
        dc_re += k * (sample - dc_re);
        dst_re[0] = sample - dc_re;

        sample = to_int16(psample[1]) * scale_im;
        dc_im += k * (sample - dc_im);
        dst_im[0] = sample - dc_im;

        // 1
        dst_re[2] = to_int16(psample[2]) * scale_re - dc_re;
        dst_im[2] = to_int16(psample[3]) * scale_im - dc_im;

        // 2
        dst_re[4] = to_int16(psample[4]) * scale_re - dc_re;
        dst_im[4] = to_int16(psample[5]) * scale_im - dc_im;

        // 3
        dst_re[6] = to_int16(psample[6]) * scale_re - dc_re;
        dst_im[6] = to_int16(psample[7]) * scale_im - dc_im;

        // 4
        dst_re[8] = to_int16(psample[8]) * scale_re - dc_re;
        dst_im[8] = to_int16(psample[9]) * scale_im - dc_im;

        // 5
        dst_re[10] = to_int16(psample[10]) * scale_re - dc_re;
        dst_im[10] = to_int16(psample[11]) * scale_im - dc_im;

        // 6
        dst_re[12] = to_int16(psample[12]) * scale_re - dc_re;
        dst_im[12] = to_int16(psample[13]) * scale_im - dc_im;

        // 7
        dst_re[14] = to_int16(psample[14]) * scale_re - dc_re;
        dst_im[14] = to_int16(psample[15]) * scale_im - dc_im;

        dst_re += 16;
        dst_im += 16;
        psample +=16;
    }
    dev->rx_dc_re = dc_re;
    dev->rx_dc_im = dc_im;
    if (dev->rx_cb)
    {
        dev->rx_cb(dev->rx_buff, complex_samples_count, dev->rx_cb_ctx);
    }
}
//==============================================================================
void fobos_rx_proceed_calibration(struct fobos_dev_t * dev, void * data, uint32_t size)
{
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(%d)\n", __FUNCTION__, size);
#endif // FOBOS_PRINT_DEBUG
    size_t complex_samples_count = size / 4;

    int64_t summ_re = 0ll;
    int64_t summ_im = 0ll;
    int16_t * psample = (int16_t *)data;
    float * dst_re = dev->rx_buff;
    float * dst_im = dev->rx_buff + 1;
    for (size_t i = 0; i < complex_samples_count; i++)
    {
        int16_t re = *psample;
        *dst_re = to_int16(re);
        summ_re += re;
        dst_re += 2;
        psample++;
        int16_t im = *psample;
        *dst_im = to_int16(im);
        summ_im += im;
        dst_im += 2;
        psample++;
    }
    float dc_re = (float)summ_re / (float)complex_samples_count;
    float dc_im = (float)summ_im / (float)complex_samples_count;

    psample = (int16_t *)data;
    dst_re = dev->rx_buff;
    dst_im = dev->rx_buff + 1;
    double avg_abs_re = 0.0;
    double avg_abs_im = 0.0;
    for (size_t i = 0; i < complex_samples_count; i++)
    {
        *dst_re -= dc_re;
        float re = fabsf(*dst_re);
        dst_re += 2;
        *dst_im -= dc_im;
        float im = fabsf(*dst_im);
        dst_im += 2;
        avg_abs_re += re;
        avg_abs_im += im;
    }

    if ((avg_abs_re > 0.0f) && (avg_abs_im > 0.0f))
    {
        avg_abs_re /= (double)complex_samples_count;
        avg_abs_im /= (double)complex_samples_count;
        float scale_re = SAMPLE_NORM;
        float scale_im = scale_re * avg_abs_re / avg_abs_im;
#ifdef FOBOS_PRINT_DEBUG
        printf_internal("[%d] im/re scale = %f\n", dev->rx_calibration_pos, scale_im / scale_re);
#endif // FOBOS_PRINT_DEBUG
        if (dev->rx_calibration_pos == 0)
        {
            dev->rx_scale_re = scale_re;
            dev->rx_scale_im = scale_im;
        }
        else
        {
            float k = 0.1f;
            dev->rx_scale_re += k * (scale_re - dev->rx_scale_re);
            dev->rx_scale_im += k * (scale_im - dev->rx_scale_im);
        }
    }
}
//==============================================================================
int fobos_rx_set_calibration(struct fobos_dev_t * dev, int state)
{
    int result = fobos_check(dev);
    if (result != 0)
    {
        return result;
    }
    if (dev->rx_calibration_state != state)
    {
        switch (state)
        {
            case 0:
            {
                dev->rx_calibration_state = 0;
                dev->rx_calibration_pos = 0;
            }
            break;

            case 1: // "remove dc" calibration state
            {
                dev->rx_calibration_state = 1;
                dev->rx_calibration_pos = 0;
                if (dev->rx_direct_sampling)
                {
                    // turn on lpf
                    bitclear(dev->dev_gpo, FOBOS_DEV_LPF_A0);
                    bitclear(dev->dev_gpo, FOBOS_DEV_LPF_A1);
                    if (dev->rx_lpf_idx & 1)
                    {
                        bitset(dev->dev_gpo, FOBOS_DEV_LPF_A1);
                    }
                    if (dev->rx_lpf_idx & 2)
                    {
                        bitset(dev->dev_gpo, FOBOS_DEV_LPF_A0);
                    }
                    fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
                    // enable clocks
                    fobos_rffc507x_clock(dev, 1);
                    fobos_max2830_clock(dev, 1);
                    // enable rffc507x
                    fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x15], 14, 14, 1); // enbl = 1
                    fobos_rffc507x_commit(dev, 0);
                }
                // shut down both lnas
                bitclear(dev->dev_gpo, FOBOS_DEV_LNA_LP_SHD);
                bitclear(dev->dev_gpo, FOBOS_DEV_LNA_HP_SHD);
                // set_if_filter(high);
                bitclear(dev->dev_gpo, FOBOS_DEV_IF_V1);
                bitset(dev->dev_gpo, FOBOS_DEV_IF_V2);
                // turn on max2830 ant1 (main) input, turn off ant2 (aux) input
                bitclear(dev->dev_gpo, FOBOS_MAX2830_ANTSEL);
                // commit dev_gpo value
                fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
                // set frequency direct to max2830
                fobos_max2830_set_frequency(dev, 2400000000.0, 0);
                // disable rffc507x
                fobos_rffc507x_register_modify(&dev->rffc507x_registers_local[0x15], 14, 14, 0); // enbl = 0
                fobos_rffc507x_commit(dev, 0);
                // set frequency direct rffc507x
                fobos_rffc507x_set_lo_frequency(dev, 2401, 0);
            }
            break;
            case 2:
            {
                double f = dev->rx_frequency;
                dev->rx_frequency = 0.0;
                dev->rx_frequency_band = 0;
                fobos_rx_set_frequency(dev, f, 0);
                if (dev->rx_direct_sampling)
                {
                    dev->rx_direct_sampling = 0;
                    fobos_rx_set_direct_sampling(dev, 1);
                }
                dev->rx_calibration_state = 2;
            }
            break;
        }
    }
    return result;
}
//==============================================================================
int fobos_alloc_buffers(struct fobos_dev_t *dev)
{
    unsigned int i;
    int result = fobos_check(dev);
    if (result != 0)
    {
        return result;
    }
    if (!dev->transfer)
    {
        dev->transfer = malloc(dev->transfer_buf_count * sizeof(struct libusb_transfer *));
        if (dev->transfer)
        {
            for (i = 0; i < dev->transfer_buf_count; ++i)
            {
                dev->transfer[i] = libusb_alloc_transfer(0);
            }
        }
    }
    if (dev->transfer_buf)
    {
        return -2;
    }
    dev->transfer_buf = malloc(dev->transfer_buf_count * sizeof(unsigned char *));
    if (dev->transfer_buf)
    {
        memset(dev->transfer_buf, 0, dev->transfer_buf_count * sizeof(unsigned char*));
    }
#if defined(ENABLE_ZEROCOPY) && defined (__linux__) && LIBUSB_API_VERSION >= 0x01000105
    printf_internal("Allocating %d zero-copy buffers\n", dev->transfer_buf_count);
    dev->use_zerocopy = 1;
    for (i = 0; i < dev->transfer_buf_count; ++i)
    {
        dev->transfer_buf[i] = libusb_dev_mem_alloc(dev->libusb_devh, dev->transfer_buf_size);
        if (dev->transfer_buf[i])
        {
            if (dev->transfer_buf[i][0] || memcmp(dev->transfer_buf[i],
                dev->transfer_buf[i] + 1,
                dev->transfer_buf_size - 1))
            {
                printf_internal("Kernel usbfs mmap() bug, falling back to buffers\n");
                dev->use_zerocopy = 0;
                break;
            }
        }
        else
        {
            printf_internal("Failed to allocate zero-copy buffer for transfer %d\n", i);
            dev->use_zerocopy = 0;
            break;
        }
    }
    if (!dev->use_zerocopy)
    {
        for (i = 0; i < dev->transfer_buf_count; ++i)
        {
            if (dev->transfer_buf[i])
            {
                libusb_dev_mem_free(dev->libusb_devh, dev->transfer_buf[i], dev->transfer_buf_size);
            }
        }
    }
#endif
    if (!dev->use_zerocopy)
    {
        for (i = 0; i < dev->transfer_buf_count; ++i)
        {
            dev->transfer_buf[i] = (unsigned char *)malloc(dev->transfer_buf_size);

            if (!dev->transfer_buf[i])
            {
                return -ENOMEM;
            }
        }
    }
    return 0;
}
//==============================================================================
int fobos_free_buffers(struct fobos_dev_t *dev)
{
    unsigned int i;
    int result = fobos_check(dev);
    if (result != 0)
    {
        return result;
    }
    if (dev->transfer)
    {
        for (i = 0; i < dev->transfer_buf_count; ++i)
        {
            if (dev->transfer[i])
            {
                libusb_free_transfer(dev->transfer[i]);
            }
        }
        free(dev->transfer);
        dev->transfer = NULL;
    }
    if (dev->transfer_buf)
    {
        for (i = 0; i < dev->transfer_buf_count; ++i)
        {
            if (dev->transfer_buf[i])
            {
                if (dev->use_zerocopy)
                {
#if defined (__linux__) && LIBUSB_API_VERSION >= 0x01000105
                    libusb_dev_mem_free(dev->libusb_devh, dev->transfer_buf[i], dev->transfer_buf_size);
#endif
                }
                else
                {
                    free(dev->transfer_buf[i]);
                }
            }
        }
        free(dev->transfer_buf);
        dev->transfer_buf = NULL;
    }
    return 0;
}
//==============================================================================
static void LIBUSB_CALL _libusb_callback(struct libusb_transfer *transfer)
{
    struct fobos_dev_t *dev = (struct fobos_dev_t *)transfer->user_data;
    if (LIBUSB_TRANSFER_COMPLETED == transfer->status)
    {
        if (transfer->actual_length == (int)dev->transfer_buf_size)
        {
            dev->rx_buff_counter++;
            if ((dev->rx_calibration_state == 1))
            {
                fobos_rx_proceed_calibration(dev, transfer->buffer, transfer->actual_length);
                dev->rx_calibration_pos++;
            }
            else
            {
                //ULONGLONG t0, t1;
                //QueryPerformanceCounter(&t0);
                fobos_rx_proceed_rx_buff(dev, transfer->buffer, transfer->actual_length);
                //QueryPerformanceCounter(&t1);
                //double dt = (t1 - t0);
                //printf("%f\n", dt);
            }
        }
        else
        {
            printf_internal("E");
            dev->rx_failures++;
        }
        libusb_submit_transfer(transfer);
        dev->transfer_errors = 0;
    }
    else if (LIBUSB_TRANSFER_CANCELLED != transfer->status)
    {
        printf_internal("transfer->status = %d\n", transfer->status);
#ifndef _WIN32
        if (LIBUSB_TRANSFER_ERROR == transfer->status)
        {
            dev->transfer_errors++;
        }
        if (dev->transfer_errors >= (int)dev->transfer_buf_count || LIBUSB_TRANSFER_NO_DEVICE == transfer->status)
        {
            dev->dev_lost = 1;
            fobos_rx_cancel_async(dev);
        }
#else
        dev->dev_lost = 1;
        fobos_rx_cancel_async(dev);
#endif
    }
}
//==============================================================================
int fobos_rx_read_async(struct fobos_dev_t * dev, fobos_rx_cb_t cb, void *ctx, uint32_t buf_count, uint32_t buf_length)
{
    unsigned int i;
    int result = fobos_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s(0x%08x, 0x%08x, 0x%08x, %d, %d)\n", __FUNCTION__, (unsigned int)dev, (unsigned int)cb, (unsigned int)ctx, buf_count, buf_length);
#endif // FOBOS_PRINT_DEBUG
    if (result != 0)
    {
        return result;
    }
    if (FOBOS_IDDLE != dev->rx_async_status)
    {
        return -5;
    }
    result = 0;
    struct timeval tv0 = { 0, 0 };
    struct timeval tv1 = { 1, 0 };
    dev->rx_async_status = FOBOS_STARTING;
    dev->rx_async_cancel = 0;
    dev->rx_buff_counter = 0;
    dev->rx_cb = cb;
    dev->rx_cb_ctx = ctx;
    dev->rx_calibration_state = 0;
    fobos_rx_set_calibration(dev, 1); // start calibration
    if (buf_count == 0)
    {
        buf_count = FOBOS_DEF_BUF_COUNT;
    }
    if (buf_count > FOBOS_MAX_BUF_COUNT)
    {
        buf_count = FOBOS_MAX_BUF_COUNT;
    }
    dev->transfer_buf_count = buf_count;

    if (buf_length == 0)
    {
        buf_length = FOBOS_DEF_BUF_LENGTH;
    }
    buf_length = 128 * (buf_length / 128);

    uint32_t transfer_buf_size = buf_length * 4; //raw int16 buff size
    transfer_buf_size = 512 * (transfer_buf_size / 512); // len must be multiple of 512

    dev->transfer_buf_size = transfer_buf_size;

    result = fobos_alloc_buffers(dev);
    if (result != 0)
    {
        return result;
    }

    dev->rx_buff = (float*)malloc(buf_length * 2 * sizeof(float));

    fobos_fx3_command(dev, 0xE1, 1, 0);        // start fx

    bitclear(dev->dev_gpo, FOBOS_DEV_ADC_SDI);
    fobos_rx_set_dev_gpo(dev, dev->dev_gpo);

    for (i = 0; i < dev->transfer_buf_count; ++i)
    {
        libusb_fill_bulk_transfer(dev->transfer[i],
            dev->libusb_devh,
            LIBUSB_BULK_IN_ENDPOINT,
            dev->transfer_buf[i],
            dev->transfer_buf_size,
            _libusb_callback,
            (void *)dev,
            LIBUSB_BULK_TIMEOUT);

        result = libusb_submit_transfer(dev->transfer[i]);
        if (result < 0)
        {
            printf_internal("Failed to submit transfer #%i, err %i\n", i, result);
            dev->rx_async_status = FOBOS_CANCELING;
            break;
        }
    }

    dev->rx_async_status = FOBOS_RUNNING;
    while (FOBOS_IDDLE != dev->rx_async_status)
    {
        if (dev->rx_calibration_state == 1)
        {
            if (dev->rx_calibration_pos >= 4)
            {
                fobos_rx_set_calibration(dev, 2);
            }
        }

        result = libusb_handle_events_timeout_completed(dev->libusb_ctx, &tv1, &dev->rx_async_cancel);
        if (result < 0)
        {
            printf_internal("libusb_handle_events_timeout_completed returned: %d\n", result);
            if (result == LIBUSB_ERROR_INTERRUPTED)
            {
                continue;
            }
            else
            {
                break;
            }
        }

        if (FOBOS_CANCELING == dev->rx_async_status)
        {
            printf_internal("FOBOS_CANCELING \n");
            dev->rx_async_status = FOBOS_IDDLE;
            if (!dev->transfer)
            {
                break;
            }
            for (i = 0; i < dev->transfer_buf_count; ++i)
            {
                if (!dev->transfer[i])
                    continue;

                if (LIBUSB_TRANSFER_CANCELLED != dev->transfer[i]->status)
                {
                    result = libusb_cancel_transfer(dev->transfer[i]);
                    libusb_handle_events_timeout_completed(dev->libusb_ctx, &tv1, NULL);
                    if (result < 0)
                    {
                        printf_internal("libusb_cancel_transfer returned: %d\n", result);
                        continue;
                    }
                    dev->rx_async_status = FOBOS_CANCELING;
                }
            }
            if (dev->dev_lost || FOBOS_IDDLE == dev->rx_async_status)
            {
                libusb_handle_events_timeout_completed(dev->libusb_ctx, &tv0, NULL);
                break;
            }
        }
    }
    fobos_fx3_command(dev, 0xE1, 0, 0);       // stop fx
    fobos_free_buffers(dev);
    free(dev->rx_buff);
    dev->rx_buff = NULL;
    bitset(dev->dev_gpo, FOBOS_DEV_ADC_SDI);
    fobos_rx_set_dev_gpo(dev, dev->dev_gpo);
    dev->rx_async_status = FOBOS_IDDLE;
    dev->rx_async_cancel = 0;
    return result;
}
//==============================================================================
int fobos_rx_cancel_async(struct fobos_dev_t * dev)
{
    int result = fobos_check(dev);
#ifdef FOBOS_PRINT_DEBUG
    printf_internal("%s()\n", __FUNCTION__);
#endif // FOBOS_PRINT_DEBUG
    if (result != 0)
    {
        return result;
    }
    if (FOBOS_RUNNING == dev->rx_async_status)
    {
        dev->rx_async_status = FOBOS_CANCELING;
        dev->rx_async_cancel = 1;
    }
    return 0;
}
//==============================================================================
const char * fobos_rx_error_name(int error)
{
    switch (error)
    {
        case  0:   return "Ok";
        case -1:   return "No device spesified, dev == NUL";
        case -2:   return "Device is not open, please use fobos_rx_open() first";
        case -5:   return "Device is not ready for reading";
        default:   return "Unknown error";
    }
}
//==============================================================================
