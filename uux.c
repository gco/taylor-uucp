/* uux.c
   Prepare to execute a command on a remote system.

   Copyright (C) 1991 Ian Lance Taylor

   This file is part of the Taylor UUCP package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   The author of the program may be contacted at ian@airs.com or
   c/o AIRS, P.O. Box 520, Waltham, MA 02254.

   $Log$
   Revision 1.20  1992/01/21  19:39:12  ian
   Chip Salzenberg: uucp and uux start uucico for right system, not any

   Revision 1.19  1992/01/15  07:06:29  ian
   Set configuration directory in Makefile rather than sysdep.h

   Revision 1.18  1992/01/05  03:09:17  ian
   Changed abProgram and abVersion to non const to avoid compiler bug

   Revision 1.17  1992/01/05  02:51:38  ian
   Allocate enough space for log message

   Revision 1.16  1991/12/29  04:04:18  ian
   Added a bunch of extern definitions

   Revision 1.15  1991/12/21  21:16:05  ian
   Franc,ois Pinard: remove parentheses from ZSHELLSEPS

   Revision 1.14  1991/12/20  03:07:54  ian
   Added space and tab to ZSHELLSEPS to stop command at whitespace

   Revision 1.13  1991/12/18  03:54:14  ian
   Made error messages to terminal appear more normal

   Revision 1.12  1991/12/14  16:09:07  ian
   Added -l option to uux to link files into the spool directory

   Revision 1.11  1991/12/11  03:59:19  ian
   Create directories when necessary; don't just assume they exist

   Revision 1.10  1991/12/07  03:03:12  ian
   Split arguments like sh; request sh execution if any metachars appear

   Revision 1.9  1991/11/21  22:17:06  ian
   Add version string, print version when printing usage

   Revision 1.8  1991/11/15  19:17:32  ian
   Hannu Strang: copy stdin using fread/fwrite, not fgets/fputs

   Revision 1.7  1991/11/13  23:08:40  ian
   Expand remote pathnames in uucp and uux; fix up uux special cases

   Revision 1.6  1991/11/08  21:53:17  ian
   Brian Campbell: fix argument handling when looking for '-'

   Revision 1.5  1991/11/07  22:52:49  ian
   Chip Salzenberg: avoid recursive strtok, handle redirection better

   Revision 1.4  1991/09/19  03:23:34  ian
   Chip Salzenberg: append to private debugging file, don't overwrite it

   Revision 1.3  1991/09/19  02:30:37  ian
   From Chip Salzenberg: check whether signal is ignored differently

   Revision 1.2  1991/09/11  02:33:14  ian
   Added ffork argument to fsysdep_run

   Revision 1.1  1991/09/10  19:40:31  ian
   Initial revision

   */

#include "uucp.h"

#if USE_RCS_ID
char uux_rcsid[] = "$Id$";
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>

#include "getopt.h"

#include "system.h"
#include "sysdep.h"

/* External functions.  */
extern int fclose ();

/* These character lists should, perhaps, be in sysdep.h.  */

/* This is the list of shell metacharacters that we check for.  If one
   of these is present, we request uuxqt to execute the command with
   /bin/sh.  Otherwise we let it execute using execve.  */

#define ZSHELLCHARS "\"'`*?[;&()|<>\\$"

/* This is the list of word separators.  We break filename arguments
   at these characters.  */
#define ZSHELLSEPS ";&*|<> \t"

/* This is the list of word separators without the redirection
   operators.  */
#define ZSHELLNONREDIRSEPS ";&*| \t"

/* The program name.  */
char abProgram[] = "uux";

/* Long getopt options.  */

static const struct option asXlongopts[] = { { NULL, 0, NULL, 0 } };

const struct option *_getopt_long_options = asXlongopts;

/* The execute file we are creating.  */

static FILE *eXxqt_file;

/* A list of commands to be spooled.  */

static struct scmd *pasXcmds;
static int cXcmds;

/* Local functions.  */

static void uxusage P((void));
static sigret_t uxcatch P((int isig));
static void uxadd_xqt_line P((int bchar, const char *z1, const char *z2));
static void uxadd_send_file P((const char *zfrom, const char *zto,
			       const char *zoptions, const char *ztemp));

