/*-
 * Copyright (c) 1998 Brian Somers <brian@Awfulhak.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Id: prompt.c,v 1.1.2.28 1998/04/19 15:24:49 brian Exp $
 */

#include <sys/param.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/un.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "defs.h"
#include "timer.h"
#include "command.h"
#include "log.h"
#include "descriptor.h"
#include "prompt.h"
#include "fsm.h"
#include "lcp.h"
#include "auth.h"
#include "iplist.h"
#include "throughput.h"
#include "slcompress.h"
#include "ipcp.h"
#include "filter.h"
#include "lqr.h"
#include "hdlc.h"
#include "async.h"
#include "mbuf.h"
#include "ccp.h"
#include "link.h"
#include "physical.h"
#include "mp.h"
#include "bundle.h"
#include "chat.h"
#include "chap.h"
#include "datalink.h"
#include "server.h"

static void
prompt_Display(struct prompt *p)
{
  static char shostname[MAXHOSTNAMELEN];
  const char *pconnect, *pauth;

  if (p->TermMode || !p->needprompt)
    return;

  p->needprompt = 0;

  if (p->nonewline)
    p->nonewline = 0;
  else
    fprintf(p->Term, "\n");

  if (p->auth == LOCAL_AUTH)
    pauth = " ON ";
  else
    pauth = " on ";

  if (p->bundle->ncp.ipcp.fsm.state == ST_OPENED)
    pconnect = "PPP";
  else if (bundle_Phase(p->bundle) == PHASE_NETWORK)
    pconnect = "PPp";
  else if (bundle_Phase(p->bundle) == PHASE_AUTHENTICATE)
    pconnect = "Ppp";
  else
    pconnect = "ppp";

  if (*shostname == '\0') {
    char *dot;

    if (gethostname(shostname, sizeof shostname))
      strcpy(shostname, "localhost");
    else if ((dot = strchr(shostname, '.')))
      *dot = '\0';
  }

  fprintf(p->Term, "%s%s%s> ", pconnect, pauth, shostname);
  fflush(p->Term);
}

static int
prompt_UpdateSet(struct descriptor *d, fd_set *r, fd_set *w, fd_set *e, int *n)
{
  struct prompt *p = descriptor2prompt(d);
  int sets;

  sets = 0;

  if (!p->active)
    return sets;

  if (p->fd_in >= 0) {
    if (r) {
      FD_SET(p->fd_in, r);
      sets++;
    }
    if (e) {
      FD_SET(p->fd_in, e);
      sets++;
    }
    if (sets && *n < p->fd_in + 1)
      *n = p->fd_in + 1;
  }

  prompt_Display(p);

  return sets;
}

static int
prompt_IsSet(struct descriptor *d, const fd_set *fdset)
{
  struct prompt *p = descriptor2prompt(d);
  return p->fd_in >= 0 && FD_ISSET(p->fd_in, fdset);
}


static void
prompt_ShowHelp(struct prompt *p)
{
  prompt_Printf(p, "The following commands are available:\r\n");
  prompt_Printf(p, " ~p\tEnter Packet mode\r\n");
  prompt_Printf(p, " ~-\tDecrease log level\r\n");
  prompt_Printf(p, " ~+\tIncrease log level\r\n");
  prompt_Printf(p, " ~t\tShow timers\r\n");
  prompt_Printf(p, " ~m\tShow memory map\r\n");
  prompt_Printf(p, " ~.\tTerminate program\r\n");
  prompt_Printf(p, " ~?\tThis help\r\n");
}

