/*
  Copyright (c) 2015, Augusto Fraga Giachero
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL AUGUSTO FRAGA GIACHERO BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 * Blink a LED
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "stm32f103xb.h"
#include "clock_cfg.h"
#include "usb.h"
#include "usb_cdc.h"
#include "circular_buffer.h"

static const char b64_table[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
  'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
  'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
  'w', 'x', 'y', 'z', '0', '1', '2', '3',
  '4', '5', '6', '7', '8', '9', '+', '/'
};

EndPointBuffer ep_buff[32];
RingBuffer ep_circ_buff;
static volatile uint32_t sample_rate = 48000;

/* This file is the part of the Lightweight USB device Stack for STM32 microcontrollers
 *
 * Copyright ©2016 Dmitry Filimonchuk <dmitrystu[at]gmail[dot]com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *   http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define CDC_EP0_SIZE    0x08
#define CDC_RXD_EP      0x01
#define CDC_TXD_EP      0x82
#define CDC_DATA_SZ     0x40
#define CDC_NTF_EP      0x83
#define CDC_NTF_SZ      0x08
#define CDC_LOOPBACK

#define CDC_USE_IRQ   /* uncomment to build interrupt-based demo */

struct cdc_config {
    struct usb_config_descriptor        config;
    struct usb_interface_descriptor     comm;
    struct usb_cdc_header_desc          cdc_hdr;
    struct usb_cdc_call_mgmt_desc       cdc_mgmt;
    struct usb_cdc_acm_desc             cdc_acm;
    struct usb_cdc_union_desc           cdc_union;
    struct usb_endpoint_descriptor      comm_ep;
    struct usb_interface_descriptor     data;
    struct usb_endpoint_descriptor      data_eprx;
    struct usb_endpoint_descriptor      data_eptx;
} __attribute__((packed));

static const struct usb_device_descriptor device_desc = {
    .bLength            = sizeof(struct usb_device_descriptor),
    .bDescriptorType    = USB_DTYPE_DEVICE,
    .bcdUSB             = VERSION_BCD(2,0,0),
    .bDeviceClass       = USB_CLASS_CDC,
    .bDeviceSubClass    = USB_SUBCLASS_NONE,
    .bDeviceProtocol    = USB_PROTO_NONE,
    .bMaxPacketSize0    = CDC_EP0_SIZE,
    .idVendor           = 0x0483,
    .idProduct          = 0x5740,
    .bcdDevice          = VERSION_BCD(1,0,0),
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = INTSERIALNO_DESCRIPTOR,
    .bNumConfigurations = 1,
};