int
main (argc, argv)
     int argc;
     char **argv;
{
  int iopt;
  /* -a: requestor address for status reports.  */
  const char *zrequestor = NULL;
  /* -b: if true, return standard input on error.  */
  boolean fretstdin = FALSE;
  /* -c,-C: if true, copy to spool directory.  */
  boolean fcopy = FALSE;
  /* -c: set if -c appears explicitly; if it and -l appear, then if the
     link fails we don't copy the file.  */
  boolean fdontcopy = FALSE;
  /* -I: configuration file name.  */
  const char *zconfig = NULL;
  /* -j: output job id.  */
  boolean fjobid = FALSE;
  /* -g: job grade.  */
  char bgrade = BDEFAULT_UUX_GRADE;
  /* -l: link file to spool directory.  */
  boolean flink = FALSE;
  /* -n: do not notify upon command completion.  */
  boolean fno_ack = FALSE;
  /* -p: read standard input for command standard input.  */
  boolean fread_stdin = FALSE;
  /* -r: do not start uucico when finished.  */
  boolean fuucico = TRUE;
  /* -s: report status to named file.  */
  const char *zstatus_file = NULL;
  /* -W: only expand local file names.  */
  boolean fexpand = TRUE;
  /* -x: set debugging level.  */
  int idebug = -1;
  /* -z: report status only on error.  */
  boolean ferror_ack = FALSE;
  const char *zuser;
  int i;
  int clen;
  char *zargs;
  char *zarg;
  char *zcmd;
  char *zexclam;
  struct ssysteminfo sxqtsys;
  const struct ssysteminfo *qxqtsys;
  boolean fxqtlocal;
  char **pzargs;
  int calloc_args;
  int cargs;
  const char *zxqtname;
  char abxqt_tname[CFILE_NAME_LEN];
  char abxqt_xname[CFILE_NAME_LEN];
  boolean fneedshell;
  char *zprint;
  const char *zcall_system;
  boolean fcall_any;
  boolean fexit;

  /* We need to be able to read a single - as an option, which getopt
     won't do.  So that we can still use getopt, we run through the
     options looking for an option "-"; if we find one we change it to
     "-p", which is an equivalent option.  */

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] != '-')
	break;
      if (argv[i][1] == '\0')
	argv[i] = xstrdup ("-p");
      else
	{
	  const char *z;

	  for (z = argv[i] + 1; *z != '\0'; z++)
	    {
	      /* If the option takes an argument, and the argument is
		 not appended, then skip the next argument.  */
	      if (*z == 'a' || *z == 'g' || *z == 'I'
		  || *z == 's' || *z == 'x')
		{
		  if (z[1] == '\0')
		    i++;
		  break;
		}
	    }
	}
    }

  /* The leading + in the getopt string means to stop processing
     options as soon as a non-option argument is seen.  */

  while ((iopt = getopt (argc, argv, "+a:bcCg:I:jlnprs:Wx:z")) != EOF)
    {
      switch (iopt)
	{
	case 'a':
	  /* Set requestor name: mail address to which status reports
	     should be sent.  */
	  zrequestor = optarg;
	  break;

	case 'b':
	  /* Return standard input on error.  */
	  fretstdin = TRUE;
	  break;

	case 'c':
	  /* Do not copy local files to spool directory.  */
	  fcopy = FALSE;
	  fdontcopy = TRUE;
	  break;

	case 'C':
	  /* Copy local files to spool directory.  */
	  fcopy = TRUE;
	  break;

	case 'I':
	  /* Configuration file name.  */ 
	  zconfig = optarg;
	  break;

	case 'j':
	  /* Output jobid.  */
	  fjobid = TRUE;
	  break;

	case 'g':
	  /* Set job grade.  */
	  bgrade = optarg[0];
	  break;

	case 'l':
	  /* Link file to spool directory.  */
	  flink = TRUE;
	  break;

	case 'n':
	  /* Do not notify upon command completion.  */
	  fno_ack = TRUE;
	  break;

	case 'p':
	  /* Read standard input for command standard input.  */
	  fread_stdin = TRUE;
	  break;

	case 'r':
	  /* Do not start uucico when finished.  */
	  fuucico = FALSE;
	  break;

	case 's':
	  /* Report status to named file.  */
	  zstatus_file = optarg;
	  break;

	case 'W':
	  /* Only expand local file names.  */
	  fexpand = FALSE;
	  break;

	case 'x':
	  /* Set debugging level.  */
	  idebug = atoi (optarg);
	  break;

	case 'z':
	  /* Report status only on error.  */
	  ferror_ack = TRUE;
	  break;

	case 0:
	  /* Long option found and flag set.  */
	  break;

	default:
	  uxusage ();
	  break;
	}
    }

  if (! FGRADE_LEGAL (bgrade))
    {
      /* We use LOG_NORMAL rather than LOG_ERROR because this is going
	 to stderr rather than to the log file, and we don't need the
	 ERROR header string.  */
      ulog (LOG_NORMAL, "Ignoring illegal grade");
      bgrade = BDEFAULT_UUX_GRADE;
    }

  if (optind == argc)
    uxusage ();

  uread_config (zconfig);

  /* Let command line override configuration file.  */
  if (idebug != -1)
    iDebug = idebug;

