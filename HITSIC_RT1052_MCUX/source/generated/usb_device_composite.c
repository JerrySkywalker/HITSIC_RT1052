/*
 * Copyright 2015-2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2019 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 * 
 * 3. Neither the name of copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"

#include "usb_device_class.h"
#include "usb_device_ch9.h"
#include "usb_device_descriptor.h"

#include "usb_device_composite.h"

#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
#include "fsl_sysmpu.h"
#endif
#if defined(FSL_FEATURE_SOC_USBPHY_COUNT) && (FSL_FEATURE_SOC_USBPHY_COUNT > 0U)
#include "usb_phy.h"
#endif
#if (defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U))
#include "fsl_power.h"
#endif
#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
#include "fsl_mrt.h"
#include "fsl_power.h"
#endif
/*******************************************************************************
 * Definitions
 ******************************************************************************/

/* USB PHY condfiguration */
#ifndef BOARD_USB_PHY_D_CAL
#define BOARD_USB_PHY_D_CAL (0x0CU)
#endif
#ifndef BOARD_USB_PHY_TXCAL45DP
#define BOARD_USB_PHY_TXCAL45DP (0x06U)
#endif
#ifndef BOARD_USB_PHY_TXCAL45DM
#define BOARD_USB_PHY_TXCAL45DM (0x06U)
#endif
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
 /*!
 * @brief Initializes board hardware.
 */
void BOARD_InitHardware(void);

void USB_DeviceIsrEnable(void);
#if USB_DEVICE_CONFIG_USE_TASK
void USB_DeviceTaskFn(void *deviceHandle);
#endif

static void USB_DeviceClockInit(void);

#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
void USB_DeviceHsPhyChirpIssueWorkaround(void);
void USB_DeviceDisconnected(void);
#endif

#if defined (USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U)
static clock_usb_src_t USB0_GetClockSource(void);
#endif

static usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param);
static usb_status_t USB_UpdateInterfaceSetting(uint8_t interface, uint8_t alternateSetting);

/*******************************************************************************
 * Variables
 ******************************************************************************/
#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
volatile uint32_t hwTick;
uint32_t timerInterval;
uint32_t isConnectedToFsHost = 0U;
uint32_t isConnectedToHsHost = 0U;
#endif

#if (defined(USB_DEVICE_CHARGER_DETECT_ENABLE) && (USB_DEVICE_CHARGER_DETECT_ENABLE > 0U)) && \
    ((defined(FSL_FEATURE_SOC_USBDCD_COUNT) && (FSL_FEATURE_SOC_USBDCD_COUNT > 0U)) ||        \
    (defined(FSL_FEATURE_SOC_USBHSDCD_COUNT) && (FSL_FEATURE_SOC_USBHSDCD_COUNT > 0U)))
usb_device_dcd_charging_time_t g_UsbDeviceDcdTimingConfig;
#endif

usb_device_composite_struct_t g_UsbDeviceComposite;


/* Set class configurations. */
usb_device_class_config_struct_t g_CompositeClassConfig[USB_COMPOSITE_INTERFACE_COUNT] = {
};

/* Set class configuration list. */
usb_device_class_config_list_struct_t g_UsbDeviceCompositeConfigList = {
    g_CompositeClassConfig, USB_DeviceCallback, USB_COMPOSITE_INTERFACE_COUNT,
};

/*******************************************************************************
 * Code
 ******************************************************************************/
#if defined (USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U)
/*!
 * @brief Function to retrieve clock source for USB0.
 *
 * @return Used clock source
 */
static clock_usb_src_t USB0_GetClockSource()
{
    clock_usb_src_t src;
    switch (SYSCON->USB0CLKSEL)
    {
        case 0U:
            src = kCLOCK_UsbSrcFro;
            break;
        case 1U:
            src = kCLOCK_UsbSrcSystemPll;
            break;
        case 2U:
            src = kCLOCK_UsbSrcUsbPll;
            break;
        case 7U:
        default:
            src = kCLOCK_UsbSrcNone;
            break;
    }
    return src;
}
#endif

