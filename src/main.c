/*
  SisPM.c

  Controls the GEMBIRD Silver Shield PM USB outlet device

  (C) 2004-2011, Mondrian Nuessle, Computer Architecture Group, University of Mannheim, Germany
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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#define __USE_XOPEN
#include <time.h>
#include <signal.h>
#include <libusb.h>
#include <assert.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <fcntl.h>

#include "sispm_ctl.h"
#include "socket.h"
#include "main.h"
#include "config.h"

#ifndef MSG_NOSIGNAL
#include <signal.h>
#endif

#define	BSIZE			 	 65536

char* homedir=0;
extern int errno;
int debug=1;
int verbose=1;


#ifndef WEBLESS
void daemonize()
{
  /* Our process ID and Session ID */
  pid_t pid, sid;

  /* Fork off the parent process */
  pid = fork();
  if (pid < 0)
    exit(EXIT_FAILURE);

  /* If we got a good PID, then
     we can exit the parent process. */
  if (pid > 0)
    exit(EXIT_SUCCESS);

  /* Change the file mode mask */
  umask(0);

  /* Create a new SID for the child process */
  sid = setsid();
  if (sid < 0)
    /* Log the failure */
    exit(EXIT_FAILURE);

  /* Change the current working directory */
  if ((chdir("/var/tmp")) < 0)
    /* Log the failure */
    exit(EXIT_FAILURE);

  /* Close out the standard file descriptors */
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
close(STDERR_FILENO);
}
#endif