static void
prompt_Read(struct descriptor *d, struct bundle *bundle, const fd_set *fdset)
{
  struct prompt *p = descriptor2prompt(d);
  int n;
  char ch;
  static int ttystate;
  char linebuff[LINE_LEN];

  if (p->TermMode == NULL) {
    n = read(p->fd_in, linebuff, sizeof linebuff - 1);
    if (n > 0) {
      if (linebuff[n-1] == '\n')
        linebuff[--n] = '\0';
      else
        linebuff[n] = '\0';
      p->nonewline = 1;		/* Maybe command_Decode does a prompt */
      prompt_Required(p);
      if (n)
        command_Decode(bundle, linebuff, n, p, p->src.from);
    } else if (n <= 0) {
      log_Printf(LogPHASE, "Client connection closed.\n");
      prompt_Destroy(p, 0);
    }
    return;
  }

  switch (p->TermMode->state) {
    case DATALINK_CLOSED:
      prompt_Printf(p, "Link lost, terminal mode.\n");
      prompt_TtyCommandMode(p);
      p->nonewline = 0;
      prompt_Required(p);
      return;

    case DATALINK_READY:
      break;

    case DATALINK_OPEN:
      prompt_Printf(p, "\nPacket mode detected.\n");
      prompt_TtyCommandMode(p);
      p->nonewline = 0;
      /* We'll get a prompt because of our status change */
      /* Fall through */

    default:
      /* Wait 'till we're in a state we care about */
      return;
  }

  /*
   * We are in terminal mode, decode special sequences
   */
  n = read(p->fd_in, &ch, 1);
  log_Printf(LogDEBUG, "Got %d bytes (reading from the terminal)\n", n);

  if (n > 0) {
    switch (ttystate) {
    case 0:
      if (ch == '~')
	ttystate++;
      else
	if (physical_Write(p->TermMode->physical, &ch, n) < 0) {
	  log_Printf(LogERROR, "error writing to modem: %s\n", strerror(errno));
          prompt_TtyCommandMode(p);
        }
      break;
    case 1:
      switch (ch) {
      case '?':
	prompt_ShowHelp(p);
	break;
      case 'p':
        datalink_Up(p->TermMode, 0, 1);
        prompt_Printf(p, "\nPacket mode.\n");
	prompt_TtyCommandMode(p);
        break;
      case '.':
	prompt_TtyCommandMode(p);
        p->nonewline = 0;
        prompt_Required(p);
	break;
      case 't':
	timer_Show(0, p);
	break;
      case 'm':
	mbuf_Show(NULL);
	break;
      default:
	if (physical_Write(p->TermMode->physical, &ch, n) < 0) {
	  log_Printf(LogERROR, "error writing to modem: %s\n", strerror(errno));
          prompt_TtyCommandMode(p);
        }
	break;
      }
      ttystate = 0;
      break;
    }
  }
}

static void
prompt_Write(struct descriptor *d, struct bundle *bundle, const fd_set *fdset)
{
  /* We never want to write here ! */
  log_Printf(LogERROR, "prompt_Write: Internal error: Bad call !\n");
}

struct prompt *
prompt_Create(struct server *s, struct bundle *bundle, int fd)
{
  struct prompt *p = (struct prompt *)malloc(sizeof(struct prompt));

  if (p != NULL) {
    p->desc.type = PROMPT_DESCRIPTOR;
    p->desc.next = NULL;
    p->desc.UpdateSet = prompt_UpdateSet;
    p->desc.IsSet = prompt_IsSet;
    p->desc.Read = prompt_Read;
    p->desc.Write = prompt_Write;

    if (fd == PROMPT_STD) {
      p->fd_in = STDIN_FILENO;
      p->fd_out = STDOUT_FILENO;
      p->Term = stdout;
      p->owner = NULL;
      p->auth = LOCAL_AUTH;
      p->src.type = "Controller";
      strncpy(p->src.from, ttyname(p->fd_out), sizeof p->src.from - 1);
      p->src.from[sizeof p->src.from - 1] = '\0';
      tcgetattr(p->fd_in, &p->oldtio);	/* Save original tty mode */
    } else {
      p->fd_in = p->fd_out = fd;
      p->Term = fdopen(fd, "a+");
      p->owner = s;
      p->auth = *s->passwd ? LOCAL_NO_AUTH : LOCAL_AUTH;
      p->src.type = "unknown";
      *p->src.from = '\0';
    }
    p->TermMode = NULL;
    p->nonewline = 1;
    p->needprompt = 1;
    p->active = 1;
    p->bundle = bundle;
    if (p->bundle)
      bundle_RegisterDescriptor(p->bundle, &p->desc);
    log_RegisterPrompt(p);
    log_DiscardAllLocal(&p->logmask);
  }

  return p;
}

void
prompt_Destroy(struct prompt *p, int verbose)
{
  if (p->Term != stdout) {
    fclose(p->Term);
    close(p->fd_in);
    if (p->fd_out != p->fd_in)
      close(p->fd_out);
    if (verbose)
      log_Printf(LogPHASE, "Client connection dropped.\n");
  } else
    prompt_TtyOldMode(p);

  log_UnRegisterPrompt(p);
  bundle_UnRegisterDescriptor(p->bundle, &p->desc);
  free(p);
}

void
prompt_Printf(struct prompt *p, const char *fmt,...)
{
  if (p && p->active) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(p->Term, fmt, ap);
    fflush(p->Term);
    va_end(ap);
    p->nonewline = 1;
  }
}

void
prompt_vPrintf(struct prompt *p, const char *fmt, va_list ap)
{
  if (p && p->active) {
    vfprintf(p->Term, fmt, ap);
    fflush(p->Term);
    p->nonewline = 1;
  }
}