#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
/*!
 * @brief Initialization on MRT timer
 *
 * @param uint8_t Instance of timer.
 * @interval uint32_t Interrupt interval.
 *
 */
void USB_TimerInit(uint8_t instance, uint32_t interval)
{
    MRT_Type *instanceList[] = MRT_BASE_PTRS;
    IRQn_Type instanceIrq[] = MRT_IRQS;
    /* Structure of initialize MRT */
    mrt_config_t mrtConfig;
    /* mrtConfig.enableMultiTask = false; */
    MRT_GetDefaultConfig(&mrtConfig);
    /* Init mrt module */
    MRT_Init(instanceList[instance], &mrtConfig);
    /* Setup Channel 0 to be repeated */
    MRT_SetupChannelMode(instanceList[instance], kMRT_Channel_0, kMRT_RepeatMode);
    /* Enable timer interrupts for channel 0 */
    MRT_EnableInterrupts(instanceList[instance], kMRT_Channel_0, kMRT_TimerInterruptEnable);
    timerInterval = interval;
    /* Enable at the NVIC */
    EnableIRQ(instanceIrq[instance]);
}

/*!
 * @brief Function used to start/stop the MRT timer
 *
 * @param uint8_t MRT timer instance.
 * @param uint8_t Set to 0 to disable the timer. Other values enable the timer.
 */
void USB_TimerInt(uint8_t instance, uint8_t enable)
{
    MRT_Type *instanceList[] = MRT_BASE_PTRS;
    uint32_t mrt_clock;
    mrt_clock = CLOCK_GetFreq(kCLOCK_BusClk);
    if (enable)
    {
        /* Start channel 0 */
        MRT_StartTimer(instanceList[instance], kMRT_Channel_0, USEC_TO_COUNT(timerInterval, mrt_clock));
    }
    else
    {
        /* Stop channel 0 */
        MRT_StopTimer(instanceList[instance], kMRT_Channel_0);
        /* Clear interrupt flag.*/
        MRT_ClearStatusFlags(instanceList[instance], kMRT_Channel_0, kMRT_TimerInterruptFlag);
    }
}

/*!
 * @brief MRT0 interrupt service routine
 */
void MRT0_IRQHandler(void)
{
    /* Clear interrupt flag.*/
    MRT_ClearStatusFlags(MRT0, kMRT_Channel_0, kMRT_TimerInterruptFlag);
    if (hwTick)
    {
        hwTick--;
        if (!hwTick)
        {
            USB_TimerInt(0, 0);
        }
    }
    else
    {
        USB_TimerInt(0, 0);
    }
}

/*!
 * @brief Clears device connected flag for chirp issue
 */
void USB_DeviceDisconnected(void)
{
    isConnectedToFsHost = 0U;
}

/*!
 * @brief Chirp issue workaround
 *
 * This is a work-around to fix the HS device Chirping issue.
 * The device (IP3511HS controller) will sometimes not work when the cable
 * is attached first time after a Power-on Reset.
 */