#ifdef SIGINT
  if (signal (SIGINT, SIG_IGN) != SIG_IGN)
    (void) signal (SIGINT, uxcatch);
#endif
#ifdef SIGHUP
  if (signal (SIGHUP, SIG_IGN) != SIG_IGN)
    (void) signal (SIGHUP, uxcatch);
#endif
#ifdef SIGQUIT
  if (signal (SIGQUIT, SIG_IGN) != SIG_IGN)
    (void) signal (SIGQUIT, uxcatch);
#endif
#ifdef SIGTERM
  if (signal (SIGTERM, SIG_IGN) != SIG_IGN)
    (void) signal (SIGTERM, uxcatch);
#endif
#ifdef SIGPIPE
  if (signal (SIGPIPE, SIG_IGN) != SIG_IGN)
    (void) signal (SIGPIPE, uxcatch);
#endif
#ifdef SIGABRT
  (void) signal (SIGABRT, uxcatch);
#endif

  usysdep_initialize (FALSE);

  zuser = zsysdep_login_name ();
  if (zuser == NULL)
    zuser = "unknown";

  /* The command and files arguments could be quoted in any number of
     ways, so we split them apart ourselves.  */
  clen = 1;
  for (i = optind; i < argc; i++)
    clen += strlen (argv[i]) + 1;

  zargs = (char *) alloca (clen);
  *zargs = '\0';
  for (i = optind; i < argc; i++)
    {
      strcat (zargs, argv[i]);
      strcat (zargs, " ");
    }

  /* The first argument is the command to execute.  */
  clen = strcspn (zargs, ZSHELLSEPS);
  zcmd = (char *) alloca (clen + 1);
  strncpy (zcmd, zargs, clen);
  zcmd[clen] = '\0';
  zargs += clen;

  /* Figure out which system the command is to be executed on.  */
  zexclam = strchr (zcmd, '!');
  if (zexclam == NULL)
    {
      qxqtsys = &sLocalsys;
      fxqtlocal = TRUE;
    }
  else
    {
      *zexclam = '\0';

      if (*zcmd == '\0' || strcmp (zcmd, zLocalname) == 0)
	{
	  qxqtsys = &sLocalsys;
	  fxqtlocal = TRUE;
	}
      else
	{
	  if (fread_system_info (zcmd, &sxqtsys))
	    qxqtsys = &sxqtsys;
	  else
	    {
	      if (! fUnknown_ok)
		ulog (LOG_FATAL, "System %s unknown", zcmd);
	      qxqtsys = &sUnknown;
	      sUnknown.zname = zcmd;
	    }

	  fxqtlocal = FALSE;
	}

      zcmd = zexclam + 1;
    }

  /* Make sure we have a spool directory.  */

  if (! fsysdep_make_spool_dir (qxqtsys))
    {
      ulog_close ();
      usysdep_exit (FALSE);
    }

  /* Split the arguments out into an array.  We break the arguments
     into alternating sequences of characters not in ZSHELLSEPS
     and characters in ZSHELLSEPS.  We remove whitespace.  We
     separate the redirection characters '>' and '<' into their
     own arguments to make them easier to process below.  */

  calloc_args = 10;
  pzargs = (char **) xmalloc (calloc_args * sizeof (char *));
  cargs = 0;

  for (zarg = strtok (zargs, " \t");
       zarg != NULL;
       zarg = strtok ((char *) NULL, " \t"))
    {
      while (*zarg != '\0')
	{
	  if (cargs >= calloc_args + 1)
	    {
	      calloc_args += 10;
	      pzargs = (char **) xrealloc ((pointer) pzargs,
					   calloc_args * sizeof (char *));
	    }

	  clen = strcspn (zarg, ZSHELLSEPS);
	  if (clen > 0)
	    {
	      pzargs[cargs] = (char *) xmalloc (clen + 1);
	      strncpy (pzargs[cargs], zarg, clen);
	      pzargs[cargs][clen] = '\0';
	      ++cargs;
	      zarg += clen;
	    }

	  /* We deliberately separate '>' and '<' out.  */
	  if (*zarg != '\0')
	    {
	      clen = strspn (zarg, ZSHELLNONREDIRSEPS);
	      if (clen == 0)
		clen = 1;
	      pzargs[cargs] = (char *) xmalloc (clen + 1);
	      strncpy (pzargs[cargs], zarg, clen);
	      pzargs[cargs][clen] = '\0';
	      ++cargs;
	      zarg += clen;
	    }
	}
    }

  /* Name and open the execute file.  If the execution is to occur on
     a remote system, we must create a data file and copy it over.  */
  if (fxqtlocal)
    zxqtname = zsysdep_xqt_file_name ();
  else
    zxqtname = zsysdep_data_file_name (qxqtsys, 'X', abxqt_tname,
				       (char *) NULL, abxqt_xname);
  if (zxqtname == NULL)
    {
      ulog_close ();
      usysdep_exit (FALSE);
    }

  eXxqt_file = esysdep_fopen (zxqtname, FALSE, FALSE, TRUE);
  if (eXxqt_file == NULL)
    {
      ulog_close ();
      usysdep_exit (FALSE);
    }

  /* Specify the user.  */
  uxadd_xqt_line ('U', zuser, zLocalname);

  /* Look through the arguments.  Any argument containing an
     exclamation point character is interpreted as a file name, and is
     sent to the appropriate system.  */

  zcall_system = NULL;
  fcall_any = FALSE;

  for (i = 0; i < cargs; i++)
    {
      const char *zsystem, *zconst;
      char *zfile;
      boolean finput, foutput;
      boolean flocal;

      /* Check for a parenthesized argument; remove the parentheses
	 and otherwise ignore it (this is how an exclamation point is
	 quoted).  */

      if (pzargs[i][0] == '(')
	{
	  clen = strlen (pzargs[i]);
	  if (pzargs[i][clen - 1] != ')')
	    {
	      /* Use LOG_NORMAL because we don't need the ERROR:
		 header.  */
	      ulog (LOG_NORMAL, "Mismatched parentheses");
	    }
	  else
	    pzargs[i][clen - 1] = '\0';
	  ++pzargs[i];
	  continue;
	}

      /* Check whether we are doing a redirection.  */

      finput = FALSE;
      foutput = FALSE;
      if (i + 1 < cargs)
	{
	  if (pzargs[i][0] == '<')
	    finput = TRUE;
	  else if (pzargs[i][0] == '>')
	    foutput = TRUE;
	  if (finput || foutput)
	    {
	      pzargs[i] = NULL;
	      i++;
	    }
	}

      zexclam = strchr (pzargs[i], '!');

      /* If there is no exclamation point and no redirection, this
	 argument is left untouched.  */

      if (zexclam == NULL && ! finput && ! foutput)
	continue;

      /* Get the system name and file name for this file.  */

      if (zexclam == NULL)
	{
	  zsystem = zLocalname;
	  zfile = pzargs[i];
	  flocal = TRUE;
	}
      else
	{
	  *zexclam = '\0';
	  zsystem = pzargs[i];
	  if (zsystem[0] != '\0')
	    flocal = strcmp (zsystem, zLocalname) == 0;
	  else
	    {
	      zsystem = zLocalname;
	      flocal = TRUE;
	    }
	  zfile = zexclam + 1;
	}

      /* Add the current working directory to the file name if it's
	 not an absolute path.  */
      if (fexpand || flocal)
	{
	  zconst = zsysdep_add_cwd (zfile, flocal);
	  if (zconst == NULL)
	    {
	      ulog_close ();
	      usysdep_exit (FALSE);
	    }	
	  zfile = xstrdup (zconst);
	}

      /* Check for output redirection.  We strip this argument out,
	 and create an O command which tells uuxqt where to send the
	 output.  */

      if (foutput)
	{
	  if (strcmp (zsystem, qxqtsys->zname) == 0)
	    uxadd_xqt_line ('O', zfile, (const char *) NULL);
	  else
	    uxadd_xqt_line ('O', zfile, zsystem);
	  pzargs[i] = NULL;
	  continue;
	}

      if (finput)
	{
	  if (fread_stdin)
	    ulog (LOG_FATAL, "Standard input specified twice");
	  pzargs[i] = NULL;
	}

      if (flocal)
	{
	  char *zuse;
	  const char *zdata;
	  char abtname[CFILE_NAME_LEN];
	  char abdname[CFILE_NAME_LEN];

	  /* It's a local file.  If requested by -C, copy the file to
	     the spool directory.  If requested by -l, link the file
	     to the spool directory; if the link fails, we copy the
	     file, unless -c was explictly used.  If the file is being
	     shipped to another system, we must set up a transfer
	     request.  First make sure the user has legitimate access,
	     since we are running setuid.  */
	  if (! fsysdep_access (zfile))
	    {
	      ulog_close ();
	      usysdep_exit (FALSE);
	    }

	  if (fcopy || flink)
	    {
	      char *zdup;
	      boolean fdid;

	      zdata = zsysdep_data_file_name (qxqtsys, bgrade, abtname,
					      abdname, (char *) NULL);
	      if (zdata == NULL)
		{
		  ulog_close ();
		  usysdep_exit (FALSE);
		}

	      zdup = xstrdup (zdata);

	      fdid = FALSE;
	      if (flink)
		{
		  boolean fworked;

		  if (! fsysdep_link (zfile, zdup, &fworked))
		    {
		      ulog_close ();
		      usysdep_exit (FALSE);
		    }

		  if (fworked)
		    fdid = TRUE;
		  else if (fdontcopy)
		    ulog (LOG_FATAL, "%s: Can't link to spool directory",
			  zfile);
		}

	      if (! fdid)
		{
		  if (! fcopy_file (zfile, zdup, FALSE, TRUE))
		    {
		      ulog_close ();
		      usysdep_exit (FALSE);
		    }
		}

	      xfree ((pointer) zdup);

	      zuse = abtname;
	    }
	  else
	    {
	      /* Make sure the daemon can access the file.  */
	      if (! fsysdep_daemon_access (zfile))
		{
		  ulog_close ();
		  usysdep_exit (FALSE);
		}

	      zuse = zfile;

	      if (! fxqtlocal)
		{
		  zdata = zsysdep_data_file_name (qxqtsys, bgrade,
						  (char *) NULL, abdname,
						  (char *) NULL);
		  if (zdata == NULL)
		    {
		      ulog_close ();
		      usysdep_exit (FALSE);
		    }
		  strcpy (abtname, "D.0");
		}
	    }

	  if (fxqtlocal)
	    {
	      if (finput)
		uxadd_xqt_line ('I', zuse, (char *) NULL);
	      else
		pzargs[i] = zuse;
	    }
	  else
	    {
	      uxadd_send_file (zuse, abdname,
			       fcopy || flink ? "C" : "c",
			       abtname);

	      if (finput)
		{
		  uxadd_xqt_line ('F', abdname, (char *) NULL);
		  uxadd_xqt_line ('I', abdname, (char *) NULL);
		}
	      else
		{
		  const char *zbase;

		  zbase = zsysdep_base_name (zfile);
		  if (zbase == NULL)
		    {
		      ulog_close ();
		      usysdep_exit (FALSE);
		    }
		  uxadd_xqt_line ('F', abdname, zbase);
		  pzargs[i] = xstrdup (zbase);
		}
	    }
	}
      else if (strcmp (qxqtsys->zname, zsystem) == 0)
	{
	  /* The file is already on the system where the command is to
	     be executed.  */
	  if (finput)
	    uxadd_xqt_line ('I', zfile, (const char *) NULL);
	  else
	    pzargs[i] = zfile;
	}
      else
	{
	  struct ssysteminfo sfromsys;
	  const struct ssysteminfo *qfromsys;
	  char abtname[CFILE_NAME_LEN];
	  char abdname[CFILE_NAME_LEN];
	  char *ztemp;
	  struct scmd s;

	  /* We need to request a remote file.  Make sure we have a
	     spool directory for the remote system.  */

	  if (! fread_system_info (zsystem, &sfromsys))
	    {
	      if (! fUnknown_ok)
		ulog (LOG_FATAL, "System %s unknown", zsystem);
	      sfromsys = sUnknown;
	      sfromsys.zname = zsystem;
	    }
	  qfromsys = &sfromsys;

	  if (! fsysdep_make_spool_dir (qfromsys))
	    {
	      ulog_close ();
	      usysdep_exit (FALSE);
	    }

	  /* We want the file to wind up in the spool directory of the
	     local system (whether the execution is occurring
	     locally or not); we have to use an absolute file name
	     here, because otherwise the file would wind up in the
	     spool directory of the system it is coming from.  */

	  if (! fxqtlocal)
	    {
	      if (! fsysdep_make_spool_dir (&sLocalsys))
		{
		  ulog_close ();
		  usysdep_exit (FALSE);
		}
	    }

	  zconst = zsysdep_data_file_name (&sLocalsys, bgrade,
					   abtname, (char *) NULL,
					   (char *) NULL);
	  if (zconst == NULL)
	    {
	      ulog_close ();
	      usysdep_exit (FALSE);
	    }

	  /* Request the file.  The special option '9' is a signal to
	     uucico that it's OK to receive a file into the spool
	     directory; normally such requests are rejected.  */

	  s.bcmd = 'R';
	  s.pseq = NULL;
	  s.zfrom = zfile;
	  s.zto = abtname;
	  s.zuser = zuser;
	  s.zoptions = "9";
	  s.ztemp = "";
	  s.imode = 0600;
	  s.znotify = "";
	  s.cbytes = -1;

	  if (! fsysdep_spool_commands (qfromsys, bgrade, 1, &s))
	    {
	      ulog_close ();
	      usysdep_exit (FALSE);
	    }

	  if (fcall_any)
	    zcall_system = NULL;
	  else
	    {
	      fcall_any = TRUE;
	      zcall_system = xstrdup (qfromsys->zname);
	    }

	  /* Now if the execution is to occur on another system, we
	     must create an execute file to send the file there.  The
	     name of the file on the execution system is put into
	     abdname.  */

	  if (fxqtlocal)
	    ztemp = abtname;
	  else
	    {
	      const char *zxqt_file;
	      FILE *e;

	      /* Get a file name to use on the execution system.  */

	      if (zsysdep_data_file_name (qxqtsys, bgrade,
					  (char *) NULL, abdname,
					  (char *) NULL) == NULL)
		{
		  ulog_close ();
		  usysdep_exit (FALSE);
		}
	      ztemp = abdname;

	      /* The local spool directory was created above, if it
		 didn't already exist.  */

	      zxqt_file = zsysdep_xqt_file_name ();
	      if (zxqt_file == NULL)
		{
		  ulog_close ();
		  usysdep_exit (FALSE);
		}

	      /* Queue up a uucp command to be executed locally once
		 the file arrives.  We take advantage of the file
		 renaming and moving that uuxqt does to remove the
		 file and avoid the hassles of adding the current
		 directory.  The -W switch to uucp prevents from
		 adding the current directory to the remote file.  */

	      e = esysdep_fopen (zxqt_file, FALSE, FALSE, TRUE);
	      if (e == NULL)
		{
		  ulog_close ();
		  usysdep_exit (FALSE);
		}

	      fprintf (e, "U %s %s\n", zuser, zLocalname);
	      fprintf (e, "F %s foo\n", abtname);
	      fprintf (e, "C uucp -CW foo %s!%s\n", qxqtsys->zname,
		       abdname);

	      if (fclose (e) != 0)
		ulog (LOG_FATAL, "fclose: %s", strerror (errno));
	    }

	  /* Tell the command execution to wait until the file has
	     been received, and tell it the real file name to use.  */

	  if (finput)
	    {
	      uxadd_xqt_line ('F', ztemp, (char *) NULL);
	      uxadd_xqt_line ('I', ztemp, (char *) NULL);
	    }
	  else
	    {
	      const char *zbase;

	      zbase = zsysdep_base_name (zfile);
	      if (zbase == NULL)
		{
		  ulog_close ();
		  usysdep_exit (FALSE);
		}
	      uxadd_xqt_line ('F', ztemp, zbase);
	      pzargs[i] = xstrdup (zbase);
	    }
	}
    }

  /* If standard input is to be read from the stdin of uux, we read it
     here into a temporary file and send it to the execute system.  */

  if (fread_stdin)
    {
      const char *zdata;
      char abtname[CFILE_NAME_LEN];
      char abdname[CFILE_NAME_LEN];
      FILE *e;
      int cread;
      char ab[1024];

      zdata = zsysdep_data_file_name (qxqtsys, bgrade, abtname, abdname,
				      (char *) NULL);
      if (zdata == NULL)
	{
	  ulog_close ();
	  usysdep_exit (FALSE);
	}

      e = esysdep_fopen (zdata, FALSE, FALSE, TRUE);
      if (e == NULL)
	{
	  ulog_close ();
	  usysdep_exit (FALSE);
	}

      do
	{
	  int cwrite;

	  cread = fread (ab, sizeof (char), sizeof ab, stdin);
	  if (cread > 0)
	    {
	      cwrite = fwrite (ab, sizeof (char), cread, e);
	      if (cwrite != cread)
		{
		  if (cwrite == EOF)
		    ulog (LOG_FATAL, "fwrite: %s", strerror (errno));
		  else
		    ulog (LOG_FATAL, "fwrite: Wrote %d when attempted %d",
			  cwrite, cread);
		}
	    }
	}
      while (cread == sizeof ab);

      if (fclose (e) != 0)
	ulog (LOG_FATAL, "fclose: %s", strerror (errno));

      if (fxqtlocal)
	uxadd_xqt_line ('I', abtname, (const char *) NULL);
      else
	{
	  uxadd_xqt_line ('F', abdname, (const char *) NULL);
	  uxadd_xqt_line ('I', abdname, (const char *) NULL);
	  uxadd_send_file (abtname, abdname, "C", abtname);
	}
    }

  /* Here all the arguments have been determined, so the command can
     be written out.  If any of the arguments contain shell
     metacharacters, we request remote execution with /bin/sh (this is
     the 'e' command in the execute file).  The default is assumed to
     be remote execution with execve.  */

  fprintf (eXxqt_file, "C %s", zcmd);

  fneedshell = FALSE;

  if (zcmd[strcspn (zcmd, ZSHELLCHARS)] != '\0')
    fneedshell = TRUE;

  for (i = 0; i < cargs; i++)
    {
      if (pzargs[i] != NULL)
	{
	  fprintf (eXxqt_file, " %s", pzargs[i]);
	  if (pzargs[i][strcspn (pzargs[i], ZSHELLCHARS)] != '\0')
	    fneedshell = TRUE;
	}
    }

  fprintf (eXxqt_file, "\n");

  /* Write out all the other miscellaneous junk.  */

  if (fno_ack)
    uxadd_xqt_line ('N', (const char *) NULL, (const char *) NULL);

  if (ferror_ack)
    uxadd_xqt_line ('Z', (const char *) NULL, (const char *) NULL);

  if (zrequestor != NULL)
    uxadd_xqt_line ('R', zrequestor, (const char *) NULL);

  if (fretstdin)
    uxadd_xqt_line ('B', (const char *) NULL, (const char *) NULL);

  if (zstatus_file != NULL)
    uxadd_xqt_line ('M', zstatus_file, (const char *) NULL);

  if (fneedshell)
    uxadd_xqt_line ('e', (const char *) NULL, (const char *) NULL);

  if (fclose (eXxqt_file) != 0)
    ulog (LOG_FATAL, "fclose: %s", strerror (errno));

  /* If the execution is to occur on another system, we must now
     arrange to copy the execute file to this system.  */

  if (! fxqtlocal)
    uxadd_send_file (abxqt_tname, abxqt_xname, "C", abxqt_tname);

  if (cXcmds > 0)
    {
      if (! fsysdep_spool_commands (qxqtsys, bgrade, cXcmds, pasXcmds))
	{
	  ulog_close ();
	  usysdep_exit (FALSE);
	}

      if (fcall_any)
	zcall_system = NULL;
      else
	{
	  fcall_any = TRUE;
	  zcall_system = qxqtsys->zname;
	}
    }

  /* If all that worked, make a log file entry.  All log file reports
     up to this point went to stderr.  */

  ulog_to_file (TRUE);
  ulog_system (qxqtsys->zname);
  ulog_user (zuser);

  clen = strlen (zcmd) + 2;
  for (i = 0; i < cargs; i++)
    if (pzargs[i] != NULL)
      clen += strlen (pzargs[i]) + 1;

  zprint = (char *) alloca (clen);
  strcpy (zprint, zcmd);
  strcat (zprint, " ");
  for (i = 0; i < cargs; i++)
    {
      if (pzargs[i] != NULL)
	{
	  strcat (zprint, pzargs[i]);
	  strcat (zprint, " ");
	}
    }
  zprint[strlen (zprint) - 1] = '\0';

  ulog (LOG_NORMAL, "Queuing %s", zprint);

  ulog_close ();

  if (! fuucico)
    fexit = TRUE;
  else
    {
      if (zcall_system != NULL)
	fexit = fsysdep_run (TRUE, "uucico", "-s", zcall_system);
      else if (fcall_any)
	fexit = fsysdep_run (TRUE, "uucico", "-r1", (const char *) NULL);
      else
	fexit = TRUE;
    }

  usysdep_exit (fexit);

  /* Avoid error about not returning a value.  */
  return 0;
}

