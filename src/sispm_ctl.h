/*
  SisPM_ctl.h
 
  Controls the GEMBIRD Silver Shield PM USB outlet device
 
  (C) 2004,2005,2006 Mondrian Nuessle, Computer Architecture Group, University of Mannheim, Germany
  (C) 2005, Andreas Neuper, Germany
  (C) 2010, Olivier Matheret, France, for the plannification part

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


  nuessle@uni-mannheim.de
  aneuper@web.de

*/

#ifndef SISPM_CTL_H
#define SISPM_CTL_H

#define MAXGEMBIRD			 32

#define VENDOR_ID 			 0x04B4

/* USB Product IDs for different sis-pm devices*/
#define PRODUCT_ID_SISPM		 0xFD11
#define PRODUCT_ID_MSISPM_OLD		 0xFD10
#define PRODUCT_ID_MSISPM_FLASH		 0xFD12
#define PRODUCT_ID_SISPM_FLASH_NEW		 0xFD13



#define USB_DIR_IN                       0x80            /* to host */
#define USB_DIR_OUT                      0               /* to device */
#define cpu_to_le16(a)                   (a)

typedef unsigned long ulong;
struct plannifAction {
	ulong switchOn;      // action to do now
	ulong timeForNext;  // wait this num of minutes before any next action ; 0 means "stop"
};

struct plannif {
	int socket;
	ulong timeStamp;
	struct plannifAction actions[17]; // 16 + the initial one
};
#define READNEXTBYTE {nextWord = (ulong)buffer[bufindex]; bufindex ++;}
#define READWORD(idx) {nextWord = (ulong)buffer[idx] + (buffer[idx+1]<<8);}
#define READNEXTWORD {nextWord = (ulong)buffer[bufindex] + (buffer[bufindex+1]<<8); bufindex += 2;}
#define READNEXTDOUBLEWORD {nextWord = (ulong)buffer[bufindex] + (buffer[bufindex+1]<<8) + (buffer[bufindex+2]<<16) + (buffer[bufindex+3]<<24); bufindex += 4;}

#define CHECK(idx, size) if (idx>0x27-(size-1)-2) {printf ("Error : too many planification items, or combined with large time intervals\n"); exit(2);} // avoid writing outside the buffer, or even in the last 2 bytes
#define WRITENEXTBYTE { CHECK(bufindex, 1); buffer[bufindex]=nextWord & 0xFF; bufindex ++;}
#define WRITENEXTWORD { CHECK(bufindex, 2); buffer[bufindex+1]=(nextWord>>8) & 0xFF ; buffer[bufindex]=nextWord & 0xFF; bufindex += 2;}
#define WRITENEXTDOUBLEWORD { CHECK(bufindex, 4); buffer[bufindex+3]=(nextWord>>24) & 0xFF ; buffer[bufindex+2]=(nextWord>>16) & 0xFF ; buffer[bufindex+1]=(nextWord>>8) & 0xFF ; buffer[bufindex]=nextWord & 0xFF; bufindex += 4;}
#define WRITEWORD(idx) { buffer[idx+1]=(nextWord>>8) & 0xFF ; buffer[idx]=nextWord & 0xFF;}

#define REVERTNEXTWORD {bufindex -= 2;}

#define STRINGIFY(x) #x
#define DEBUGVAR(var) printf("value of "STRINGIFY(var)" is %li\n", var);
void plannif_reset (struct plannif* plan);
void usb_command_getplannif(libusb_device_handle *udev, int socket, struct plannif* plan);
void usb_command_setplannif(libusb_device_handle *udev, struct plannif* plan);
void plannif_display(const struct plannif* plan, int verbose, const char* progname);

libusb_device_handle*get_handle(struct libusb_device*dev);
int usb_command(libusb_device_handle *udev, int b1, int b2, int return_value_expected );

#define sispm_buzzer_on(udev)		usb_command( udev, 0x02,        0x00, 0 )
#define sispm_buzzer_off(udev)		usb_command( udev, 0x02,        0x04, 0 ) 

int get_id( struct libusb_device* dev);
char* get_serial(libusb_device_handle *udev);
int sispm_switch_on(libusb_device_handle * udev,int id, int outlet);
int sispm_switch_off(libusb_device_handle * udev,int id, int outlet);
int sispm_switch_getstatus(libusb_device_handle * udev,int id, int outlet);
int sispm_get_power_supply_status(libusb_device_handle * udev,int id, int outlet);
int check_outlet_number(int id, int outlet);
int sispm_switch_toggle(libusb_device_handle * udev,int id, int outlet);
#endif