static const struct cdc_config config_desc = {
    .config = {
        .bLength                = sizeof(struct usb_config_descriptor),
        .bDescriptorType        = USB_DTYPE_CONFIGURATION,
        .wTotalLength           = sizeof(struct cdc_config),
        .bNumInterfaces         = 2,
        .bConfigurationValue    = 1,
        .iConfiguration         = NO_DESCRIPTOR,
        .bmAttributes           = USB_CFG_ATTR_RESERVED | USB_CFG_ATTR_SELFPOWERED,
        .bMaxPower              = USB_CFG_POWER_MA(100),
    },
    .comm = {
        .bLength                = sizeof(struct usb_interface_descriptor),
        .bDescriptorType        = USB_DTYPE_INTERFACE,
        .bInterfaceNumber       = 0,
        .bAlternateSetting      = 0,
        .bNumEndpoints          = 1,
        .bInterfaceClass        = USB_CLASS_CDC,
        .bInterfaceSubClass     = USB_CDC_SUBCLASS_ACM,
        .bInterfaceProtocol     = USB_CDC_PROTO_V25TER,
        .iInterface             = NO_DESCRIPTOR,
    },
    .cdc_hdr = {
        .bFunctionLength        = sizeof(struct usb_cdc_header_desc),
        .bDescriptorType        = USB_DTYPE_CS_INTERFACE,
        .bDescriptorSubType     = USB_DTYPE_CDC_HEADER,
        .bcdCDC                 = VERSION_BCD(1,1,0),
    },
    .cdc_mgmt = {
        .bFunctionLength        = sizeof(struct usb_cdc_call_mgmt_desc),
        .bDescriptorType        = USB_DTYPE_CS_INTERFACE,
        .bDescriptorSubType     = USB_DTYPE_CDC_CALL_MANAGEMENT,
        .bmCapabilities         = 0,
        .bDataInterface         = 1,

    },
    .cdc_acm = {
        .bFunctionLength        = sizeof(struct usb_cdc_acm_desc),
        .bDescriptorType        = USB_DTYPE_CS_INTERFACE,
        .bDescriptorSubType     = USB_DTYPE_CDC_ACM,
        .bmCapabilities         = 0,
    },
    .cdc_union = {
        .bFunctionLength        = sizeof(struct usb_cdc_union_desc),
        .bDescriptorType        = USB_DTYPE_CS_INTERFACE,
        .bDescriptorSubType     = USB_DTYPE_CDC_UNION,
        .bMasterInterface0      = 0,
        .bSlaveInterface0       = 1,
    },
    .comm_ep = {
        .bLength                = sizeof(struct usb_endpoint_descriptor),
        .bDescriptorType        = USB_DTYPE_ENDPOINT,
        .bEndpointAddress       = CDC_NTF_EP,
        .bmAttributes           = USB_EPTYPE_INTERRUPT,
        .wMaxPacketSize         = CDC_NTF_SZ,
        .bInterval              = 0xFF,
    },
    .data = {
        .bLength                = sizeof(struct usb_interface_descriptor),
        .bDescriptorType        = USB_DTYPE_INTERFACE,
        .bInterfaceNumber       = 1,
        .bAlternateSetting      = 0,
        .bNumEndpoints          = 2,
        .bInterfaceClass        = USB_CLASS_CDC_DATA,
        .bInterfaceSubClass     = USB_SUBCLASS_NONE,
        .bInterfaceProtocol     = USB_PROTO_NONE,
        .iInterface             = NO_DESCRIPTOR,
    },
    .data_eprx = {
        .bLength                = sizeof(struct usb_endpoint_descriptor),
        .bDescriptorType        = USB_DTYPE_ENDPOINT,
        .bEndpointAddress       = CDC_RXD_EP,
        .bmAttributes           = USB_EPTYPE_BULK,
        .wMaxPacketSize         = CDC_DATA_SZ,
        .bInterval              = 0x01,
    },
    .data_eptx = {
        .bLength                = sizeof(struct usb_endpoint_descriptor),
        .bDescriptorType        = USB_DTYPE_ENDPOINT,
        .bEndpointAddress       = CDC_TXD_EP,
        .bmAttributes           = USB_EPTYPE_BULK,
        .wMaxPacketSize         = CDC_DATA_SZ,
        .bInterval              = 0x01,
    },
};

static const struct usb_string_descriptor lang_desc     = USB_ARRAY_DESC(USB_LANGID_ENG_US);
static const struct usb_string_descriptor manuf_desc_en = USB_STRING_DESC("Open source USB stack for STM32");
static const struct usb_string_descriptor prod_desc_en  = USB_STRING_DESC("CDC Loopback demo");
static const struct usb_string_descriptor *const dtable[] = {
    &lang_desc,
    &manuf_desc_en,
    &prod_desc_en,
};

usbd_device udev;
uint32_t	ubuf[0x20];
uint8_t     fifo[0x200];
uint32_t    fpos = 0;

static struct usb_cdc_line_coding cdc_line = {
    .dwDTERate          = 38400,
    .bCharFormat        = USB_CDC_1_STOP_BITS,
    .bParityType        = USB_CDC_NO_PARITY,
    .bDataBits          = 8,
};

static usbd_respond cdc_getdesc (usbd_ctlreq *req, void **address, uint16_t *length) {
    const uint8_t dtype = req->wValue >> 8;
    const uint8_t dnumber = req->wValue & 0xFF;
    const void* desc;
    uint16_t len = 0;
    switch (dtype) {
    case USB_DTYPE_DEVICE:
        desc = &device_desc;
        break;
    case USB_DTYPE_CONFIGURATION:
        desc = &config_desc;
        len = sizeof(config_desc);
        break;
    case USB_DTYPE_STRING:
        if (dnumber < 3) {
            desc = dtable[dnumber];
        } else {
            return usbd_fail;
        }
        break;
    default:
        return usbd_fail;
    }
    if (len == 0) {
        len = ((struct usb_header_descriptor*)desc)->bLength;
    }
    *address = (void*)desc;
    *length = len;
    return usbd_ack;
};


static usbd_respond cdc_control(usbd_device *dev, usbd_ctlreq *req, usbd_rqc_callback *callback) {
    if (((USB_REQ_RECIPIENT | USB_REQ_TYPE) & req->bmRequestType) != (USB_REQ_INTERFACE | USB_REQ_CLASS)) return usbd_fail;
    switch (req->bRequest) {
    case USB_CDC_SET_CONTROL_LINE_STATE:
        return usbd_ack;
    case USB_CDC_SET_LINE_CODING:
        memmove( req->data, &cdc_line, sizeof(cdc_line));
        return usbd_ack;
    case USB_CDC_GET_LINE_CODING:
        dev->status.data_ptr = &cdc_line;
        dev->status.data_count = sizeof(cdc_line);
        return usbd_ack;
    default:
        return usbd_fail;
    }
}