void USB_DeviceHsPhyChirpIssueWorkaround(void)
{
    uint32_t startFrame = USBHSD->INFO & USBHSD_INFO_FRAME_NR_MASK;
    uint32_t currentFrame;
    uint32_t isConnectedToFsHostFlag = 0U;
    
    if ((!isConnectedToHsHost) && (!isConnectedToFsHost))
    {
        if (((USBHSD->DEVCMDSTAT & USBHSD_DEVCMDSTAT_Speed_MASK) >> USBHSD_DEVCMDSTAT_Speed_SHIFT) == 0x01U)
        {
            USBHSD->DEVCMDSTAT = (USBHSD->DEVCMDSTAT & (~(0x0F000000U | USBHSD_DEVCMDSTAT_PHY_TEST_MODE_MASK))) |
                                 USBHSD_DEVCMDSTAT_PHY_TEST_MODE(0x05U);
            hwTick = 100;
            USB_TimerInt(0, 1);
            
            while (hwTick)
            {
            }
            
            currentFrame = USBHSD->INFO & USBHSD_INFO_FRAME_NR_MASK;
            
            if (currentFrame != startFrame)
            {
                isConnectedToHsHost = 1U;
            }
            else
            {
                hwTick = 1;
                USB_TimerInt(0, 1);
                
                while (hwTick)
                {
                }
                
                currentFrame = USBHSD->INFO & USBHSD_INFO_FRAME_NR_MASK;
                
                if (currentFrame != startFrame)
                {
                    isConnectedToHsHost = 1U;
                }
                else
                {
                    isConnectedToFsHostFlag = 1U;
                }
            }
            
            USBHSD->DEVCMDSTAT = (USBHSD->DEVCMDSTAT & (~(0x0F000000U | USBHSD_DEVCMDSTAT_PHY_TEST_MODE_MASK)));
            USBHSD->DEVCMDSTAT = (USBHSD->DEVCMDSTAT & (~(0x0F000000U | USBHSD_DEVCMDSTAT_DCON_MASK)));
            hwTick = 510;
            USB_TimerInt(0, 1);
            
            while (hwTick)
            {
            }
            
            USBHSD->DEVCMDSTAT = (USBHSD->DEVCMDSTAT & (~(0x0F000000U))) | USB_DEVCMDSTAT_DCON_C_MASK;
            USBHSD->DEVCMDSTAT =
                (USBHSD->DEVCMDSTAT & (~(0x0F000000U))) | USBHSD_DEVCMDSTAT_DCON_MASK | USB_DEVCMDSTAT_DRES_C_MASK;
                
            if (isConnectedToFsHostFlag)
            {
                isConnectedToFsHost = 1U;
            }
        }
    }
}
#endif

/*!
 * @brief USB Interrupt service routine.
 *
 * This function serves as the USB interrupt service routine.
 *
 * @return None.
 */
void USB_OTG1_IRQHandler(void)
{
    USB_DeviceEhciIsrFunction(g_UsbDeviceComposite.deviceHandle);

#if (defined(USB_DEVICE_CONFIG_KHCI) && (USB_DEVICE_CONFIG_KHCI > 0U)) || (defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U))
    /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F Store immediate overlapping
    exception return operation might vector to incorrect interrupt */
    __DSB();
#endif
}

/*!
 * @brief Initializes USB specific setting that was not set by the Clocks tool.
 */
void USB_DeviceClockInit(void)
{
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL, BOARD_USB_PHY_TXCAL45DP, BOARD_USB_PHY_TXCAL45DM,
    };
    uint32_t notUsed = 0;

    if (USB_DEVICE_CONTROLLER_ID == kUSB_ControllerEhci0)
    {
        CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, 480000000U);
        CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, 480000000U);
    }
    else
    {
        CLOCK_EnableUsbhs1PhyPllClock(kCLOCK_Usbphy480M, 480000000U);
        CLOCK_EnableUsbhs1Clock(kCLOCK_Usb480M, 480000000U);
    }

    USB_EhciPhyInit(USB_DEVICE_CONTROLLER_ID, notUsed, &phyConfig);
}

/*!
 * @brief Enables interrupt service routines for device.
 */
void USB_DeviceIsrEnable(void)
{
    uint8_t irqNumber;
#if defined(USB_DEVICE_CONFIG_KHCI) && (USB_DEVICE_CONFIG_KHCI > 0U)
    uint8_t usbDeviceKhciIrq[] = USB_IRQS;
    irqNumber = usbDeviceKhciIrq[USB_DEVICE_CONTROLLER_ID - kUSB_ControllerKhci0];
#endif
#if defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)
    uint8_t usbDeviceEhciIrq[] = USBHS_IRQS;
    irqNumber = usbDeviceEhciIrq[USB_DEVICE_CONTROLLER_ID - kUSB_ControllerEhci0];
#endif
#if defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U)
    uint8_t usbDeviceIP3511Irq[] = USB_IRQS;
    irqNumber = usbDeviceIP3511Irq[USB_DEVICE_CONTROLLER_ID - kUSB_ControllerLpcIp3511Fs0];
#endif
#if defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U)
    uint8_t usbDeviceIP3511Irq[] = USBHSD_IRQS;
    irqNumber = usbDeviceIP3511Irq[USB_DEVICE_CONTROLLER_ID - kUSB_ControllerLpcIp3511Hs0];
#endif
/* Install isr, set priority, and enable IRQ. */
#if defined(__GIC_PRIO_BITS)
    GIC_SetPriority((IRQn_Type)irqNumber, USB_DEVICE_INTERRUPT_PRIORITY);
#else
    NVIC_SetPriority((IRQn_Type)irqNumber, USB_DEVICE_INTERRUPT_PRIORITY);
#endif
    EnableIRQ((IRQn_Type)irqNumber);
}