/* Report command usage.  */

static void
uxusage ()
{
  fprintf (stderr,
	   "Taylor UUCP version %s, copyright (C) 1991 Ian Lance Taylor\n",
	   abVersion);
  fprintf (stderr,
	   "Usage: uux [options] [-] command\n");
  fprintf (stderr,
	   " -,-p: Read standard input for standard input of command\n");
  fprintf (stderr,
	   " -c: Do not copy local files to spool directory (default)\n");
  fprintf (stderr,
	   " -C: Copy local files to spool directory\n");
  fprintf (stderr,
	   " -l: link local files to spool directory\n");
  fprintf (stderr,
	   " -g grade: Set job grade (must be alphabetic)\n");
  fprintf (stderr,
	   " -n: Do not report completion status\n");
  fprintf (stderr,
	   " -z: Report completion status only on error\n");
  fprintf (stderr,
	   " -r: Do not start uucico daemon\n");
  fprintf (stderr,
	   " -a address: Address to mail status report to\n");
  fprintf (stderr,
	   " -b: Return standard input with status report\n");
  fprintf (stderr,
	   " -s file: Report completion status to file\n");
  fprintf (stderr,
	   " -j: Report job id\n");
  fprintf (stderr,
	   " -x debug: Set debugging level\n");
#if HAVE_TAYLOR_CONFIG
  fprintf (stderr,
	   " -I file: Set configuration file to use (default %s%s)\n",
	   NEWCONFIGLIB, CONFIGFILE);
#endif /* HAVE_TAYLOR_CONFIG */
  exit (EXIT_FAILURE);
}