#ifndef WEBLESS
void process(int out ,char *request, struct libusb_device *dev, int devnum)
{
  char xbuffer[BSIZE+2];
  char filename[1024];
  char *ptr = NULL;
  FILE *in = NULL;
  long length = 0;
  long lastpos = 0;
  long remlen = 0;
  libusb_device_handle *udev;
  unsigned int id; //product id of current device
  char *retvalue = NULL;

  if (debug)
    fprintf(stderr,"\nRequested is (%s)\n",request);

  // extract the filename
  if (strchr(request,'\n') != NULL) {
    memset(filename, 0, 1023);
    strncpy(filename, strchr(request,' ')+1, strchr(request,'\n')-request);
    ptr = strchr(filename, ' ');
    if (ptr != NULL)
      *ptr = 0;
  }

  // avoid to read other directories, %-codes are not evalutated
  ptr = strrchr(filename,'/');
  if (ptr != NULL)
    ptr++;
  else
    ptr = filename;

  if (strlen(ptr) == 0)
    ptr="index.html";

  if (debug) {
    fprintf(stderr,"\nrequested filename(%s)\n", filename);
    fprintf(stderr,"resulting filename(%s)\n", ptr);
    fprintf(stderr,"change directory to (%s)\n", homedir);
  }

  if (chdir(homedir) != 0) {
    sprintf(xbuffer, "HTTP/1.1 4%02d Bad request\nServer: SisPM\nContent-Type: text/html\n\n"
            "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n<html><head>\n<title>4%02d Bad Defaults</title>\n"
            "</head><body>\n<h1>Bad Defaults</h1>\n<p>%s</p></body></html>\n\n",
            errno,errno,strerror(errno));
    send(out,xbuffer,strlen(xbuffer),0);
    return;
  }

  if (debug)
    fprintf(stderr,"\nopen file(%s)\n",ptr);

  in = fopen(ptr,"r");

  if (in == NULL) {
    sprintf(xbuffer, "HTTP/1.1 4%02d Bad request\nServer: SisPM\nContent-Type: text/html\n\n"
            "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n<html><head>\n<title>4%02d Bad Request</title>\n"
            "</head><body>\n<h1>Bad Request</h1>\n<p>%s</p></body></html>\n\n",
            errno,errno,strerror(errno));
    send(out,xbuffer,strlen(xbuffer),0);
    return;
  }

  /* get device-handle/-id */
  udev = get_handle(dev);
  id = get_id(dev);
  if (udev == NULL)
    fprintf(stderr, "No access to Gembird #%d USB device\n", devnum);
  else
    if (verbose)
      printf("Accessing Gembird #%d USB device\n", devnum);

  lastpos = ftell(in);
  retvalue = fgets(xbuffer, BSIZE-1, in);
  assert((retvalue != NULL) || feof(in));
  remlen = length = ftell(in) - lastpos;
  lastpos = ftell(in);

  while (!feof(in)) {
    char *mrk = xbuffer;
    char *ptr = xbuffer;
    /* search for:
     *	$$exec(0)?.1.:.2.$$	to execute command(#)
     *	$$stat(2)?.1.:.2.$$	to evaluate status(#)
     */
    for (mrk = ptr = xbuffer; (ptr-xbuffer) < length; ptr++) {
      if (*ptr=='$' && ptr[1]=='$' && (ptr[2]=='e' || ptr[2]=='s'|| ptr[2]=='o')) {
        /*
         * $$exec(1)?select:forget$$
         *   ^cmd    ^pos	 ^trm
         * ^ptr	 ^num	   ^neg
         */
        char *cmd=&ptr[2];
        char *num=strchr(cmd,'(');
        char *pos=strchr(num?num:cmd,'?');
        char *neg=strchr(pos?pos:cmd,':');
        char *trm=strchr(neg?neg:cmd,'$');
        if (debug) {
          fprintf(stderr,"%p\n%p\n%p\n%p\n%p\n%p\n",cmd,num,pos,neg,trm,ptr);
          fprintf(stderr,"%s%s%s%s%s%s",cmd,num,pos,neg,trm,ptr);
        }

	if (trm != NULL) {
          assert(("Command-Format: $$exec(#)?select:forget$$	ERROR at #",num!=NULL));
          assert(("Command-Format: $$exec(#)?select:forget$$	ERROR at ?",pos!=NULL));
          assert(("Command-Format: $$exec(#)?select:forget$$	ERROR at :",neg!=NULL));
          // if( (ptr=strchr(neg,'$')) == NULL ) ptr=cmd; else *ptr=0;
          // *pos=*neg=0;
          num++; pos++; neg++;
          send(out,mrk,ptr-mrk,0);
          remlen = remlen - (ptr - mrk);
          mrk=ptr;

          if (strncasecmp(cmd,"on(",3)==0) {
            if (debug)
              fprintf(stderr,"\nON(%s)\n",num);
            if (sispm_switch_on(udev,id,atoi(num)) !=0)
              send(out,pos,neg-pos-1,0);
            else
              send(out,neg,trm-neg,0);
          }
          else
            if (strncasecmp(cmd,"off(",4)==0) {
              assert(("Command-Format: $$exec(#)?select:forget$$	ERROR at final $",(trm[1]=='$')));
              if (debug)
                fprintf(stderr,"\nOFF(%s)\n",num);
              if (sispm_switch_off(udev,id,atoi(num)) !=0)
                send(out,pos,neg-pos-1,0);
              else
                send(out,neg,trm-neg,0);
            }
            else
              if (strncasecmp(cmd,"toggle(",7)==0) {
                assert(("Command-Format: $$exec(#)?select:forget$$	ERROR at final $",(trm[1]=='$')));
                if (debug)
                  fprintf(stderr,"\nTOGGLE(%s)\n",num);
                if (sispm_switch_getstatus(udev,id,atoi(num)) == 0) {
                  sispm_switch_on(udev,id,atoi(num));
                  send(out,pos,neg-pos-1,0);
                }
                else {
                  sispm_switch_off(udev,id,atoi(num));
                  send(out,neg,trm-neg,0);
                }
              }
              else
                if (strncasecmp(cmd,"status(",7)==0) {
                  assert(("Command-Format: $$exec(#)?select:forget$$	ERROR at final $",(trm[1]=='$')));
                  if (debug)
                    fprintf(stderr,"\nSTATUS(%s)\n",num);
                  if (sispm_switch_getstatus(udev,id,atoi(num)) != 0)
                    send(out,pos,neg-pos-1,0);
                  else
                    send(out,neg,trm-neg,0);
                }
                else {
                  // assert(("unknown but terminated command sequence",(trm[1]=='$')));
                  //fprintf(stderr,"\n<UNDERLINE>undefined sequence <B>%s</B></UNDERLINE>\n",cmd);
                  send(out,"$$",2,0);
                }
          remlen=remlen-(2+trm-mrk);
          mrk=ptr=&trm[2];
        }
      }
    }
    send(out, mrk, remlen, 0);
    memset(xbuffer, 0, BSIZE);
    retvalue = fgets(xbuffer, BSIZE-1, in);
    assert((retvalue != NULL) || feof(in));
    remlen = length = ftell(in) - lastpos;
    lastpos = ftell(in);
  }

  if (udev != NULL) {
    libusb_close(udev);
    udev = NULL;
  }
  fclose(in);
  return;
}
#endif