#if USB_DEVICE_CONFIG_USE_TASK
/*!
 * @brief USB device task. When USB device does not use interrupt service routines, this function should be called periodically.
 *
 * @param *deviceHandle Pointer to device handle.
 */
void USB_DeviceTaskFn(void *deviceHandle)
{
#if defined(USB_DEVICE_CONFIG_KHCI) && (USB_DEVICE_CONFIG_KHCI > 0U)
        USB_DeviceKhciTaskFunction(deviceHandle);
#endif
#if defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)
        USB_DeviceEhciTaskFunction(deviceHandle);
#endif
#if defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U)
    USB_DeviceLpcIp3511TaskFunction(deviceHandle);
#endif
#if defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U)
    USB_DeviceLpcIp3511TaskFunction(deviceHandle);
#endif
}
#endif

/*!
 * @brief Standard device callback function.
 *
 * This function handle the USB standard event. For more information, please refer to usb spec chapter 9.
 * @param handle          The USB device handle.
 * @param event           The USB device event type.
 * @param *param          The parameter of the device specific request.
 * @return usb_status_t Returns status of operation.
 */
static usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param)
{
    usb_status_t error = kStatus_USB_Error;
    uint16_t *temp16 = (uint16_t *)param;
    uint8_t *temp8 = (uint8_t *)param;

    switch (event)
    {
        case kUSB_DeviceEventBusReset:
            /* USB bus reset signal detected */
            g_UsbDeviceComposite.attach = 0U;
            error = kStatus_USB_Success;
#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
            /* The work-around is used to fix the HS device Chirping issue.
             * Please refer to the implementation for the detail information.
             */
            USB_DeviceHsPhyChirpIssueWorkaround();
#endif
#if defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U) || \
    (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
            /* Get USB speed to configure the device, including max packet size and interval of the endpoints. */
            if (kStatus_USB_Success == USB_DeviceClassGetSpeed(USB_DEVICE_CONTROLLER_ID, &g_UsbDeviceComposite.speed))
            {
                USB_DeviceSetSpeed(handle, g_UsbDeviceComposite.speed);
            }
#endif
            break;
#if (defined(USB_DEVICE_CONFIG_DETACH_ENABLE) && (USB_DEVICE_CONFIG_DETACH_ENABLE > 0U))
        case kUSB_DeviceEventAttach:
#if (defined(USB_DEVICE_CHARGER_DETECT_ENABLE) && (USB_DEVICE_CHARGER_DETECT_ENABLE > 0U)) && \
    ((defined(FSL_FEATURE_SOC_USBDCD_COUNT) && (FSL_FEATURE_SOC_USBDCD_COUNT > 0U)) ||        \
     (defined(FSL_FEATURE_SOC_USBHSDCD_COUNT) && (FSL_FEATURE_SOC_USBHSDCD_COUNT > 0U)))
            g_UsbDeviceComposite.vReginInterruptDetected = 1;
            g_UsbDeviceComposite.vbusValid = 1;
#else
#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U)) || \
    (defined(USB_DEVICE_CONFIG_LPCIP3511FS) && (USB_DEVICE_CONFIG_LPCIP3511FS > 0U))
#else
            USB_DeviceRun(g_UsbDeviceComposite.deviceHandle);
#endif
#endif
            break;
        case kUSB_DeviceEventDetach:
#if (defined(USB_DEVICE_CHARGER_DETECT_ENABLE) && (USB_DEVICE_CHARGER_DETECT_ENABLE > 0U)) && \
    ((defined(FSL_FEATURE_SOC_USBDCD_COUNT) && (FSL_FEATURE_SOC_USBDCD_COUNT > 0U)) ||        \
     (defined(FSL_FEATURE_SOC_USBHSDCD_COUNT) && (FSL_FEATURE_SOC_USBHSDCD_COUNT > 0U)))
            g_UsbDeviceComposite.vReginInterruptDetected = 1;
            g_UsbDeviceComposite.vbusValid = 0;
            g_UsbDeviceComposite.attach = 0;
#else
            g_UsbDeviceComposite.attach = 0;
#if (defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)) || \
    (defined(USB_DEVICE_CONFIG_KHCI) && (USB_DEVICE_CONFIG_KHCI > 0U))
        USB_DeviceStop(g_UsbDeviceComposite.deviceHandle);
#else
#if (defined(USB_DEVICE_CONFIG_LPCIP3511HS) && (USB_DEVICE_CONFIG_LPCIP3511HS > 0U))
            USB_DeviceDisconnected();
#endif
#endif
#endif
            break;
#endif
        case kUSB_DeviceEventSetConfiguration:
            if (0 == temp8)
            {
                g_UsbDeviceComposite.attach = 0U;
                g_UsbDeviceComposite.currentConfiguration = 0U;
            }
            else if (USB_COMPOSITE_CONFIGURATION_INDEX == (*temp8))
            {
                /* Set device configuration request */
                g_UsbDeviceComposite.attach = 1U;
                g_UsbDeviceComposite.currentConfiguration = *temp8;
                error = kStatus_USB_Success;
            }
            else
            {
                error = kStatus_USB_InvalidRequest;
            }
            break;
        case kUSB_DeviceEventSetInterface:
            if (g_UsbDeviceComposite.attach)
            {
                /* Set device interface request */
                uint8_t interface = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                uint8_t alternateSetting = (uint8_t)(*temp16 & 0x00FFU);

                if (interface < USB_COMPOSITE_INTERFACE_COUNT)
                {
                    error = USB_UpdateInterfaceSetting(interface, alternateSetting);
                }
            }
            break;
        case kUSB_DeviceEventGetConfiguration:
            if (param)
            {
                /* Get current configuration request */
                *temp8 = g_UsbDeviceComposite.currentConfiguration;
                error = kStatus_USB_Success;
            }
            break;
        case kUSB_DeviceEventGetInterface:
            if (param)
            {
                /* Get current alternate setting of the interface request */
                uint8_t interface = (uint8_t)((*temp16 & 0xFF00U) >> 0x08U);
                if (interface < USB_COMPOSITE_INTERFACE_COUNT)
                {
                    *temp16 = (*temp16 & 0xFF00U) | g_UsbDeviceComposite.currentInterfaceAlternateSetting[interface];
                    error = kStatus_USB_Success;
                }
                else
                {
                    error = kStatus_USB_InvalidRequest;
                }
            }
            break;
        case kUSB_DeviceEventGetDeviceDescriptor:
            if (param)
            {
                /* Get device descriptor request */
                error = USB_DeviceGetDeviceDescriptor(handle, (usb_device_get_device_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetConfigurationDescriptor:
            if (param)
            {
                /* Get device configuration descriptor request */
                error = USB_DeviceGetConfigurationDescriptor(handle,
                                                             (usb_device_get_configuration_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetStringDescriptor:
            if (param)
            {
                /* Get device string descriptor request */
                error = USB_DeviceGetStringDescriptor(handle, (usb_device_get_string_descriptor_struct_t *)param);
            }
            break;
#if (USB_DEVICE_CONFIG_HID > 0U)
        case kUSB_DeviceEventGetHidDescriptor:
            if (param)
            {
                /* Get hid descriptor request */
                error = USB_DeviceGetHidDescriptor(handle, (usb_device_get_hid_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetHidReportDescriptor:
            if (param)
            {
                /* Get hid report descriptor request */
                error =
                    USB_DeviceGetHidReportDescriptor(handle, (usb_device_get_hid_report_descriptor_struct_t *)param);
            }
            break;
        case kUSB_DeviceEventGetHidPhysicalDescriptor:
            if (param)
            {
                /* Get hid physical descriptor request */
                error = USB_DeviceGetHidPhysicalDescriptor(handle,
                                                           (usb_device_get_hid_physical_descriptor_struct_t *)param);
            }
            break;
#endif /* USB_DEVICE_CONFIG_HID */
#if (defined(USB_DEVICE_CONFIG_CV_TEST) && (USB_DEVICE_CONFIG_CV_TEST > 0U))
        case kUSB_DeviceEventGetDeviceQualifierDescriptor:
            if (param)
            {
                /* Get Qualifier descriptor request */
                error = USB_DeviceGetDeviceQualifierDescriptor(
                    handle, (usb_device_get_device_qualifier_descriptor_struct_t *)param);
            }
            break;
#endif
#if (defined(USB_DEVICE_CHARGER_DETECT_ENABLE) && (USB_DEVICE_CHARGER_DETECT_ENABLE > 0U)) && \
    ((defined(FSL_FEATURE_SOC_USBDCD_COUNT) && (FSL_FEATURE_SOC_USBDCD_COUNT > 0U)) ||        \
    (defined(FSL_FEATURE_SOC_USBHSDCD_COUNT) && (FSL_FEATURE_SOC_USBHSDCD_COUNT > 0U)))
        case kUSB_DeviceEventDcdTimeOut:
            if (g_UsbDeviceComposite.dcdDevStatus == kUSB_DeviceDCDDevStatusVBUSDetect)
            {
                g_UsbDeviceComposite.dcdDevStatus = kUSB_DeviceDCDDevStatusTimeOut;
            }
            break;
        case kUSB_DeviceEventDcdUnknownType:
            if (g_UsbDeviceComposite.dcdDevStatus == kUSB_DeviceDCDDevStatusVBUSDetect)
            {
                g_UsbDeviceComposite.dcdDevStatus = kUSB_DeviceDCDDevStatusUnknownType;
            }
            break;
        case kUSB_DeviceEventSDPDetected:
            if (g_UsbDeviceComposite.dcdDevStatus == kUSB_DeviceDCDDevStatusVBUSDetect)
            {
                g_UsbDeviceComposite.dcdPortType = kUSB_DeviceDCDPortTypeSDP;
                g_UsbDeviceComposite.dcdDevStatus = kUSB_DeviceDCDDevStatusDetectFinish;
            }
            break;
        case kUSB_DeviceEventChargingPortDetected:
            if (g_UsbDeviceComposite.dcdDevStatus == kUSB_DeviceDCDDevStatusVBUSDetect)
            {
                g_UsbDeviceComposite.dcdDevStatus = kUSB_DeviceDCDDevStatusChargingPortDetect;
            }
            break;
        case kUSB_DeviceEventChargingHostDetected:
            if (g_UsbDeviceComposite.dcdDevStatus == kUSB_DeviceDCDDevStatusVBUSDetect)
            {
                g_UsbDeviceComposite.dcdDevStatus = kUSB_DeviceDCDDevStatusDetectFinish;
                g_UsbDeviceComposite.dcdPortType = kUSB_DeviceDCDPortTypeCDP;
            }
            break;
        case kUSB_DeviceEventDedicatedChargerDetected:
            if (g_UsbDeviceComposite.dcdDevStatus == kUSB_DeviceDCDDevStatusVBUSDetect)
            {
                g_UsbDeviceComposite.dcdDevStatus = kUSB_DeviceDCDDevStatusDetectFinish;
                g_UsbDeviceComposite.dcdPortType = kUSB_DeviceDCDPortTypeDCP;
            }
            break;
#endif
        default:
            break;
    }
    return error;
}

/*!
 * @brief Select interface and call setInterface callback.
 *
 * @param interface          Number of interface to be set to.
 * @param alternateSetting    Alternate setting to be used.
 * @return usb_status_t Returns status of operation.
 */
usb_status_t USB_UpdateInterfaceSetting(uint8_t interface, uint8_t alternateSetting)
{
    usb_status_t ret = kStatus_USB_Error;

    /* select appropriate interface to be updated*/
    switch (interface)
    {
    }

    if (ret == kStatus_USB_Success)
    {
        //interface setting was set
        g_UsbDeviceComposite.currentInterfaceAlternateSetting[interface] = alternateSetting;
    }

    return ret;
}

/*!
 * @brief Completely initializes USB device.
 *
 * This function calls other USB device functions and directly initializes following: USB specific clocks, g_UsbDeviceComposite values, USB stack, class drivers and device isr.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceApplicationInit(void)
{
	usb_status_t status;

    USB_DeviceClockInit();
#if (defined(FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT > 0U))
    SYSMPU_Enable(SYSMPU, 0);
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */

    g_UsbDeviceComposite.speed = USB_SPEED_FULL;
    g_UsbDeviceComposite.attach = 0U;
    g_UsbDeviceComposite.deviceHandle = NULL;

#if (defined(USB_DEVICE_CHARGER_DETECT_ENABLE) && (USB_DEVICE_CHARGER_DETECT_ENABLE > 0U)) && \
    ((defined(FSL_FEATURE_SOC_USBDCD_COUNT) && (FSL_FEATURE_SOC_USBDCD_COUNT > 0U)) ||        \
     (defined(FSL_FEATURE_SOC_USBHSDCD_COUNT) && (FSL_FEATURE_SOC_USBHSDCD_COUNT > 0U)))
    g_UsbDeviceComposite.dcdDevStatus = kUSB_DeviceDCDDevStatusDetached;

    g_UsbDeviceDcdTimingConfig.dcdSeqInitTime = USB_DEVICE_DCD_SEQ_INIT_TIME;
    g_UsbDeviceDcdTimingConfig.dcdDbncTime = USB_DEVICE_DCD_DBNC_MSEC;
    g_UsbDeviceDcdTimingConfig.dcdDpSrcOnTime = USB_DEVICE_DCD_VDPSRC_ON_MSEC;
    g_UsbDeviceDcdTimingConfig.dcdTimeWaitAfterPrD = USB_DEVICE_DCD_TIME_WAIT_AFTER_PRI_DETECTION;
    g_UsbDeviceDcdTimingConfig.dcdTimeDMSrcOn = USB_DEVICE_DCD_TIME_DM_SRC_ON;
#endif

    /* Initialize the usb stack and class drivers. */
    status = USB_DeviceClassInit(USB_DEVICE_CONTROLLER_ID, &g_UsbDeviceCompositeConfigList, &g_UsbDeviceComposite.deviceHandle);

    if (kStatus_USB_Success != status)
    {
        return status;
    }
    
    /* Get the class handle. */

    USB_DeviceIsrEnable();

#if (defined(USB_DEVICE_CHARGER_DETECT_ENABLE) && (USB_DEVICE_CHARGER_DETECT_ENABLE > 0U)) && \
    ((defined(FSL_FEATURE_SOC_USBDCD_COUNT) && (FSL_FEATURE_SOC_USBDCD_COUNT > 0U)) ||        \
     (defined(FSL_FEATURE_SOC_USBHSDCD_COUNT) && (FSL_FEATURE_SOC_USBHSDCD_COUNT > 0U)))
#else    
    USB_DeviceRun(g_UsbDeviceComposite.deviceHandle);
#endif

    return status;
}

/*!
 * @brief USB device charger task function.
 *
 * This function handles the USB device charger events.
 * @param usbDeviceCompositeDcd Device charger configuration.
 * @return None.
 */
#if (defined(USB_DEVICE_CHARGER_DETECT_ENABLE) && (USB_DEVICE_CHARGER_DETECT_ENABLE > 0U)) && \
    ((defined(FSL_FEATURE_SOC_USBDCD_COUNT) && (FSL_FEATURE_SOC_USBDCD_COUNT > 0U)) ||        \
     (defined(FSL_FEATURE_SOC_USBHSDCD_COUNT) && (FSL_FEATURE_SOC_USBHSDCD_COUNT > 0U)))
void USB_DeviceChargerTask(usb_device_composite_struct_t *usbDeviceCompositeDcd)
{
    if (usbDeviceCompositeDcd->vReginInterruptDetected)
    {
        usbDeviceCompositeDcd->vReginInterruptDetected = 0;
        if (usbDeviceCompositeDcd->vbusValid)
        {
            USB_DeviceDcdInitModule(usbDeviceCompositeDcd->deviceHandle, &g_UsbDeviceDcdTimingConfig);
            usbDeviceCompositeDcd->dcdDevStatus = kUSB_DeviceDCDDevStatusVBUSDetect;
        }
        else
        {
            USB_DeviceDcdDeinitModule(usbDeviceCompositeDcd->deviceHandle);
            USB_DeviceStop(usbDeviceCompositeDcd->deviceHandle);
            usbDeviceCompositeDcd->dcdPortType = kUSB_DeviceDCDPortTypeNoPort;
            usbDeviceCompositeDcd->dcdDevStatus = kUSB_DeviceDCDDevStatusDetached;
        }
    }

    if (usbDeviceCompositeDcd->dcdDevStatus == kUSB_DeviceDCDDevStatusChargingPortDetect) /* This is only for BC1.1 */
    {
        USB_DeviceRun(usbDeviceCompositeDcd->deviceHandle);
    }
    if (usbDeviceCompositeDcd->dcdDevStatus == kUSB_DeviceDCDDevStatusTimeOut)
    {
        usb_echo("Timeout error.\r\n");
        usbDeviceCompositeDcd->dcdDevStatus = kUSB_DeviceDCDDevStatusComplete;
    }
    if (usbDeviceCompositeDcd->dcdDevStatus == kUSB_DeviceDCDDevStatusUnknownType)
    {
        usb_echo("Unknown port type.\r\n");
        usbDeviceCompositeDcd->dcdDevStatus = kUSB_DeviceDCDDevStatusComplete;
    }
    if (usbDeviceCompositeDcd->dcdDevStatus == kUSB_DeviceDCDDevStatusDetectFinish)
    {
        if (usbDeviceCompositeDcd->dcdPortType == kUSB_DeviceDCDPortTypeSDP)
        {
            USB_DeviceRun(usbDeviceCompositeDcd->deviceHandle); /* If the facility attached is SDP, start enumeration */
        }
        else if (usbDeviceCompositeDcd->dcdPortType == kUSB_DeviceDCDPortTypeCDP)
        {
            USB_DeviceRun(usbDeviceCompositeDcd->deviceHandle); /* If the facility attached is CDP, start enumeration */
        }
        
        usbDeviceCompositeDcd->dcdDevStatus = kUSB_DeviceDCDDevStatusComplete;
    }
}
#endif

/*!
 * @brief USB device tasks function.
 *
 * This function runs the tasks for USB device.
 *
 * @return None.
 */
void USB_DeviceTasks(void)
{
#if USB_DEVICE_CONFIG_USE_TASK
    USB_DeviceTaskFn(g_UsbDeviceComposite.deviceHandle);
#endif
#if (defined(USB_DEVICE_CHARGER_DETECT_ENABLE) && (USB_DEVICE_CHARGER_DETECT_ENABLE > 0U)) && \
    ((defined(FSL_FEATURE_SOC_USBDCD_COUNT) && (FSL_FEATURE_SOC_USBDCD_COUNT > 0U)) ||        \
     (defined(FSL_FEATURE_SOC_USBHSDCD_COUNT) && (FSL_FEATURE_SOC_USBHSDCD_COUNT > 0U)))
    USB_DeviceChargerTask(&g_UsbDeviceComposite);
#endif
}