static void cdc_rxonly (usbd_device *dev, uint8_t event, uint8_t ep) {
   usbd_ep_read(dev, ep, fifo, CDC_DATA_SZ);
}

static void cdc_txonly(usbd_device *dev, uint8_t event, uint8_t ep) {
    uint8_t _t = dev->driver->frame_no();
    memset(fifo, _t, CDC_DATA_SZ);
    usbd_ep_write(dev, ep, fifo, CDC_DATA_SZ);
}

static void cdc_loopback(usbd_device *dev, uint8_t event, uint8_t ep) {
    int bytes_read;
	EndPointBuffer ep_tx_buff;
	static char at_cmd[32];
	static char data_in[64];
	static int at_cmd_index = 0;

    switch (event) {
    case usbd_evt_eptx:
		if (RingBufferRead(&ep_circ_buff, &ep_tx_buff) != -1)
		{
			usbd_ep_write(dev, CDC_TXD_EP, ep_tx_buff.data, ep_tx_buff.size);
		}
		else
		{
			usbd_ep_write(dev, CDC_TXD_EP, "\n", 0);
		}

    case usbd_evt_eprx:
		bytes_read = usbd_ep_read(dev, CDC_RXD_EP, data_in, CDC_DATA_SZ);
		for (int i = 0; i < bytes_read; i++)
		{
			//GPIOC->BSRR = GPIO_BSRR_BR13; //PC13 = 0 (Led ON)
			if (data_in[i] == '\n' || data_in[i] == '\r')
			{
				at_cmd[at_cmd_index] = 0;
				at_cmd_index = 0;
				sample_rate = atoi(at_cmd);
				if (sample_rate >= 1000 && sample_rate <= 48000)
				{
					TIM3->PSC = (48000000 / (2 * sample_rate)) - 1;
					GPIOC->BSRR = GPIO_BSRR_BR13; //PC13 = 0 (Led ON)
				}
			}
			else
			{
				at_cmd[at_cmd_index] = data_in[i];
				at_cmd_index++;
			}
		}

    default:
        break;
    }
}

static usbd_respond cdc_setconf (usbd_device *dev, uint8_t cfg) {
    switch (cfg) {
    case 0:
        /* deconfiguring device */
        usbd_ep_deconfig(dev, CDC_NTF_EP);
        usbd_ep_deconfig(dev, CDC_TXD_EP);
        usbd_ep_deconfig(dev, CDC_RXD_EP);
        usbd_reg_endpoint(dev, CDC_RXD_EP, 0);
        usbd_reg_endpoint(dev, CDC_TXD_EP, 0);
        return usbd_ack;
    case 1:
        /* configuring device */
        usbd_ep_config(dev, CDC_RXD_EP, USB_EPTYPE_BULK | USB_EPTYPE_DBLBUF, CDC_DATA_SZ);
        usbd_ep_config(dev, CDC_TXD_EP, USB_EPTYPE_BULK | USB_EPTYPE_DBLBUF, CDC_DATA_SZ);
        usbd_ep_config(dev, CDC_NTF_EP, USB_EPTYPE_INTERRUPT, CDC_NTF_SZ);
#if defined(CDC_LOOPBACK)
        usbd_reg_endpoint(dev, CDC_RXD_EP, cdc_loopback);
        usbd_reg_endpoint(dev, CDC_TXD_EP, cdc_loopback);
#else
        usbd_reg_endpoint(dev, CDC_RXD_EP, cdc_rxonly);
        usbd_reg_endpoint(dev, CDC_TXD_EP, cdc_txonly);
#endif
        usbd_ep_write(dev, CDC_TXD_EP, 0, 0);
        return usbd_ack;
    default:
        return usbd_fail;
    }
}

static void cdc_init_usbd(void)
{
    usbd_init(&udev, &usbd_hw, CDC_EP0_SIZE, ubuf, sizeof(ubuf));
    usbd_reg_config(&udev, cdc_setconf);
    usbd_reg_control(&udev, cdc_control);
    usbd_reg_descr(&udev, cdc_getdesc);
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    usbd_poll(&udev);
}

/*
 * Delay function:
 *
 * Uses the SysTick timer to wait for an arbitrary time in
 * microseconds.
 *
 * The clock source is assumed to be the PLL @ 48MHz divided by 8
 * (6MHz)
 */