/* Catch a signal.  We should clean up here, but so far we don't.  */

static sigret_t
uxcatch (isig)
     int isig;
{
  if (fAborting)
    {
      ulog_close ();
      usysdep_exit (FALSE);
    }
  else
    {
      ulog (LOG_ERROR, "Got signal %d", isig);
      ulog_close ();
      (void) signal (isig, SIG_DFL);
      raise (isig);
    }
}

/* Add a line to the execute file.  */

static void
uxadd_xqt_line (bchar, z1, z2)
     int bchar;
     const char *z1;
     const char *z2;
{
  if (z1 == NULL)
    fprintf (eXxqt_file, "%c\n", bchar);
  else if (z2 == NULL)
    fprintf (eXxqt_file, "%c %s\n", bchar, z1);
  else
    fprintf (eXxqt_file, "%c %s %s\n", bchar, z1, z2);
}

/* Add a file to be sent to the execute system.  */

static void
uxadd_send_file (zfrom, zto, zoptions, ztemp)
     const char *zfrom;
     const char *zto;
     const char *zoptions;
     const char *ztemp;
{
  struct scmd s;

  s.bcmd = 'S';
  s.pseq = NULL;
  s.zfrom = xstrdup (zfrom);
  s.zto = xstrdup (zto);
  s.zuser = zsysdep_login_name ();
  s.zoptions = xstrdup (zoptions);
  s.ztemp = xstrdup (ztemp);
  s.imode = 0666;
  s.znotify = "";
  s.cbytes = -1;

  ++cXcmds;
  pasXcmds = (struct scmd *) xrealloc ((pointer) pasXcmds,
				       cXcmds * sizeof (struct scmd));
  pasXcmds[cXcmds - 1] = s;
}