void print_disclaimer(char*name)
{
  fprintf(stderr, "\nSiS PM Control for Linux 3.1\n\n"
	 "(C) 2004-2011 by Mondrian Nuessle, (C) 2005, 2006 by Andreas Neuper.\n"
	 "(C) 2010 by Olivier Matheret for the plannification part\n"
	 "This program is free software.\n"
	 "%s comes with ABSOLUTELY NO WARRANTY; for details \n"
	 "see the file INSTALL. This is free software, and you are welcome\n"
	 "to redistribute it under certain conditions; see the file INSTALL\n"
	 "for details.\n\n", name );
  return;
}

void print_usage(char* name)
{
  print_disclaimer(name);
  fprintf(stderr,"\n"
          "sispmctl -s\n"
          "sispmctl [-q] [-n] [-d 0...] [-D ...] -b <on|off>\n"
          "sispmctl [-q] [-n] [-d 0...] [-D ...] -[o|f|t|g|m] 1..4|all\n"
          "sispmctl [-q] [-n] [-d 0...] [-D ...] -[a|A] 1..4|all [--Aat '...'] [--Aafter ...] [--Ado <on|off>] ... [--Aloop ...]\n"
          "   'v'   - print version & copyright\n"
          "   'h'   - print this usage information\n"
          "   's'   - scan for supported GEMBIRD devices\n"
          "   'b'   - switch buzzer on or off\n"
          "   'o'   - switch outlet(s) on\n"
          "   'f'   - switch outlet(s) off\n"
          "   't'   - toggle outlet(s) on/off\n"
          "   'g'   - get status of outlet(s)\n"
          "   'm'   - get power supply status outlet(s) on/off\n"
          "   'd'   - apply to device 'n'\n"
          "   'D'   - apply to device with given serial number\n"
          "   'n'   - show result numerically\n"
          "   'q'   - quiet mode, no explanations - but errors\n"
          "   'a'   - get plannification for outlet\n"
          "   'A'   - set plannification for outlet\n"
          "           '-A<num>'        - select outlet\n"
          "           '--Aat \"date\"'   - sets an event time as a date '%%Y-%%m-%%d %%H:%%M'\n"
          "           '--Aafter N'     - sets an event time as N minutes after the previous one\n"
          "           '--Ado <on|off>' - sets the current event's action\n"
          "           '--Aloop N'      - loops to 1st event's action after N minutes\n\n"
#ifndef WEBLESS
          "Webinterface features:\n"
          "sispmctl [-q] [-i <ip>] [-p <#port>] [-u <path>] -l|L\n"
          "   'l'   - start port listener\n"
          "   'L'   - same as 'l', but stay in foreground\n"
          "   'i'   - bind socket on interface with given IP (dotted decimal, i.e. 192.168.1.1)\n"
          "   'p'   - port number for listener (%d)\n"
          "   'u'   - repository for web pages (default=%s)\n\n"
	  ,listenport, homedir
#endif
          );

#ifdef WEBLESS
  fprintf(stderr,"Note: This build was compiled without web-interface features.\n\n");
#endif
}