void delay_us(unsigned int time)
{
	/*
	 * Load the delay period in microseconds
	 * assuming a 1MHz source
	 */
	SysTick->LOAD = time*6;

	/*
	 * Clears the current value and the count flag
	 */
	SysTick->VAL = 0;
	
	/*
	 * Waits until the count ends
	 */
	while(!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
}

void init_adc()
{
	/*
	 * Enable the ADC1 clock
	 */
	RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

	/*
	 * Enable the Analog to Digital
	 * converter
	 */
	ADC1->CR2 = ADC_CR2_ADON;

	/*
	 * Starts the ADC calibration
	 */
	ADC1->CR2 |= ADC_CR2_CAL;

	/*
	 * Waits until the calibration has
	 * finished
	 */
	while(ADC1->CR2 & ADC_CR2_CAL);

	/*
	 * Selects the ADC channel 0
	 */
	ADC1->SQR1 = (1 << ADC_SQR1_L_Pos);
	ADC1->SQR3 = (0 & ADC_SQR3_SQ1_Msk);

	/*
	 * Starts the conversion
	 */
	ADC1->CR2 |= ADC_CR2_ADON;
}

uint16_t read_adc()
{
	uint16_t value = ADC1->DR;

	/*
	 * Starts a new conversion
	 */
	ADC1->CR2 |= ADC_CR2_ADON;

	/*
	 * Returns the acquired value
	 */
	return value;
}

/*
 * Timer 3 interrupt
 */
void TIM3_IRQHandler()
{
	static EndPointBuffer ep_tx_buff = {.size = 0};

	/*
	 * Test for the Update Event
	 * (CNT Reload)
	 */
	if(TIM3->SR & TIM_SR_UIF)
	{
		uint16_t value = read_adc();
		if (value > 4000 || value < 100)
		{
			GPIOC->BSRR = GPIO_BSRR_BR13; //PC13 = 0 (Led ON)
		}
		ep_tx_buff.data[ep_tx_buff.size] = b64_table[value & 0x3F];
		ep_tx_buff.data[ep_tx_buff.size + 1] = b64_table[(value >> 6) & 0x3F];
		ep_tx_buff.size += 2;
		if (ep_tx_buff.size >= 62)
		{
			ep_tx_buff.data[62] = '\n';
			ep_tx_buff.size = 63;
			RingBufferWrite(&ep_circ_buff, &ep_tx_buff);
			ep_tx_buff.size = 0;
		}
		/*
		 * Clears the interrupt event
		 */
		TIM3->SR &= ~(TIM_SR_UIF);
	}
}


int main()
{
	RingBufferInit(&ep_circ_buff, ep_buff, 32);

	/*
	 * Enable all Ports and Alternate Function clocks
	 */
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN |
	    RCC_APB2ENR_IOPCEN | RCC_APB2ENR_IOPDEN | RCC_APB2ENR_AFIOEN;

	/*
	 * Enable the timer3 clock
	 */
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
	
	/*
	 * Disable JTAG and SWO (Free PB3, PB4 and PA15)
	 */
	AFIO->MAPR = AFIO_MAPR_SWJ_CFG_JTAGDISABLE;

	/*
	 * Enable the SysTick Timer with
	 * the CPU clock divided by 8
	 */
	SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;

	/*
	 * Configure the CPU clock to 48MHz
	 */
	ClockCFG();

	init_adc();

	/*
	 * Enable the PC13 as a digital output
	 */
	GPIOC->CRH = 0x00200000;

	/*
	 * 1000 preescaler (interrupt 48000 times per second)
	 */
	TIM3->PSC = 499;

	/*
	 * Enable the Update
	 */
	TIM3->DIER |= TIM_DIER_UIE;

	/*
	 * The timer 3 will interrupt after
	 * 1ms to start play the music
	 */
	TIM3->ARR = 1;

	/*
	 * Enable the timer 3 interrupt
	 */
	NVIC_EnableIRQ(TIM3_IRQn);
	NVIC_SetPriority(TIM3_IRQn, 0);
	__enable_irq();

	/*
	 * Enable timer 3 counter
	 */
	TIM3->CR1 = TIM_CR1_CEN;

	/*
	 * Initialize the USB stack
	 */
	cdc_init_usbd();
	NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
	NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 2);
    usbd_enable(&udev, true);
    usbd_connect(&udev, true);

	/*
	 * Infinite loop
	 */
	while(1)
	{
		GPIOC->BSRR = GPIO_BSRR_BS13; //PC13 = 1 (Led OFF)
		delay_us(200000); //500ms delay
		/* GPIOC->BSRR = GPIO_BSRR_BR13; //PC13 = 0 (Led ON) */
		/* delay_us(500000); //500ms delay */
	}
}