void
prompt_TtyInit(struct prompt *p)
{
  int stat, fd = p ? p->fd_in : STDIN_FILENO;
  struct termios newtio;

  stat = fcntl(fd, F_GETFL, 0);
  if (stat > 0) {
    stat |= O_NONBLOCK;
    fcntl(fd, F_SETFL, stat);
  }

  if (p)
    newtio = p->oldtio;
  else
    tcgetattr(fd, &newtio);

  newtio.c_lflag &= ~(ECHO | ISIG | ICANON);
  newtio.c_iflag = 0;
  newtio.c_oflag &= ~OPOST;
  newtio.c_cc[VEOF] = _POSIX_VDISABLE;
  if (!p)
    newtio.c_cc[VINTR] = _POSIX_VDISABLE;
  newtio.c_cc[VMIN] = 1;
  newtio.c_cc[VTIME] = 0;
  newtio.c_cflag |= CS8;
  tcsetattr(fd, TCSANOW, &newtio);
  if (p)
    p->comtio = newtio;
}

/*
 *  Set tty into command mode. We allow canonical input and echo processing.
 */
void
prompt_TtyCommandMode(struct prompt *p)
{
  struct termios newtio;
  int stat;

  tcgetattr(p->fd_in, &newtio);
  newtio.c_lflag |= (ECHO | ISIG | ICANON);
  newtio.c_iflag = p->oldtio.c_iflag;
  newtio.c_oflag |= OPOST;
  tcsetattr(p->fd_in, TCSADRAIN, &newtio);

  stat = fcntl(p->fd_in, F_GETFL, 0);
  if (stat > 0) {
    stat |= O_NONBLOCK;
    fcntl(p->fd_in, F_SETFL, stat);
  }

  p->TermMode = NULL;
}

/*
 * Set tty into terminal mode which is used while we invoke term command.
 */
void
prompt_TtyTermMode(struct prompt *p, struct datalink *dl)
{
  int stat;

  prompt_Printf(p, "Entering terminal mode on %s.\n", dl->name);
  prompt_Printf(p, "Type `~?' for help.\n");

  if (p->Term == stdout)
    tcsetattr(p->fd_in, TCSADRAIN, &p->comtio);

  stat = fcntl(p->fd_in, F_GETFL, 0);
  if (stat > 0) {
    stat &= ~O_NONBLOCK;
    fcntl(p->fd_in, F_SETFL, stat);
  }
  p->TermMode = dl;
}

void
prompt_TtyOldMode(struct prompt *p)
{
  int stat;

  stat = fcntl(p->fd_in, F_GETFL, 0);
  if (stat > 0) {
    stat &= ~O_NONBLOCK;
    fcntl(p->fd_in, F_SETFL, stat);
  }

  if (p->Term == stdout)
    tcsetattr(p->fd_in, TCSADRAIN, &p->oldtio);
}

pid_t
prompt_pgrp(struct prompt *p)
{
  return tcgetpgrp(p->fd_in);
}

int
PasswdCommand(struct cmdargs const *arg)
{
  const char *pass;

  if (!arg->prompt) {
    log_Printf(LogWARN, "passwd: Cannot specify without a prompt\n");
    return 0;
  }

  if (arg->prompt->owner == NULL) {
    log_Printf(LogWARN, "passwd: Not required\n");
    return 0;
  }

  if (arg->argc == arg->argn)
    pass = "";
  else if (arg->argc > arg->argn+1)
    return -1;
  else
    pass = arg->argv[arg->argn];

  if (!strcmp(arg->prompt->owner->passwd, pass))
    arg->prompt->auth = LOCAL_AUTH;
  else
    arg->prompt->auth = LOCAL_NO_AUTH;

  return 0;
}

static struct pppTimer bgtimer;

static void
prompt_TimedContinue(void *v)
{
  prompt_Continue((struct prompt *)v);
}

void
prompt_Continue(struct prompt *p)
{
  timer_Stop(&bgtimer);
  if (getpgrp() == prompt_pgrp(p)) {
    prompt_TtyCommandMode(p);
    p->nonewline = 1;
    prompt_Required(p);
    p->active = 1;
  } else if (!p->owner) {
    bgtimer.func = prompt_TimedContinue;
    bgtimer.name = "prompt bg";
    bgtimer.load = SECTICKS;
    bgtimer.arg = p;
    timer_Start(&bgtimer);
  }
}

void
prompt_Suspend(struct prompt *p)
{
  if (getpgrp() == prompt_pgrp(p)) {
    prompt_TtyOldMode(p);
    p->active = 0;
  }
}