#ifndef WEBLESS
const char *show_header(int type)
{
  char *content = NULL;
  static char buffer[1024];
  time_t now;
  int http = 100 * (type>>4);

  switch(type&0xf) {
    case 1: content="\nContent-Type: text/html"; break;
    case 2: content="\nContent-Type: text/plain"; break;
    case 3: content="\nContent-Type: image/gif"; break;
    case 4: content="\nContent-Type: image/jpg"; break;
    default: content=""; break;
  }

  now = time(NULL);
  sprintf(buffer, "HTTP/1.0 %d Answer\nServer: sispm\nConnection: close%s\nDate: %s\n\n", http, content, ctime(&now));

  return(buffer);
}



const char *answer(char*in)
{
  char *ptr = NULL, *end = NULL;
  int type = 0;

  static char out[MAXANSWER+2];
  memset(out,0,MAXANSWER);

  if (strncasecmp("GET ",in,4) == 0) {
    type = 1;
    ptr = &in[4];
  } else
    if (strncasecmp("POST ",in,5) == 0) {
      type = 1;
      ptr = &in[5];
    } else {
      type = 1;
      ptr = &in[0];
    }

  end = strchr(ptr,' ');
  assert((end-ptr<1024,"filename buffer is defined to 1024 chars only"));
  if (strncasecmp("/switch",ptr,6) == 0)
    ptr += 6;

  strcpy(out,show_header((4<<4)+1));

  strcat(out,"\n\n<HTML><HEAD><TITLE>TEST</TITLE></HEAD><BODY><H1>TEST</H1></BODY></HTML>\n");
  strcat(out,show_header((4<<4)+1));
  return(out);
}
#endif

void parse_command_line(int argc, char* argv[], int count, struct libusb_device*dev[], char *usbdevsn[])
{
  int numeric=0;
  int c;
  int i,j;
  int result;
  int from=1, upto=4;
  int status;
  int devnum=0;
  libusb_device_handle *udev=NULL;
  libusb_device_handle *sudev=NULL; //scan device
  unsigned int id=0; //product id of current device
  char* onoff[] = { "off", "on", "0", "1" };
#ifndef WEBLESS
  char* bindaddr=0;
#endif
  unsigned int outlet;
  struct plannif plan;
  plannif_reset(&plan);

#ifdef BINDADDR
  if (strlen(BINDADDR) != 0)
    bindaddr=BINDADDR;
#endif

  while( (c=getopt(argc, argv,"i:o:f:t:a:A:b:g:m:lLqvhnsd:D:u:p:")) != -1 )
  {
    if ((c != 'h') && (c != 'v') && (count == 0)) {
      fprintf(stderr, "No GEMBIRD SiS-PM found. Check USB connections, please!\n");
      if (udev != NULL) {
        libusb_close(udev);
        udev = NULL;
      }
      exit(1);
    }
    if( c=='o' || c=='f' || c=='g' || c=='t' || c=='a' || c=='A' || c=='m')
    {
      if((strncmp(optarg,"all",strlen("all"))==0)
         || (atoi(optarg)==7) )
      { //use all outlets
        from=1;
        upto=4;
      } else
      {
		    from = upto = atoi(optarg);
      }
      if (from<1 || upto>4)
      {
        fprintf(stderr,"Invalid outlet number given: %s\n"
          "Expected: 1, 2, 3, 4, or all.\nTerminating.\n",optarg);
        print_disclaimer( argv[0] );
        exit(-6);
      }
    }
    else
    {
      from = upto = 0;
    }
    if( c=='o' || c=='f' || c=='g' || c=='b' || c=='t' || c=='a' || c=='A' || c=='m') //we need a device handle for these commands
    {
      /* get device-handle/-id if it wasn't done already */
      if(udev==NULL){
        udev = get_handle(dev[devnum]);
        id = get_id(dev[devnum]);
        if(udev==NULL)
          fprintf(stderr, "No access to Gembird #%d USB device\n",
            devnum);
        else
          if(verbose) printf("Accessing Gembird #%d USB device\n",
		       devnum);
      }
    }

#ifdef WEBLESS
    if (c=='l' || c=='L' || c=='i' || c=='p' || c=='u' )
    {
      fprintf(stderr,"Application was compiled without web-interface. Feature not available.\n");
      exit(-100);
    }
#endif

    // using first available device
    if (c != 'h' && c != 'v') {
      id = get_id(dev[devnum]);
      if (((id == PRODUCT_ID_MSISPM_OLD) || (id == PRODUCT_ID_MSISPM_FLASH)) && (from != upto))
        from = upto = 1;
    }
    for (i=from; i <= upto; i++) {
      switch(c) {
        case 's':
          for (status=0; status<count; status++) {
            if (numeric == 0)
              printf("Gembird #%d\nUSB information:\n", status);
            else
              printf("%d\n", status);
            id = get_id(dev[status]);
            if ((id == PRODUCT_ID_SISPM) || (id == PRODUCT_ID_SISPM_FLASH_NEW))
              if (numeric == 0)
                printf("device type:      4-socket SiS-PM\n");
              else
                printf("4\n");
            else
              if (numeric == 0)
                printf("device type:      1-socket mSiS-PM.\n");
              else
                printf("1\n");
            sudev = get_handle(dev[status]);
            id = get_id(dev[status]);
            if(sudev==NULL)
              fprintf(stderr, "No access to Gembird #%d USB device\n",
                      status);

            if (numeric == 0)
              printf("serial number:    %s\n",get_serial(sudev));
            else
              printf("%s\n",get_serial(sudev));
            libusb_close(sudev);
            sudev = NULL;
            printf("\n");
          }
          break;
      // select device...
      // replace previous (first is default) device by selected one
      case 'd': // by id
        if (udev != NULL) {
          libusb_close(udev);
          udev = NULL;
        }
        devnum = atoi(optarg);
        if ((devnum < 0) || (devnum >= count)) {
          fprintf(stderr, "Invalid number or given device not found.\nTerminating\n");
          if (udev != NULL) {
            libusb_close(udev);
            udev = NULL;
          }
          exit(-8);
        }
        break;
      case 'D': // by serial number
        for (j=0; j < count; j++) {
          if (debug)
            fprintf(stderr, "now comparing %s and %s\n", usbdevsn[j], optarg);
          if (strcasecmp(usbdevsn[j], optarg) == 0) {
            if (udev != NULL) {
              libusb_close(udev);
              udev = NULL;
            }
            devnum = j;
            break;
          }
        }
        if (devnum != j) {
          fprintf(stderr, "No device with serial number %s found.\nTerminating\n",optarg);
          if (udev != NULL) {
            libusb_close(udev);
            udev = NULL;
          }
          exit(-8);
        }
        break;
      	case 'o':
          outlet=check_outlet_number(id, i);
          sispm_switch_on(udev,id,outlet);
          if(verbose) printf("Switched outlet %d %s\n",i,onoff[1+numeric]);
          break;
      	case 'f':
	        outlet=check_outlet_number(id, i);
	        sispm_switch_off(udev,id,outlet);
	        if(verbose) printf("Switched outlet %d %s\n",i,onoff[0+numeric]);
	        break;
      	case 't':
	        outlet=check_outlet_number(id, i);
	        result=sispm_switch_toggle(udev,id,outlet);
	        if(verbose) printf("Toggled outlet %d %s\n",i,onoff[result]);
	        break;
      	case 'A':
	      {
          time_t date, lastEventTime;
          struct tm *timeStamp_tm;
          struct plannif plan;
          int opt, lastAction = 0;
          ulong loop = 0;
          int optindsave = optind;
          int actionNo=0;

          outlet=check_outlet_number(id, i);

          time( &date );
					timeStamp_tm = localtime(&date);
          lastEventTime = ((ulong)(date/60))*60; // round to previous minute
          plannif_reset (&plan);
          plan.socket = outlet;
          plan.timeStamp = date;
          plan.actions[0].switchOn = 0;

          const struct option opts[] = {
            { "Ado", 1, NULL, 'd' }, { "Aafter", 1, NULL, 'a' }, { "Aat", 1, NULL, '@' },
            { "Aloop", 1, NULL, 'l' }, { NULL, 0, 0, 0 }
          };

          // scan long options and store in plan+loop variables
          while ((opt = getopt_long(argc, argv, "", opts, NULL)) != EOF) {
            if (opt == 'l') {
              loop = atol(optarg);
              continue;
            }
            if (actionNo+1 >= sizeof(plan.actions)/sizeof(struct plannifAction)) { // last event is reserved for loop or stop
              fprintf(stderr,"Too many plannification events\nTerminating\n");
              exit(-7);
            }
            switch (opt) {
            case 'd':
              plan.actions[actionNo+1].switchOn = (strcmp(optarg, "on") == 0 ? 1 : 0);
              break;
            case 'a':
              plan.actions[actionNo].timeForNext = atol(optarg);
              break;
            case '@':
              {
                struct tm tm;
                time_t time4next;
                bzero (&tm, sizeof(tm));
                tm.tm_isdst = timeStamp_tm->tm_isdst;
                strptime(optarg, "%Y-%m-%d %H:%M", &tm);
                time4next = mktime(&tm);
                if (time4next > lastEventTime)
                  plan.actions[actionNo].timeForNext = (time4next - lastEventTime) / 60;
                else
                  plan.actions[actionNo].timeForNext = 0;
                break;
              }
            case '?':
            default:
                fprintf(stderr,"Unknown Option: %s\nTerminating\n",argv[optind-1]);
                exit(-7);
                break;
            }
            if (plan.actions[actionNo].timeForNext == 0) {
              fprintf(stderr,"Incorrect Date: %s\nTerminating\n",optarg);
              exit(-7);
            }

            if (plan.actions[actionNo].timeForNext != -1 && plan.actions[actionNo+1].switchOn != -1) {
              lastEventTime += 60 * plan.actions[actionNo].timeForNext;
              actionNo++;
            }
          }

          // compute the value to set in the last row, according to loop
          while (plan.actions[lastAction].timeForNext != -1) {
            if (loop && (lastAction > 0)) { // we ignore the first time for the loop calculation
              if (loop <= plan.actions[lastAction].timeForNext) {
                printf ("error : the loop period is too short\n");
                exit(1);
              }
              loop -= plan.actions[lastAction].timeForNext;
            }
            lastAction++;
          }
          if (lastAction >= 1)
            plan.actions[lastAction].timeForNext = loop;

          // let's go, and check
          libusb_command_setplannif(udev, &plan);
	        if(verbose) {
            plannif_reset (&plan);
	          libusb_command_getplannif(udev,outlet,&plan);
	          plannif_display(&plan, 0, NULL);
          }

          if (i<upto)
            optind = optindsave; // reset for next device if needed

          break;
	      }
      	case 'a':
	        outlet=check_outlet_number(id, i);
          struct plannif plan;
          plannif_reset (&plan);
	        libusb_command_getplannif(udev,outlet,&plan);
          plannif_display(&plan, verbose, argv[0]);
	        break;
      	case 'g':
	        outlet=check_outlet_number(id, i);
	        result=sispm_switch_getstatus(udev,id,outlet);
	        if(verbose) printf("Status of outlet %d:\t",i);
	        printf("%s\n",onoff[ result +numeric]);
	        break;
      	case 'm':
    	    outlet=check_outlet_number(id, i);
	        result=sispm_get_power_supply_status(udev,id,outlet);
	        if(verbose) printf("Power supply status is:\t");
	        printf("%s\n",onoff[ result +numeric]); //take bit 1, which gives the relais status
	        break;
#ifndef WEBLESS
  	    case 'p':
	        listenport=atoi(optarg);
	        if(verbose) printf("Server will listen on port %d.\n",listenport);
	        break;
      	case 'u':
	        homedir=strdup(optarg);
	        if(verbose) printf("Web pages come from \"%s\".\n",homedir);
	        break;
            case 'i':
      	    bindaddr=optarg;
	        if (verbose) printf("Web server will bind on interface with IP %s\n",bindaddr);
	        break;
        case 'l':
        case 'L':
          {
                int* s;
                if (verbose)
                  printf("Server goes to listen mode now.\n");
                if ((s = socket_init(bindaddr)) != NULL) {
                  if (c == 'l')
                    daemonize();
                  while(1)
                    l_listen(s,dev[devnum],devnum);
                }
                else
                  exit(EXIT_FAILURE);
            break;
          }
#endif
      	case 'q':
	        verbose=1-verbose;
    	    break;
      	case 'n':
	        numeric=2-numeric;
	        break;
        case 'b':
    	    if (strncmp(optarg,"on",strlen("on"))==0)
          {
                 sispm_buzzer_on(udev);
		        if(verbose) printf("Turned buzzer %s\n",onoff[1+numeric]);
    	    } else
      	    if (strncmp(optarg,"off",strlen("off"))==0)
	          {
	            sispm_buzzer_off(udev);
          		if(verbose) printf("Turned buzzer %s\n",onoff[0+numeric]);
      	    }
    	    break;
        case 'v':
            print_disclaimer( argv[0] );
    	    break;
        case 'h':
            print_usage( argv[0] );
            break;
      	default:
            fprintf(stderr,"Unknown Option: %c(%x)\nTerminating\n",c,c);
            exit(-7);
      }
    } // loop through devices
  } // loop through options

  if (udev!=NULL) {
    libusb_close(udev);
    udev = NULL;
  }
  return;
}


int main(int argc, char** argv)
{
  libusb_context *ctx;
  struct libusb_device *usbdev[MAXGEMBIRD], *usbdevtemp;
  char *usbdevsn[MAXGEMBIRD];
  int count=0, found = 0, i=1;

#ifndef MSG_NOSIGNAL
  (void) signal(SIGPIPE, SIG_IGN);
#endif

  memset(usbdev,0,sizeof(usbdev));

  libusb_init(&ctx);

#ifndef WEBLESS
  if (strlen(WEBDIR)==0)
    homedir=HOMEDIR;
  else
    homedir=WEBDIR;
#endif

  // initialize by setting device pointers to zero
  for (count=0; count < MAXGEMBIRD; count++)
    usbdev[count]=NULL; count=0;

  //first search for GEMBIRD (m)SiS-PM devices
  struct libusb_device **listdev;
  struct libusb_device_descriptor descriptor;
  ssize_t devcount = libusb_get_device_list(ctx, &listdev);
  ssize_t idx;
  
  for(idx = 0; idx < devcount; ++idx) {
    libusb_get_device_descriptor(listdev[idx], &descriptor);
    if ((descriptor.idVendor == VENDOR_ID) && ((descriptor.idProduct == PRODUCT_ID_SISPM) ||
                                              (descriptor.idProduct == PRODUCT_ID_MSISPM_OLD) ||
                                              (descriptor.idProduct == PRODUCT_ID_MSISPM_FLASH) ||
                                              (descriptor.idProduct == PRODUCT_ID_SISPM_FLASH_NEW))) {
      usbdev[count++] = listdev[idx];
    }
    
    if (count == MAXGEMBIRD) {
      fprintf(stderr,"%d devices found. Please recompile if you need to support more devices!\n",count);
      break;
    }
  }

//  /* bubble sort them first, thnx Ingo Flaschenberger */
//  if (count > 1) {
//    do {
//      found = 0;
//      for (i=1; i< count; i++) {
//        if (usbdev[i]->devnum < usbdev[i-1]->devnum) {
//          usbdevtemp = usbdev[i];
//          usbdev[i] = usbdev[i-1];
//          usbdev[i-1] = usbdevtemp;
//          found = 1;
//        }
//      }
//    } while (found != 0);
//  }

  /* get serial number of each device */
  for (i=0; i < count; i++) {
    libusb_device_handle *sudev = NULL;

    sudev = get_handle(usbdev[i]);
    if (sudev == NULL) {
      fprintf(stderr, "No access to Gembird #%d USB device\n", i );
      usbdevsn[i] = malloc(5);
      usbdevsn[i][0] = '#';
      usbdevsn[i][1] = '0'+i;
      usbdevsn[i][2] = '\0';
    }
    else {
      usbdevsn[i] = strdup(get_serial(sudev));
      libusb_close(sudev);
      sudev = NULL;
    }
  }

  /* do the real work here */
  if (argc <= 1)
    print_usage(argv[0]);
  else
    parse_command_line(argc,argv,count,usbdev,usbdevsn);
  
  libusb_exit(ctx);

  return 0;
}
