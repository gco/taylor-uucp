/* uucico.c
   This is the main UUCP communication program.

   Copyright (C) 1991, 1992 Ian Lance Taylor

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
   c/o Infinity Development Systems, P.O. Box 520, Waltham, MA 02254.
   */

#include "uucp.h"

#if USE_RCS_ID
const char uucico_rcsid[] = "$Id$";
#endif

#include <ctype.h>

#include "getopt.h"

#include "conn.h"
#include "prot.h"
#include "trans.h"
#include "system.h"

/* The program name.  */
char abProgram[] = "uucico";

/* Define the known protocols.  */

#define TCP_PROTO \
  (UUCONF_RELIABLE_ENDTOEND \
   | UUCONF_RELIABLE_RELIABLE \
   | UUCONF_RELIABLE_EIGHT)

static const struct sprotocol asProtocols[] =
{
  { 't', TCP_PROTO, 1,
      asTproto_params, ftstart, ftshutdown, ftsendcmd, ztgetspace,
      ftsenddata, ftwait, ftfile },
  { 'e', TCP_PROTO, 1,
      asEproto_params, festart, feshutdown, fesendcmd, zegetspace,
      fesenddata, fewait, fefile },
  { 'g', UUCONF_RELIABLE_EIGHT, 1,
      asGproto_params, fgstart, fgshutdown, fgsendcmd, zggetspace,
      fgsenddata, fgwait, NULL },
  { 'f', UUCONF_RELIABLE_RELIABLE, 1,
      asFproto_params, ffstart, ffshutdown, ffsendcmd, zfgetspace,
      ffsenddata, ffwait, fffile },
};

#define CPROTOCOLS (sizeof asProtocols / sizeof asProtocols[0])

/* Locked system.  */
static boolean fLocked_system;
static struct uuconf_system sLocked_system;

/* Open connection.  */
static struct sconnection *qConn;

/* uuconf global pointer; need to close the connection after a fatal
   error.  */
static pointer pUuconf;

/* This structure is passed to iuport_lock via uuconf_find_port.  */
struct spass
{
  boolean fmatched;
  boolean flocked;
  struct sconnection *qconn;
};

/* Local functions.  */

static void uusage P((void));
static void uabort P((void));
static boolean fcall P((pointer puuconf,
			const struct uuconf_system *qsys,
			struct uuconf_port *qport, boolean fifwork,
			boolean fforce, boolean fdetach,
			boolean ftimewarn));
static boolean fconn_call P((struct sdaemon *qdaemon,
			     struct uuconf_port *qport,
			     struct sstatus *qstat, int cretry,
			     boolean *pfcalled));
static boolean fdo_call P((struct sdaemon *qdaemon,
			   struct sstatus *qstat,
			   const struct uuconf_dialer *qdialer,
			   boolean *pfcalled, enum tstatus_type *pterr));
static int iuport_lock P((struct uuconf_port *qport, pointer pinfo));
static boolean flogin_prompt P((pointer puuconf,
				struct sconnection *qconn));
static boolean faccept_call P((pointer puuconf, const char *zlogin,
			       struct sconnection *qconn,
			       const char **pzsystem));
static void uapply_proto_params P((pointer puuconf, int bproto,
				   struct uuconf_cmdtab *qcmds,
				   struct uuconf_proto_param *pas));
static boolean fsend_uucp_cmd P((struct sconnection *qconn,
				 const char *z));
static char *zget_uucp_cmd P((struct sconnection *qconn,
			      boolean frequired));
static char *zget_typed_line P((struct sconnection *qconn));

/* Long getopt options.  */

static const struct option asLongopts[] = { { NULL, 0, NULL, 0 } };

int
main (argc, argv)
     int argc;
     char **argv;
{
  /* -c: Whether to warn if a call is attempted at a bad time.  */
  boolean ftimewarn = TRUE;
  /* -D: don't detach from controlling terminal.  */
  boolean fdetach = TRUE;
  /* -e: Whether to do an endless loop of accepting calls.  */
  boolean fendless = FALSE;
  /* -f: Whether to force a call despite status of previous call.  */
  boolean fforce = FALSE;
  /* -I file: configuration file name.  */
  const char *zconfig = NULL;
  /* -l: Whether to give a single login prompt.  */
  boolean flogin = FALSE;
  /* -P port: port to use; in master mode, call out on this port.  In
     slave mode, accept logins on this port.  If port not specified,
     then in master mode figure it out for each system, and in slave
     mode use stdin and stdout.  */
  const char *zport = NULL;
  /* -q: Whether to start uuxqt when done.  */
  boolean fuuxqt = TRUE;
  /* -r1: Whether we are the master.  */
  boolean fmaster = FALSE;
  /* -s,-S system: system to call.  */
  const char *zsystem = NULL;
  /* -w: Whether to wait for a call after doing one.  */
  boolean fwait = FALSE;
  int iopt;
  struct uuconf_port *qport;
  struct uuconf_port sport;
  boolean fret = TRUE;
  pointer puuconf;
  int iuuconf;
#if DEBUG > 1
  int iholddebug;
#endif

  while ((iopt = getopt (argc, argv,
			 "cDefI:lp:qr:s:S:u:x:X:w")) != EOF)
    {
      switch (iopt)
	{
	case 'c':
	  /* Don't warn if a call is attempted at a bad time.  */
	  ftimewarn = FALSE;
	  break;

	case 'D':
	  /* Don't detach from controlling terminal.  */
	  fdetach = FALSE;
	  break;

	case 'e':
	  /* Do an endless loop of accepting calls.  */
	  fendless = TRUE;
	  break;

	case 'f':
	  /* Force a call even if it hasn't been long enough since the last
	     failed call.  */
	  fforce = TRUE;
	  break;

	case 'I':
	  /* Set configuration file name (default is in sysdep.h).  */
	  if (fsysdep_other_config (optarg))
	    zconfig = optarg;
	  break;

	case 'l':
	  /* Prompt for login name and password.  */
	  flogin = TRUE;
	  break;

	case 'p':
	  /* Port to use  */
	  zport = optarg;
	  break;

	case 'q':
	  /* Don't start uuxqt.  */
	  fuuxqt = FALSE;
	  break;

	case 'r':
	  /* Set mode: -r1 for master, -r0 for slave (default)  */
	  if (strcmp (optarg, "1") == 0)
	    fmaster = TRUE;
	  else if (strcmp (optarg, "0") == 0)
	    fmaster = FALSE;
	  else
	    uusage ();
	  break;
    
	case 's':
	  /* Set system name  */
	  zsystem = optarg;
	  fmaster = TRUE;
	  break;

	case 'S':
	  /* Set system name and force call like -f  */
	  zsystem = optarg;
	  fforce = TRUE;
	  fmaster = TRUE;
	  break;

	case 'u':
	  /* Some versions of uucpd invoke uucico with a -u argument
	     specifying the login name.  I'm told it is safe to ignore
	     this value, although perhaps we should use it rather than
	     zsysdep_login_name ().  */
	  break;

	case 'x':
	case 'X':
#if DEBUG > 1
	  /* Set debugging level  */
	  iDebug |= idebug_parse (optarg);
#endif
	  break;

	case 'w':
	  /* Call out and then wait for a call in  */
	  fwait = TRUE;
	  break;

	case 0:
	  /* Long option found, and flag value set.  */
	  break;

	default:
	  uusage ();
	  break;
	}
    }

  if (optind != argc)
    uusage ();

  if (fwait && zport == NULL)
    {
      ulog (LOG_ERROR, "-w requires -e");
      uusage ();
    }

  iuuconf = uuconf_init (&puuconf, (const char *) NULL, zconfig);
  if (iuuconf != UUCONF_SUCCESS)
    ulog_uuconf (LOG_FATAL, puuconf, iuuconf);
  pUuconf = puuconf;

#if DEBUG > 1
  {
    const char *zdebug;

    iuuconf = uuconf_debuglevel (puuconf, &zdebug);
    if (iuuconf != UUCONF_SUCCESS)
      ulog_uuconf (LOG_FATAL, puuconf, iuuconf);
    if (zdebug != NULL)
      iDebug |= idebug_parse (zdebug);
  }
#endif

  /* If a port was named, get its information.  */
  if (zport == NULL)
    qport = NULL;
  else
    {
      iuuconf = uuconf_find_port (puuconf, zport, (long) 0, (long) 0,
				  (int (*) P((struct uuconf_port *,
					      pointer))) NULL,
				  (pointer) NULL, &sport);
      if (iuuconf == UUCONF_NOT_FOUND)
	ulog (LOG_FATAL, "%s: Port not found", zport);
      else if (iuuconf != UUCONF_SUCCESS)
	ulog_uuconf (LOG_FATAL, puuconf, iuuconf);
      qport = &sport;
    }

#ifdef SIGINT
  usysdep_signal (SIGINT);
#endif
#ifdef SIGHUP
  usysdep_signal (SIGHUP);
#endif
#ifdef SIGQUIT
  usysdep_signal (SIGQUIT);
#endif
#ifdef SIGTERM
  usysdep_signal (SIGTERM);
#endif
#ifdef SIGPIPE
  usysdep_signal (SIGPIPE);
#endif

  usysdep_initialize (puuconf, INIT_DAEMON);

  ulog_to_file (puuconf, TRUE);
  ulog_fatal_fn (uabort);

  if (fmaster)
    {
      if (zsystem != NULL)
	{
	  /* A system was named.  Call it.  */
	  iuuconf = uuconf_system_info (puuconf, zsystem,
					&sLocked_system);
	  if (iuuconf == UUCONF_NOT_FOUND)
	    ulog (LOG_FATAL, "%s: System not found", zsystem);
	  else if (iuuconf != UUCONF_SUCCESS)
	    ulog_uuconf (LOG_FATAL, puuconf, iuuconf);

	  /* Detach from the controlling terminal for the call.  This
	     probably makes sense only on Unix.  We want the modem
	     line to become the controlling terminal.  */
	  if (fdetach &&
	      (qport == NULL
	       || qport->uuconf_ttype != UUCONF_PORTTYPE_STDIN))
	    usysdep_detach ();

	  ulog_system (sLocked_system.uuconf_zname);

#if DEBUG > 1
	  iholddebug = iDebug;
	  if (sLocked_system.uuconf_zdebug != NULL)
	    iDebug |= idebug_parse (sLocked_system.uuconf_zdebug);
#endif

	  if (! fsysdep_lock_system (&sLocked_system))
	    {
	      ulog (LOG_ERROR, "System already locked");
	      fret = FALSE;
	    }
	  else
	    {
	      fLocked_system = TRUE;
	      fret = fcall (puuconf, &sLocked_system, qport, FALSE,
			    fforce, fdetach, ftimewarn);
	      if (fLocked_system)
		{
		  (void) fsysdep_unlock_system (&sLocked_system);
		  fLocked_system = FALSE;
		}
	    }
#if DEBUG > 1
	  iDebug = iholddebug;
#endif
	  ulog_system ((const char *) NULL);
	  (void) uuconf_system_free (puuconf, &sLocked_system);
	}
      else
	{
	  char **pznames, **pz;
	  int c, i;
	  boolean fdidone;

	  /* Call all systems which have work to do.  */
	  fret = TRUE;
	  fdidone = FALSE;

	  iuuconf = uuconf_system_names (puuconf, &pznames, 0);
	  if (iuuconf != UUCONF_SUCCESS)
	    ulog_uuconf (LOG_FATAL, puuconf, iuuconf);

	  /* Randomize the order in which we call the systems.  */
	  c = 0;
	  for (pz = pznames; *pz != NULL; pz++)
	    c++;

	  srand ((int) isysdep_time ((long *) NULL));
	  for (i = c - 1; i > 0; i--)
	    {
	      int iuse;
	      char *zhold;

	      iuse = rand () % (i + 1);
	      zhold = pznames[i];
	      pznames[i] = pznames[iuse];
	      pznames[iuse] = zhold;
	    }

	  for (pz = pznames; *pz != NULL && ! FGOT_SIGNAL (); pz++)
	    {
	      iuuconf = uuconf_system_info (puuconf, *pz,
					    &sLocked_system);
	      if (iuuconf != UUCONF_SUCCESS)
		{
		  ulog_uuconf (LOG_ERROR, puuconf, iuuconf);
		  xfree ((pointer) *pz);
		  continue;
		}

	      if (fsysdep_has_work (&sLocked_system))
		{
		  fdidone = TRUE;

		  /* Detach from the controlling terminal.  On Unix
		     this means that we will wind up forking a new
		     process for each system we call.  */
		  if (fdetach
		      && (qport == NULL
			  || qport->uuconf_ttype != UUCONF_PORTTYPE_STDIN))
		    usysdep_detach ();

		  ulog_system (sLocked_system.uuconf_zname);

#if DEBUG > 1
		  iholddebug = iDebug;
		  if (sLocked_system.uuconf_zdebug != NULL)
		    iDebug |= idebug_parse (sLocked_system.uuconf_zdebug);
#endif

		  if (! fsysdep_lock_system (&sLocked_system))
		    {
		      ulog (LOG_ERROR, "System already locked");
		      fret = FALSE;
		    }
		  else
		    {
		      fLocked_system = TRUE;
		      if (! fcall (puuconf, &sLocked_system, qport, TRUE,
				   fforce, fdetach, ftimewarn))
			fret = FALSE;

		      /* Now ignore any SIGHUP that we got.  */
		      afSignal[INDEXSIG_SIGHUP] = FALSE;

		      if (fLocked_system)
			{
			  (void) fsysdep_unlock_system (&sLocked_system);
			  fLocked_system = FALSE;
			}
		    }
#if DEBUG > 1
		  iDebug = iholddebug;
#endif
		  ulog_system ((const char *) NULL);
		}

	      (void) uuconf_system_free (puuconf, &sLocked_system);
	      xfree ((pointer) *pz);
	    }

	  xfree ((pointer) pznames);

	  if (! fdidone)
	    ulog (LOG_NORMAL, "No work");
	}

      /* If requested, wait for calls after dialing out.  */
      if (fwait)
	{
	  fendless = TRUE;
	  fmaster = FALSE;
	}
    }

  if (! fmaster)
    {
      struct sconnection sconn;
      boolean flocked;

      /* If a port was specified by name, we go into endless loop
	 mode.  In this mode, we wait for calls and prompt them with
	 "login:" and "Password:", so that they think we are a regular
	 UNIX system.  If we aren't in endless loop mode, we have been
	 called by some other system.  If flogin is TRUE, we prompt
	 with "login:" and "Password:" a single time.  */

      fret = TRUE;
      zsystem = NULL;

      if (! fconn_init (qport, &sconn))
	fret = FALSE;

      if (qport != NULL)
	{
	  /* We are not using standard input.  Detach from the
	     controlling terminal, so that the port we are about to
	     use becomes our controlling terminal.  */
	  if (fdetach
	      && qport->uuconf_ttype != UUCONF_PORTTYPE_STDIN)
	    usysdep_detach ();

	  /* If a port was given, we loop forever.  */
	  fendless = TRUE;
	}

      if (fconn_lock (&sconn, TRUE))
	flocked = TRUE;
      else
	{
	  flocked = FALSE;
	  ulog (LOG_ERROR, "%s: Port already locked",
		qport->uuconf_zname);
	  fret = FALSE;
	}

      if (fret)
	{
	  if (! fconn_open (&sconn, (long) 0, (long) 0, TRUE))
	    fret = FALSE;
	  qConn = &sconn;
	}

      if (fret)
	{
	  if (fendless)
	    {
	      while (! FGOT_SIGNAL ()
		     && flogin_prompt (puuconf, &sconn))
		{
		  /* Now ignore any SIGHUP that we got.  */
		  afSignal[INDEXSIG_SIGHUP] = FALSE;

		  if (fLocked_system)
		    {
		      (void) fsysdep_unlock_system (&sLocked_system);
		      fLocked_system = FALSE;
		    }
		  if (! fconn_reset (&sconn))
		    break;
		}
	      fret = FALSE;
	    }
	  else
	    {
	      if (flogin)
		fret = flogin_prompt (puuconf, &sconn);
	      else
		{
#if DEBUG > 1
		  iholddebug = iDebug;
#endif
		  fret = faccept_call (puuconf, zsysdep_login_name (),
				       &sconn, &zsystem);
#if DEBUG > 1
		  iDebug = iholddebug;
#endif
		}
	    }
	}

      if (qConn != NULL)
	{
	  if (! fconn_close (&sconn, puuconf, (struct uuconf_dialer *) NULL,
			     fret))
	    fret = FALSE;
	  qConn = NULL;
	}

      if (flocked)
	(void) fconn_unlock (&sconn);

      if (fLocked_system)
	{
	  (void) fsysdep_unlock_system (&sLocked_system);
	  fLocked_system = FALSE;
	}

      uconn_free (&sconn);
    }

  ulog_close ();
  ustats_close ();

  /* If we got a SIGTERM, perhaps because the system is going down,
     don't run uuxqt.  We go ahead and run it for any other signal,
     since I think they indicate more temporary conditions.  */
  if (afSignal[INDEXSIG_SIGTERM])
    fuuxqt = FALSE;

  if (fuuxqt)
    {
      /* Detach from the controlling terminal before starting up uuxqt,
	 so that it runs as a true daemon.  */
      if (fdetach)
	usysdep_detach ();
      if (zsystem == NULL)
	fret = fsysdep_run (FALSE, "uuxqt", (const char *) NULL,
			    (const char *) NULL);
      else
	fret = fsysdep_run (FALSE, "uuxqt", "-s", zsystem);
    }

  usysdep_exit (fret);

  /* Avoid complaints about not returning.  */
  return 0;
}

/* Print out a usage message.  */

static void
uusage ()
{
  fprintf (stderr,
	   "Taylor UUCP version %s, copyright (C) 1991, 1992 Ian Lance Taylor\n",
	   VERSION);
  fprintf (stderr,
	   "Usage: uucico [options]\n");
  fprintf (stderr,
	   " -s,-S system: Call system (-S implies -f)\n");
  fprintf (stderr,
	   " -f: Force call despite system status\n");
  fprintf (stderr,
	   " -r state: 1 for master, 0 for slave (default)\n");
  fprintf (stderr,
	   " -p port: Specify port (implies -e)\n");
  fprintf (stderr,
	   " -l: prompt for login name and password\n");
  fprintf (stderr,
	   " -e: Endless loop of login prompts and daemon execution\n");
  fprintf (stderr,
	   " -w: After calling out, wait for incoming calls\n");
  fprintf (stderr,
	   " -q: Don't start uuxqt when done\n");
  fprintf (stderr,
	   " -x,-X debug: Set debugging level\n");
#if HAVE_TAYLOR_CONFIG
  fprintf (stderr,
	   " -I file: Set configuration file to use\n");
#endif /* HAVE_TAYLOR_CONFIG */

  exit (EXIT_FAILURE);
}

/* This function is called when a LOG_FATAL error occurs.  */

static void
uabort ()
{
  if (fLocked_system)
    ustats_failed (&sLocked_system);

  ulog_user ((const char *) NULL);

  if (qConn != NULL)
    {
      (void) fconn_close (qConn, pUuconf, (struct uuconf_dialer *) NULL,
			  FALSE);
      (void) fconn_unlock (qConn);
      uconn_free (qConn);
    }

  if (fLocked_system)
    {
      (void) fsysdep_unlock_system (&sLocked_system);
      fLocked_system = FALSE;
    }

  ulog_system ((const char *) NULL);

  ulog_close ();
  ustats_close ();

  usysdep_exit (FALSE);
}

/* Call another system, trying all the possible sets of calling
   instructions.  The qsys argument is the system to call.  The qport
   argument is the port to use, and may be NULL.  If the fifwork
   argument is TRUE, the call is only placed if there is work to be
   done.  If the fforce argument is TRUE, a call is forced even if not
   enough time has passed since the last failed call.  If the
   ftimewarn argument is TRUE (the normal case), then a warning is
   given if calls are not permitted at this time.  */

static boolean
fcall (puuconf, qorigsys, qport, fifwork, fforce, fdetach, ftimewarn)
     pointer puuconf;
     const struct uuconf_system *qorigsys;
     struct uuconf_port *qport;
     boolean fifwork;
     boolean fforce;
     boolean fdetach;
     boolean ftimewarn;
{
  struct sstatus sstat;
  struct sdaemon sdaem;
  boolean fbadtime, fnevertime;
  const struct uuconf_system *qsys;

  if (! fsysdep_get_status (qorigsys, &sstat, (boolean *) NULL))
    return FALSE;

  /* Make sure it's been long enough since the last failed call.  */
  if (! fforce)
    {
#ifdef CMAXRETRIES
#if CMAXRETRIES > 0
      if (sstat.cretries >= CMAXRETRIES)
	{
	  ulog (LOG_ERROR, "Too many retries");
	  return FALSE;
	}
#endif /* CMAXRETRIES > 0 */
#endif /* defined (CMAXRETRIES) */

      if (sstat.ttype != STATUS_COMPLETE
	  && sstat.ilast + sstat.cwait > isysdep_time ((long *) NULL))
	{
	  ulog (LOG_NORMAL, "Retry time not reached");
	  return FALSE;
	}
    }

  sdaem.puuconf = puuconf;
  sdaem.qsys = NULL;
  sdaem.zlocalname = NULL;
  sdaem.qconn = NULL;
  sdaem.qproto = NULL;
  sdaem.clocal_size = -1;
  sdaem.cremote_size = -1;
  sdaem.cmax_ever = -2;
  sdaem.cmax_receive = -1;
  sdaem.fnew = FALSE;
  sdaem.fhangup = FALSE;
  sdaem.fmaster = TRUE;
  sdaem.fcaller = TRUE;
  sdaem.fhalfduplex = FALSE;
  sdaem.bgrade = '\0';

  fbadtime = TRUE;
  fnevertime = TRUE;

  for (qsys = qorigsys; qsys != NULL; qsys = qsys->uuconf_qalternate)
    {
      int cretry;
      boolean fany, fret, fcalled;

      if (FGOT_SIGNAL ())
	return FALSE;

      if (! qsys->uuconf_fcall || qsys->uuconf_qtimegrade == NULL)
	continue;

      fnevertime = FALSE;

      /* Make sure this is a legal time to call.  */
      if (! ftimespan_match (qsys->uuconf_qtimegrade, (long *) NULL,
			     &cretry))
	continue;

      sdaem.qsys = qsys;

      /* Queue up any work there is to do.  */
      if (! fqueue (&sdaem, &fany))
	return FALSE;

      /* If we are only supposed to call if there is work, and there
	 isn't any work, check the next alternates.  We can't give up
	 at this point because there might be some other alternates
	 with fewer restrictions on grade or file transfer size.  */
      if (fifwork && ! fany)
	continue;

      fbadtime = FALSE;

      fret = fconn_call (&sdaem, qport, &sstat, cretry, &fcalled);
      if (fret)
	return TRUE;
      if (fcalled)
	return FALSE;

      /* Now we have to dump that port so that we can aquire a new
	 one.  On Unix this means that we will fork and get a new
	 process ID, so we must unlock and relock the system.  */
      if (fdetach)
	{
	  (void) fsysdep_unlock_system (&sLocked_system);
	  fLocked_system = FALSE;
	  usysdep_detach ();
	  if (! fsysdep_lock_system (&sLocked_system))
	    return FALSE;
	  fLocked_system = TRUE;
	}
    }

  if (fbadtime && ftimewarn)
    {
      ulog (LOG_NORMAL, "Wrong time to call");

      /* Update the status, unless the system can never be called.  If
	 the system can never be called, there is little point to
	 putting in a ``wrong time to call'' message.  We don't change
	 the number of retries, although we do set the wait until the
	 next retry to 0.  */
      if (! fnevertime)
	{
	  sstat.ttype = STATUS_WRONG_TIME;
	  sstat.ilast = isysdep_time ((long *) NULL);
	  sstat.cwait = 0;
	  (void) fsysdep_set_status (qorigsys, &sstat);
	}
    }

  return FALSE;
}

/* Find a port to use when calling a system, open a connection, and
   dial the system.  The actual call is done in fdo_call.  This
   routine is responsible for opening and closing the connection.  */

static boolean
fconn_call (qdaemon, qport, qstat, cretry, pfcalled)
     struct sdaemon *qdaemon;
     struct uuconf_port *qport;
     struct sstatus *qstat;
     int cretry;
     boolean *pfcalled;
{
  pointer puuconf;
  const struct uuconf_system *qsys;
  struct uuconf_port sport;
  struct sconnection sconn;
  enum tstatus_type terr;
  boolean fret;

  puuconf = qdaemon->puuconf;
  qsys = qdaemon->qsys;

  *pfcalled = FALSE;

  /* If no port was specified on the command line, use any port
     defined for the system.  To select the system port: 1) see if
     port information was specified directly; 2) see if a port was
     named; 3) get an available port given the baud rate.  We don't
     change the system status if a port is unavailable; i.e. we don't
     force the system to wait for the retry time.  */
  if (qport == NULL)
    qport = qsys->uuconf_qport;
  if (qport != NULL)
    {
      if (! fconn_init (qport, &sconn))
	return FALSE;
      if (! fconn_lock (&sconn, FALSE))
	{
	  ulog (LOG_ERROR, "%s: Port already locked",
		qport->uuconf_zname);
	  return FALSE;
	}
    }
  else
    {
      struct spass s;
      int iuuconf;

      s.fmatched = FALSE;
      s.flocked = FALSE;
      s.qconn = &sconn;
      iuuconf = uuconf_find_port (puuconf, qsys->uuconf_zport,
				  qsys->uuconf_ibaud,
				  qsys->uuconf_ihighbaud,
				  iuport_lock, (pointer) &s,
				  &sport);
      if (iuuconf == UUCONF_NOT_FOUND)
	{
	  if (s.fmatched)
	    ulog (LOG_ERROR, "All matching ports in use");
	  else
	    ulog (LOG_ERROR, "No matching ports");
	  return FALSE;
	}
      else if (iuuconf != UUCONF_SUCCESS)
	{
	  ulog_uuconf (LOG_ERROR, puuconf, iuuconf);
	  if (s.flocked)
	    {
	      (void) fconn_unlock (&sconn);
	      uconn_free (&sconn);
	    }
	  return FALSE;
	}
    }

  if (! fconn_open (&sconn, qsys->uuconf_ibaud, qsys->uuconf_ihighbaud,
		    FALSE))
    {
      terr = STATUS_PORT_FAILED;
      fret = FALSE;
    }
  else
    {
      struct uuconf_dialer *qdialer;
      struct uuconf_dialer sdialer;
      enum tdialerfound tdialer;

      if (qsys->uuconf_zalternate == NULL)
	ulog (LOG_NORMAL, "Calling system %s (port %s)", qsys->uuconf_zname,
	      zLdevice == NULL ? (char *) "unknown" : zLdevice);
      else
	ulog (LOG_NORMAL, "Calling system %s (alternate %s, port %s)",
	      qsys->uuconf_zname, qsys->uuconf_zalternate,
	  zLdevice == NULL ? (char *) "unknown" : zLdevice);

      qdialer = NULL;

      if (! fconn_dial (&sconn, puuconf, qsys, qsys->uuconf_zphone,
			&sdialer, &tdialer))
	{
	  terr = STATUS_DIAL_FAILED;
	  fret = FALSE;
	}
      else
	{
	  qdaemon->qconn = &sconn;
	  if (tdialer == DIALERFOUND_FALSE)
	    qdialer = NULL;
	  else
	    qdialer = &sdialer;
	  fret = fdo_call (qdaemon, qstat, qdialer, pfcalled, &terr);
	}

      (void) fconn_close (&sconn, puuconf, qdialer, fret);

      if (tdialer == DIALERFOUND_FREE)
	(void) uuconf_dialer_free (puuconf, &sdialer);
    }

  if (! fret)
    {
      DEBUG_MESSAGE2 (DEBUG_HANDSHAKE, "Call failed: %d (%s)",
		      (int) terr, azStatus[(int) terr]);
      qstat->ttype = terr;
      qstat->cretries++;
      qstat->ilast = isysdep_time ((long *) NULL);
      if (cretry == 0)
	qstat->cwait = CRETRY_WAIT (qstat->cretries);
      else
	qstat->cwait = cretry * 60;
      (void) fsysdep_set_status (qsys, qstat);
    }

  (void) fconn_unlock (&sconn);
  uconn_free (&sconn);

  if (qport == NULL)
    (void) uuconf_port_free (puuconf, &sport);

  return fret;
}

/* Do the actual work of calling another system.  The qsys argument is
   the system to call, the qconn argument is the connection to use,
   the qstat argument holds the current status of the ssystem, and the
   qdialer argument holds the dialer being used (it may be NULL).  If
   we log in successfully, set *pfcalled to TRUE; this is used to
   distinguish a failed dial from a failure during the call.  If an
   error occurs *pterr is set to the status type to record.  */

static boolean
fdo_call (qdaemon, qstat, qdialer, pfcalled, pterr)
     struct sdaemon *qdaemon;
     struct sstatus *qstat;
     const struct uuconf_dialer *qdialer;
     boolean *pfcalled;
     enum tstatus_type *pterr;
{
  pointer puuconf;
  const struct uuconf_system *qsys;
  struct sconnection *qconn;
  const char *zport;
  int iuuconf;
  char *zstr;
  long istart_time;

  puuconf = qdaemon->puuconf;
  qsys = qdaemon->qsys;
  qconn = qdaemon->qconn;

  *pterr = STATUS_LOGIN_FAILED;

  if (qconn->qport == NULL)
    zport = "unknown";
  else
    zport = qconn->qport->uuconf_zname;
  if (! fchat (qconn, puuconf, &qsys->uuconf_schat, qsys,
	       (const struct uuconf_dialer *) NULL,
	       (const char *) NULL, FALSE, zport,
	       iconn_baud (qconn)))
    return FALSE;

  qstat->ttype = STATUS_TALKING;
  qstat->ilast = isysdep_time ((long *) NULL);
  qstat->cretries = 0;
  qstat->cwait = 0;
  if (! fsysdep_set_status (qsys, qstat))
    return FALSE;

  ulog (LOG_NORMAL, "Login successful");

  *pfcalled = TRUE;
  istart_time = isysdep_time ((long *) NULL);

  *pterr = STATUS_HANDSHAKE_FAILED;

  /* We should now see "Shere" from the other system.  Newer systems
     send "Shere=foo" where foo is the remote name.  */
  zstr = zget_uucp_cmd (qconn, TRUE);
  if (zstr == NULL)
    return FALSE;

  if (strncmp (zstr, "Shere", 5) != 0)
    {
      ulog (LOG_ERROR, "Bad initialization string");
      ubuffree (zstr);
      return FALSE;
    }

  if (zstr[5] == '=')
    {
      const char *zheresys;
      size_t clen;
      int icmp;

      /* Some UUCP packages only provide seven characters in the Shere
	 machine name.  */
      zheresys = zstr + 6;
      clen = strlen (zheresys);
      if (clen == 7)
	icmp = strncmp (zheresys, qsys->uuconf_zname, 7);
      else
	icmp = strcmp (zheresys, qsys->uuconf_zname);
      if (icmp != 0)
	{
	  if (qsys->uuconf_pzalias != NULL)
	    {
	      char **pz;

	      for (pz = qsys->uuconf_pzalias; *pz != NULL; pz++)
		{
		  if (clen == 7)
		    icmp = strncmp (zheresys, *pz, 7);
		  else
		    icmp = strcmp (zheresys, *pz);
		  if (icmp == 0)
		    break;
		}
	    }
	  if (icmp != 0)
	    {
	      ulog (LOG_ERROR, "Called wrong system (%s)", zheresys);
	      ubuffree (zstr);
	      return FALSE;
	    }
	}
    }
#if DEBUG > 1
  else if (zstr[5] != '\0')
    DEBUG_MESSAGE1 (DEBUG_HANDSHAKE,
		    "fdo_call: Strange Shere: %s", zstr);
#endif

  ubuffree (zstr);

  /* We now send "S" name switches, where name is our UUCP name.  If
     we are using sequence numbers with this system, we send a -Q
     argument with the sequence number.  If the call-timegrade command
     was used, we send a -p argument and a -vgrade= argument with the
     grade to send us (we send both argument to make it more likely
     that one is recognized).  We always send a -N (for new) switch to
     indicate that we are prepared to accept file sizes.  */
  {
    long ival;
    char bgrade;
    char *zsend;
    boolean fret;

    /* Determine the grade we should request of the other system.  A
       '\0' means that no restrictions have been made.  */
    if (! ftimespan_match (qsys->uuconf_qcalltimegrade, &ival,
			   (int *) NULL))
      bgrade = '\0';
    else
      bgrade = (char) ival;

    /* Determine the name we will call ourselves.  */
    if (qsys->uuconf_zlocalname != NULL)
      qdaemon->zlocalname = qsys->uuconf_zlocalname;
    else
      {
	iuuconf = uuconf_localname (puuconf, &qdaemon->zlocalname);
	if (iuuconf == UUCONF_NOT_FOUND)
	  {
	    qdaemon->zlocalname = zsysdep_localname ();
	    if (qdaemon->zlocalname == NULL)
	      return FALSE;
	  }
	else if (iuuconf != UUCONF_SUCCESS)
	  {
	    ulog_uuconf (LOG_ERROR, puuconf, iuuconf);
	    return FALSE;
	  }
      }	    

    zsend = zbufalc (strlen (qdaemon->zlocalname) + 70);
    if (! qsys->uuconf_fsequence)
      {
	if (bgrade == '\0')
	  sprintf (zsend, "S%s -N", qdaemon->zlocalname);
	else
	  sprintf (zsend, "S%s -p%c -vgrade=%c -N", qdaemon->zlocalname,
		   bgrade, bgrade);
      }
    else
      {
	long iseq;

	iseq = isysdep_get_sequence (qsys);
	if (iseq < 0)
	  return FALSE;
	if (bgrade == '\0')
	  sprintf (zsend, "S%s -Q%ld -N", qdaemon->zlocalname, iseq);
	else
	  sprintf (zsend, "S%s -Q%ld -p%c -vgrade=%c -N",
		   qdaemon->zlocalname, iseq, bgrade, bgrade);
      }

    fret = fsend_uucp_cmd (qconn, zsend);
    ubuffree (zsend);
    if (! fret)
	return FALSE;
  }

  /* Now we should see ROK or Rreason where reason gives a cryptic
     reason for failure.  If we are talking to a counterpart, we will
     get back ROKN.  */
  zstr = zget_uucp_cmd (qconn, TRUE);
  if (zstr == NULL)
    return FALSE;

  if (zstr[0] != 'R')
    {
      ulog (LOG_ERROR, "Bad reponse to handshake string (%s)",
	    zstr);
      ubuffree (zstr);
      return FALSE;
    }

  if (strcmp (zstr + 1, "OKN") == 0)
    qdaemon->fnew = TRUE;
  else if (strcmp (zstr + 1, "OK") == 0)
    qdaemon->fnew = FALSE;
  else if (strcmp (zstr + 1, "CB") == 0)
    {
      ulog (LOG_NORMAL, "Remote system will call back");
      qstat->ttype = STATUS_COMPLETE;
      (void) fsysdep_set_status (qsys, qstat);
      ubuffree (zstr);
      return TRUE;
    }
  else
    {
      ulog (LOG_ERROR, "Handshake failed (%s)", zstr + 1);
      ubuffree (zstr);
      return FALSE;
    }

  ubuffree (zstr);

  /* The slave should now send \020Pprotos\0 where protos is a list of
     supported protocols.  Each protocol is a single character.  */
  zstr = zget_uucp_cmd (qconn, TRUE);
  if (zstr == NULL)
    return FALSE;

  if (zstr[0] != 'P')
    {
      ulog (LOG_ERROR, "Bad protocol handshake (%s)", zstr);
      ubuffree (zstr);
      return FALSE;
    }

  /* Now decide which protocol to use.  The system and the port may
     have their own list of protocols.  */
  {
    int i;
    char ab[5];

    i = CPROTOCOLS;
    if (qsys->uuconf_zprotocols != NULL
	|| (qconn->qport != NULL
	    && qconn->qport->uuconf_zprotocols != NULL))
      {
	const char *zproto;

	if (qsys->uuconf_zprotocols != NULL)
	  zproto = qsys->uuconf_zprotocols;
	else
	  zproto = qconn->qport->uuconf_zprotocols;
	for (; *zproto != '\0'; zproto++)
	  {
	    if (strchr (zstr + 1, *zproto) != NULL)
	      {
		for (i = 0; i < CPROTOCOLS; i++)
		  if (asProtocols[i].bname == *zproto)
		    break;
		if (i < CPROTOCOLS)
		  break;
	      }
	  }
      }
    else
      {
	int ir;

	/* If neither the system nor the port specified a list of
	   protocols, we want only protocols that match the known
	   reliability of the dialer and the port.  If we have no
	   reliability information, we default to a reliable eight bit
	   connection.  */
	ir = 0;
	if (qconn->qport != NULL
	    && (qconn->qport->uuconf_ireliable
		& UUCONF_RELIABLE_SPECIFIED) != 0)
	  ir = qconn->qport->uuconf_ireliable;
	if (qdialer != NULL
	    && (qdialer->uuconf_ireliable & UUCONF_RELIABLE_SPECIFIED) != 0)
	  {
	    if (ir != 0)
	      ir &= qdialer->uuconf_ireliable;
	    else
	      ir = qdialer->uuconf_ireliable;
	  }
	if (ir == 0)
	  ir = (UUCONF_RELIABLE_RELIABLE
		| UUCONF_RELIABLE_EIGHT
		| UUCONF_RELIABLE_SPECIFIED);

	for (i = 0; i < CPROTOCOLS; i++)
	  {
	    int ipr;

	    ipr = asProtocols[i].ireliable;
	    if ((ipr & ir) != ipr)
	      continue;
	    if (strchr (zstr + 1, asProtocols[i].bname) != NULL)
	      break;
	  }
      }

    ubuffree (zstr);

    if (i >= CPROTOCOLS)
      {
	(void) fsend_uucp_cmd (qconn, "UN");
	ulog (LOG_ERROR, "No mutually supported protocols");
	return FALSE;
      }

    qdaemon->qproto = &asProtocols[i];

    sprintf (ab, "U%c", qdaemon->qproto->bname);
    if (! fsend_uucp_cmd (qconn, ab))
      return FALSE;
  }

  /* Run any protocol parameter commands.  */
  if (qdaemon->qproto->qcmds != NULL)
    {
      if (qsys->uuconf_qproto_params != NULL)
	uapply_proto_params (puuconf, qdaemon->qproto->bname,
			     qdaemon->qproto->qcmds,
			     qsys->uuconf_qproto_params);
      if (qconn->qport != NULL
	  && qconn->qport->uuconf_qproto_params != NULL)
	uapply_proto_params (puuconf, qdaemon->qproto->bname,
			     qdaemon->qproto->qcmds,
			     qconn->qport->uuconf_qproto_params);
      if (qdialer != NULL
	  && qdialer->uuconf_qproto_params != NULL)
	uapply_proto_params (puuconf, qdaemon->qproto->bname,
			     qdaemon->qproto->qcmds,
			     qdialer->uuconf_qproto_params);
    }

  /* Turn on the selected protocol.  */
  if (! (*qdaemon->qproto->pfstart) (qdaemon, TRUE))
    return FALSE;

  /* Now we have succesfully logged in as the master.  */
  ulog (LOG_NORMAL, "Handshake successful");

  *pterr = STATUS_FAILED;

  {
    boolean fret;
    long iend_time;

    fret = floop (qdaemon);

    /* Now send the hangup message.  As the caller, we send six O's
       and expect to receive seven O's.  We send the six O's twice
       to help the other side.  We don't worry about errors here.  */
    if (fsend_uucp_cmd (qconn, "OOOOOO")
	&& fsend_uucp_cmd (qconn, "OOOOOO"))
      {
	/* We don't even look for the hangup string from the other
	   side unless we're in debugging mode.  */
#if DEBUG > 1
	if (fret && FDEBUGGING (DEBUG_HANDSHAKE))
	  {
	    zstr = zget_uucp_cmd (qconn, FALSE);
	    if (zstr != NULL)
	      {
		/* The Ultrix UUCP only sends six O's, although I
		   think it should send seven.  Because of this, we
		   only check for six.  */
		if (strstr (zstr, "OOOOOO") == NULL)
		  ulog (LOG_DEBUG, "No hangup from remote");
		ubuffree (zstr);
	      }
	  }
#endif
      }

    iend_time = isysdep_time ((long *) NULL);

    ulog (LOG_NORMAL, "Call complete (%ld seconds)",
	  iend_time - istart_time);

    if (fret)
      {
	qstat->ttype = STATUS_COMPLETE;
	qstat->ilast = iend_time;
	(void) fsysdep_set_status (qsys, qstat);
      }

    return fret;
  }
}

/* This routine is called via uuconf_find_port when a matching port is
   found.  It tries to lock the port.  If it fails, it returns
   UUCONF_NOT_FOUND to force uuconf_find_port to continue searching
   for the next matching port.  */

static int
iuport_lock (qport, pinfo)
     struct uuconf_port *qport;
     pointer pinfo;
{
  struct spass *q = (struct spass *) pinfo;

  q->fmatched = TRUE;

  if (! fconn_init (qport, q->qconn))
    return UUCONF_NOT_FOUND;
  else if (! fconn_lock (q->qconn, FALSE))
    {
      uconn_free (q->qconn);
      return UUCONF_NOT_FOUND;
    }
  else
    {
      q->flocked = TRUE;
      return UUCONF_SUCCESS;
    }
}

/* Prompt for a login name and a password, and run as the slave.  */

static boolean
flogin_prompt (puuconf, qconn)
     pointer puuconf;
     struct sconnection *qconn;
{
  char *zuser, *zpass;
  boolean fret;
  int iuuconf;

  DEBUG_MESSAGE0 (DEBUG_HANDSHAKE, "flogin_prompt: Waiting for login");

  zuser = NULL;
  do
    {
      ubuffree (zuser);
      if (! fconn_write (qconn, "login: ", sizeof "login: " - 1))
	return FALSE;
      zuser = zget_typed_line (qconn);
    }
  while (zuser != NULL && *zuser == '\0');

  if (zuser == NULL)
    return TRUE;

  if (! fconn_write (qconn, "Password:", sizeof "Password:" - 1))
    {
      ubuffree (zuser);
      return FALSE;
    }

  zpass = zget_typed_line (qconn);
  if (zpass == NULL)
    {
      ubuffree (zuser);
      return TRUE;
    }

  fret = TRUE;

  iuuconf = uuconf_callin (puuconf, zuser, zpass);
  ubuffree (zpass);
  if (iuuconf == UUCONF_NOT_FOUND)
    ulog (LOG_ERROR, "Bad login");
  else if (iuuconf != UUCONF_SUCCESS)
    {
      ulog_uuconf (LOG_ERROR, puuconf, iuuconf);
      fret = FALSE;
    }
  else
    {
#if DEBUG > 1
      int iholddebug;
#endif

      /* We ignore the return value of faccept_call because we really
	 don't care whether the call succeeded or not.  We are going
	 to reset the port anyhow.  */
#if DEBUG > 1
      iholddebug = iDebug;
#endif
      (void) faccept_call (puuconf, zuser, qconn, (const char **) NULL);
#if DEBUG > 1
      iDebug = iholddebug;
#endif
    }

  ubuffree (zuser);

  return fret;
}

/* Accept a call from a remote system.  If pqsys is not NULL, *pqsys
   will be set to the system that called in if known.  */

static boolean
faccept_call (puuconf, zlogin, qconn, pzsystem)
     pointer puuconf;
     const char *zlogin;
     struct sconnection *qconn;
     const char **pzsystem;
{
  long istart_time;
  const char *zport;
  struct uuconf_port *qport;
  struct uuconf_port sport;
  int iuuconf;
  struct uuconf_dialer *qdialer;
  struct uuconf_dialer sdialer;
  boolean ftcp_port;
  char *zsend, *zspace;
  boolean fret;
  char *zstr;
  struct uuconf_system ssys;
  const struct uuconf_system *qsys;
  const struct uuconf_system *qany;
  struct sdaemon sdaem;
  char *zloc;
  struct sstatus sstat;
  boolean frestart;

  if (pzsystem != NULL)
    *pzsystem = NULL;

  ulog (LOG_NORMAL, "Incoming call (login %s port %s)", zlogin,
	zLdevice == NULL ? (char *) "unknown" : zLdevice);

  istart_time = isysdep_time ((long *) NULL);

  /* Figure out protocol parameters determined by the port.  If no
     port was specified we're reading standard input, so try to get
     the port name and read information from the port file.  We only
     use the port information to get protocol parameters; we don't
     want to start treating the port as though it were a modem, for
     example.  */
  if (qconn->qport != NULL)
    {
      qport = qconn->qport;
      zport = qport->uuconf_zname;
      ftcp_port = FALSE;
    }
  else
    {
      zport = zsysdep_port_name (&ftcp_port);
      if (zport == NULL)
	{
	  qport = NULL;
	  zport = "unknown";
	}
      else
	{
	  iuuconf = uuconf_find_port (puuconf, zport, (long) 0, (long) 0,
				      (int (*) P((struct uuconf_port *,
						  pointer pinfo))) NULL,
				      (pointer) NULL,
				      &sport);
	  if (iuuconf == UUCONF_NOT_FOUND)
	    qport = NULL;
	  else if (iuuconf != UUCONF_SUCCESS)
	    {
	      ulog_uuconf (LOG_ERROR, puuconf, iuuconf);
	      return FALSE;
	    }
	  else
	    qport = &sport;
	}
    }

  /* If we've managed to figure out that this is a modem port, now try
     to get protocol parameters from the dialer.  */
  qdialer = NULL;
  if (qport != NULL)
    {
      if (qport->uuconf_ttype == UUCONF_PORTTYPE_MODEM)
	{
	  if (qport->uuconf_u.uuconf_smodem.uuconf_pzdialer != NULL)
	    {
	      const char *zdialer;

	      zdialer = qport->uuconf_u.uuconf_smodem.uuconf_pzdialer[0];
	      iuuconf = uuconf_dialer_info (puuconf, zdialer, &sdialer);
	      if (iuuconf == UUCONF_SUCCESS)
		qdialer = &sdialer;
	    }
	  else
	    qdialer = qport->uuconf_u.uuconf_smodem.uuconf_qdialer;
	}	  
      else if (qport->uuconf_ttype == UUCONF_PORTTYPE_TCP)
	ftcp_port = TRUE;
    }

  sdaem.puuconf = puuconf;
  sdaem.qsys = NULL;
  sdaem.zlocalname = NULL;
  sdaem.qconn = qconn;
  sdaem.qproto = NULL;
  sdaem.clocal_size = -1;
  sdaem.cremote_size = -1;
  sdaem.cmax_ever = -2;
  sdaem.cmax_receive = -1;
  sdaem.fnew = FALSE;
  sdaem.fhangup = FALSE;
  sdaem.fmaster = FALSE;
  sdaem.fcaller = FALSE;
  sdaem.fhalfduplex = FALSE;
  sdaem.bgrade = UUCONF_GRADE_LOW;

  /* Get the local name to use.  If uuconf_login_localname returns a
     value, it is not always freed up, although it should be.  */
  iuuconf = uuconf_login_localname (puuconf, zlogin, &zloc);
  if (iuuconf == UUCONF_SUCCESS)
    sdaem.zlocalname = zloc;
  else if (iuuconf == UUCONF_NOT_FOUND)
    {
      sdaem.zlocalname = zsysdep_localname ();
      if (sdaem.zlocalname == NULL)
	return FALSE;
    }
  else
    {
      ulog_uuconf (LOG_ERROR, puuconf, iuuconf);
      return FALSE;
    }

  /* Tell the remote system who we are.   */
  zsend = zbufalc (strlen (sdaem.zlocalname) + sizeof "Shere=");
  sprintf (zsend, "Shere=%s", sdaem.zlocalname);
  fret = fsend_uucp_cmd (qconn, zsend);
  ubuffree (zsend);
  if (! fret)
    return FALSE;

  zstr = zget_uucp_cmd (qconn, TRUE);
  if (zstr == NULL)
    return FALSE;

  if (zstr[0] != 'S')
    {
      ulog (LOG_ERROR, "Bad introduction string");
      ubuffree (zstr);
      return FALSE;
    }

  zspace = strchr (zstr, ' ');
  if (zspace != NULL)
    *zspace = '\0';

  iuuconf = uuconf_system_info (puuconf, zstr + 1, &ssys);
  if (iuuconf == UUCONF_NOT_FOUND)
    {
      if (! funknown_system (puuconf, zstr + 1, &ssys))
	{
	  (void) fsend_uucp_cmd (qconn, "RYou are unknown to me");
	  ulog (LOG_ERROR, "Call from unknown system %s", zstr + 1);
	}

#if ! HAVE_TAYLOR_CONFIG && HAVE_HDB_CONFIG
      /* We should check remote.unknown at this point.  */
#endif
    }
  else if (iuuconf != UUCONF_SUCCESS)
    {
      ulog_uuconf (LOG_ERROR, puuconf, iuuconf);
      ubuffree (zstr);
      return FALSE;
    }

  qany = NULL;
  for (qsys = &ssys; qsys != NULL; qsys = qsys->uuconf_qalternate)
    {
      if (! qsys->uuconf_fcalled)
	continue;

      if (qsys->uuconf_zcalled_login == NULL
	  || strcmp (qsys->uuconf_zcalled_login, "ANY") == 0)
	{
	  if (qany != NULL)
	    qany = qsys;
	}
      else if (strcmp (qsys->uuconf_zcalled_login, zlogin) == 0)
	break;
    }

  if (qsys == NULL && qany != NULL)
    {
      iuuconf = uuconf_validate (puuconf, qany, zlogin);
      if (iuuconf == UUCONF_SUCCESS)
	qsys = qany;
      else if (iuuconf != UUCONF_NOT_FOUND)
	{
	  ulog_uuconf (LOG_ERROR, puuconf, iuuconf);
	  ubuffree (zstr);
	  return FALSE;
	}
    }

  if (qsys == NULL)
    {
      (void) fsend_uucp_cmd (qconn, "RLOGIN");
      ulog (LOG_ERROR, "System %s used wrong login name %s",
	    zstr + 1, zlogin);
      ubuffree (zstr);
      return FALSE;
    }

  ubuffree (zstr);

  sdaem.qsys = qsys;

  if (pzsystem != NULL)
    *pzsystem = zbufcpy (qsys->uuconf_zname);

  ulog_system (qsys->uuconf_zname);

#if DEBUG > 1
  if (qsys->uuconf_zdebug != NULL)
    iDebug |= idebug_parse (qsys->uuconf_zdebug);
#endif

  /* See if we are supposed to call the system back.  This will queue
     up an empty command.  It would be better to actually call back
     directly at this point as well.  */
  if (qsys->uuconf_fcallback)
    {
      (void) fsend_uucp_cmd (qconn, "RCB");
      ulog (LOG_NORMAL, "Will call back");
      ubuffree (zsysdep_spool_commands (qsys, UUCONF_GRADE_HIGH, 0,
					(const struct scmd *) NULL));
      return TRUE;
    }

  /* We only permit one call at a time from a remote system.  Lock it.  */
  if (! fsysdep_lock_system (qsys))
    {
      (void) fsend_uucp_cmd (qconn, "RLCK");
      ulog (LOG_ERROR, "System already locked");
      return FALSE;
    }
  sLocked_system = *qsys;
  fLocked_system = TRUE;

  /* Set the system status.  We don't care what the status was before.
     We also don't want to kill the conversation just because we can't
     output the .Status file, so we ignore any errors.  */
  sstat.ttype = STATUS_TALKING;
  sstat.cretries = 0;
  sstat.ilast = isysdep_time ((long *) NULL);
  sstat.cwait = 0;
  (void) fsysdep_set_status (qsys, &sstat);

  /* Check the arguments of the remote system.  We accept -x# to set
     our debugging level and -Q# for a sequence number.  We may insist
     on a sequence number.  The -p and -vgrade= arguments are taken to
     specify the lowest job grade that we should transfer; I think
     this is the traditional meaning, but I don't know.  The -N switch
     means that we are talking to another instance of ourselves.  The
     -U switch specifies the ulimit of the remote system, which we
     treat as the maximum file size that may be sent.  The -R switch
     means that the remote system supports file restart; we don't.  */
  frestart = FALSE;

  if (zspace == NULL)
    {
      if (qsys->uuconf_fsequence)
	{
	  (void) fsend_uucp_cmd (qconn, "RBADSEQ");
	  ulog (LOG_ERROR, "No sequence number (call rejected)");
	  sstat.ttype = STATUS_FAILED;
	  (void) fsysdep_set_status (qsys, &sstat);
	  return FALSE;
	}
    }
  else
    {
      ++zspace;
      while (isspace (BUCHAR (*zspace)))
	++zspace;

      while (*zspace != '\0')
	{
	  boolean frecognized;
	  char *znext;
	  
	  frecognized = FALSE;
	  if (*zspace == '-')
	    {
	      switch (zspace[1])
		{
		case 'x':
		  frecognized = TRUE;
#if DEBUG > 1
		  {
		    int iwant;

		    iwant = (int) strtol (zspace + 2, (char **) NULL, 10);
		    if (! sdaem.fnew)
		      iwant = (1 << iwant) - 1;
		    if (qsys->uuconf_zmax_remote_debug != NULL)
		      iwant &= idebug_parse (qsys->uuconf_zmax_remote_debug);
		    if ((iDebug | iwant) != iDebug)
		      {
			iDebug |= iwant;
			ulog (LOG_NORMAL, "Setting debugging mode to 0%o",
			      iDebug);
		      }
		  }
#endif
		  break;
		case 'Q':
		  frecognized = TRUE;
		  {
		    long iseq;

		    if (! qsys->uuconf_fsequence)
		      break;
		    iseq = strtol (zspace + 2, (char **) NULL, 10);
		    if (iseq != isysdep_get_sequence (qsys))
		      {
			(void) fsend_uucp_cmd (qconn, "RBADSEQ");
			ulog (LOG_ERROR, "Out of sequence call rejected");
			sstat.ttype = STATUS_FAILED;
			(void) fsysdep_set_status (qsys, &sstat);
			return FALSE;
		      }
		  }
		  break;
		case 'p':
		  /* We don't accept a space between the -p and the
		     grade, although we should.  */
		  frecognized = TRUE;
		  if (UUCONF_GRADE_LEGAL (zspace[2]))
		    sdaem.bgrade = zspace[2];
		  break;
		case 'v':
		  if (strncmp (zspace + 1, "vgrade=",
			       sizeof "vgrade=" - 1) == 0)
		    {
		      frecognized = TRUE;
		      if (UUCONF_GRADE_LEGAL (zspace[sizeof "vgrade="]))
			sdaem.bgrade = zspace[sizeof "vgrade="];
		    }
		  break;
		case 'N':
		  frecognized = TRUE;
		  sdaem.fnew = TRUE;
		  break;
		case 'U':
		  frecognized = TRUE;
		  {
		    long c;

		    c = strtol (zspace + 2, (char **) NULL, 0);
		    if (c > 0)
		      sdaem.cmax_receive = c * (long) 512;
		  }
		  break;
		case 'R':
		  frecognized = TRUE;
		  frestart = TRUE;
		  break;
		default:
		  break;
		}
	    }

	  znext = zspace;
	  while (*znext != '\0' && ! isspace (BUCHAR (*znext)))
	    ++znext;

	  if (! frecognized)
	    {
	      size_t clen;
	      char *zcopy;

	      /* We could just use %.*s for this, but it's probably
		 not portable.  */
	      clen = znext - zspace;
	      zcopy = zbufalc (clen + 1);
	      memcpy (zcopy, zspace, clen);
	      zcopy[clen] = '\0';
	      ulog (LOG_NORMAL, "Unrecognized argument %s", zcopy);
	      ubuffree (zcopy);
	    }

	  zspace = znext;
	  while (isspace (BUCHAR (*zspace)))
	    ++zspace;
	}
    }

  /* We recognized the system, and the sequence number (if any) was
     OK.  Send an ROK, and send a list of protocols.  If we got the -N
     switch, send ROKN to confirm it.  */
  if (! fsend_uucp_cmd (qconn, sdaem.fnew ? "ROKN" : "ROK"))
    {
      sstat.ttype = STATUS_FAILED;
      (void) fsysdep_set_status (qsys, &sstat);
      return FALSE;
    }

  {
    int i;
   
    if (qsys->uuconf_zprotocols != NULL ||
	(qport != NULL && qport->uuconf_zprotocols != NULL))
      {
	const char *zprotos;

	if (qsys->uuconf_zprotocols != NULL)
	  zprotos = qsys->uuconf_zprotocols;
	else
	  zprotos = qport->uuconf_zprotocols;
	zsend = zbufalc (strlen (zprotos) + 2);
	sprintf (zsend, "P%s", zprotos);
      }
    else
      {
	char *zset;
	int ir;

	zsend = zbufalc (CPROTOCOLS + 2);
	zset = zsend;
	*zset++ = 'P';

	/* If the system did not specify a list of protocols, we want
	   only protocols that match the known reliability of the
	   dialer and the port.  If we have no information, we default
	   to a reliable eight bit connection.  */
	if (ftcp_port)
	  ir = (UUCONF_RELIABLE_SPECIFIED
		| UUCONF_RELIABLE_ENDTOEND
		| UUCONF_RELIABLE_RELIABLE
		| UUCONF_RELIABLE_EIGHT);
	else
	  {
	    ir = 0;
	    if (qport != NULL
		&& (qport->uuconf_ireliable & UUCONF_RELIABLE_SPECIFIED) != 0)
	      ir = qport->uuconf_ireliable;
	    if (qdialer != NULL
		&& (qdialer->uuconf_ireliable
		    & UUCONF_RELIABLE_SPECIFIED) != 0)
	      {
		if (ir != 0)
		  ir &= qdialer->uuconf_ireliable;
		else
		  ir = qdialer->uuconf_ireliable;
	      }
	    if (ir == 0)
	      ir = (UUCONF_RELIABLE_RELIABLE
		    | UUCONF_RELIABLE_EIGHT
		    | UUCONF_RELIABLE_SPECIFIED);
	  }

	for (i = 0; i < CPROTOCOLS; i++)
	  {
	    int ipr;

	    ipr = asProtocols[i].ireliable;
	    if ((ipr & ir) != ipr)
	      continue;
	    *zset++ = asProtocols[i].bname;
	  }
	*zset = '\0';
      }

    fret = fsend_uucp_cmd (qconn, zsend);
    ubuffree (zsend);
    if (! fret)
      {
	sstat.ttype = STATUS_FAILED;
	(void) fsysdep_set_status (qsys, &sstat);
	return FALSE;
      }
    
    /* The master will now send back the selected protocol.  */
    zstr = zget_uucp_cmd (qconn, TRUE);
    if (zstr == NULL)
      {
	sstat.ttype = STATUS_FAILED;
	(void) fsysdep_set_status (qsys, &sstat);
	return FALSE;
      }

    if (zstr[0] != 'U' || zstr[2] != '\0')
      {
	ulog (LOG_ERROR, "Bad protocol response string");
	sstat.ttype = STATUS_FAILED;
	(void) fsysdep_set_status (qsys, &sstat);
	ubuffree (zstr);
	return FALSE;
      }

    if (zstr[1] == 'N')
      {
	ulog (LOG_ERROR, "No supported protocol");
	sstat.ttype = STATUS_FAILED;
	(void) fsysdep_set_status (qsys, &sstat);
	ubuffree (zstr);
	return FALSE;
      }

    for (i = 0; i < CPROTOCOLS; i++)
      if (asProtocols[i].bname == zstr[1])
	break;

    ubuffree (zstr);

    if (i >= CPROTOCOLS)
      {
	ulog (LOG_ERROR, "No supported protocol");
	sstat.ttype = STATUS_FAILED;
	(void) fsysdep_set_status (qsys, &sstat);
	return FALSE;
      }

    sdaem.qproto = &asProtocols[i];
  }

  /* Run the chat script for when a call is received.  */

  if (! fchat (qconn, puuconf, &qsys->uuconf_scalled_chat, qsys,
	       (const struct uuconf_dialer *) NULL, (const char *) NULL,
	       FALSE, zport, iconn_baud (qconn)))
    {
      sstat.ttype = STATUS_FAILED;
      sstat.ilast = isysdep_time ((long *) NULL);
      (void) fsysdep_set_status (qsys, &sstat);
      return FALSE;
    }

  /* Run any protocol parameter commands.  */
  if (sdaem.qproto->qcmds != NULL)
    {
      if (qsys->uuconf_qproto_params != NULL)
	uapply_proto_params (puuconf, sdaem.qproto->bname,
			     sdaem.qproto->qcmds,
			     qsys->uuconf_qproto_params);
      if (qport != NULL
	  && qport->uuconf_qproto_params != NULL)
	uapply_proto_params (puuconf, sdaem.qproto->bname,
			     sdaem.qproto->qcmds,
			     qport->uuconf_qproto_params);
      if (qdialer != NULL
	  && qdialer->uuconf_qproto_params != NULL)
	uapply_proto_params (puuconf, sdaem.qproto->bname,
			     sdaem.qproto->qcmds,
			     qdialer->uuconf_qproto_params);
    }

  /* We don't need the dialer information any more.  */
  if (qdialer == &sdialer)
    (void) uuconf_dialer_free (puuconf, &sdialer);

  /* Turn on the selected protocol.  */

  if (! (*sdaem.qproto->pfstart) (&sdaem, FALSE))
    {
      sstat.ttype = STATUS_FAILED;
      sstat.ilast = isysdep_time ((long *) NULL);
      (void) fsysdep_set_status (qsys, &sstat);
      return FALSE;
    }

  /* If we using HAVE_HDB_LOGGING, then the previous ``incoming call''
     message went to the general log, since we didn't know the system
     name at that point.  In that case, we repeat the port and login
     names.  */
#if HAVE_HDB_LOGGING
  if (sdaem.bgrade == BGRADE_LOW)
    ulog (LOG_NORMAL, "Handshake successful (login %s port %s)",
	  zlogin,
	  zLdevice == NULL ? "unknown" : zLdevice);
  else
    ulog (LOG_NORMAL, "Handshake successful (login %s port %s grade %c)",
	  zlogin,
	  zLdevice == NULL ? "unknown" : zLdevice,
	  sdaem.bgrade);
#else /* ! HAVE_HDB_LOGGING */
  if (sdaem.bgrade == UUCONF_GRADE_LOW)
    ulog (LOG_NORMAL, "Handshake successful");
  else
    ulog (LOG_NORMAL, "Handshake successful (grade %c)", sdaem.bgrade);
#endif /* ! HAVE_HDB_LOGGING */

  {
    long iend_time;

    fret = floop (&sdaem);

    /* Hangup.  As the answerer, we send seven O's and expect to see
       six.  */
    if (fsend_uucp_cmd (qconn, "OOOOOOO")
	&& fsend_uucp_cmd (qconn, "OOOOOOO"))
      {
	/* We don't even look for the hangup string from the other
	   side unless we're in debugging mode.  */
#if DEBUG > 1
	if (fret && FDEBUGGING (DEBUG_HANDSHAKE))
	  {
	    zstr = zget_uucp_cmd (qconn, FALSE);
	    if (zstr != NULL)
	      {
		if (strstr (zstr, "OOOOOO") == NULL)
		  ulog (LOG_DEBUG, "No hangup from remote");
		ubuffree (zstr);
	      }
	  }
#endif
      }

    iend_time = isysdep_time ((long *) NULL);

    ulog (LOG_NORMAL, "Call complete (%ld seconds)",
	  iend_time - istart_time);

    (void) uuconf_system_free (puuconf, &ssys);
    if (qport == &sport)
      (void) uuconf_port_free (puuconf, &sport);
    xfree ((pointer) zloc);

    if (fret)
      sstat.ttype = STATUS_COMPLETE;
    else
      sstat.ttype = STATUS_FAILED;
    sstat.ilast = iend_time;
    (void) fsysdep_set_status (qsys, &sstat);

    return fret;
  }
}

/* Apply protocol parameters, once we know the protocol.  */

static void
uapply_proto_params (puuconf, bproto, qcmds, pas)
     pointer puuconf;
     int bproto;
     struct uuconf_cmdtab *qcmds;
     struct uuconf_proto_param *pas;
{
  struct uuconf_proto_param *qp;

  for (qp = pas; qp->uuconf_bproto != '\0'; qp++)
    {
      if (qp->uuconf_bproto == bproto)
	{
	  struct uuconf_proto_param_entry *qe;

	  for (qe = qp->uuconf_qentries; qe->uuconf_cargs > 0; qe++)
	    {
	      int iuuconf;

	      iuuconf = uuconf_cmd_args (puuconf, qe->uuconf_cargs,
					 qe->uuconf_pzargs, qcmds,
					 (pointer) NULL,
					 (uuconf_cmdtabfn) NULL, 0,
					 (pointer) NULL);
	      if (UUCONF_ERROR_VALUE (iuuconf) != UUCONF_SUCCESS)
		{
		  ulog (LOG_ERROR, "Error in %c protocol parameters",
			bproto);
		  ulog_uuconf (LOG_ERROR, puuconf, iuuconf);
		}
	    }

	  break;
	}
    }
}

/* Send a string to the other system beginning with a DLE
   character and terminated with a null byte.  This is only
   used when no protocol is in force.  */

static boolean
fsend_uucp_cmd (qconn, z)
     struct sconnection *qconn;
     const char *z;
{
  size_t cwrite;
  char *zalc;
  boolean fret;

  cwrite = strlen (z) + 2;

  zalc = zbufalc (cwrite);
  zalc[0] = '\020';
  memcpy (zalc + 1, z, cwrite - 1);

  fret = fconn_write (qconn, zalc, cwrite);
  ubuffree (zalc);
  return fret;
}

/* Get a UUCP command beginning with a DLE character and ending with a
   null byte.  This is only used when no protocol is in force.  This
   implementation has the potential of being seriously slow.  It also
   doesn't have any real error recovery.  The frequired argument is
   passed as TRUE if we need the string; we don't care that much if
   we're closing down the connection anyhow.  */

#define CTIMEOUT (120)
#define CSHORTTIMEOUT (10)
#define CINCREMENT (100)

static char *
zget_uucp_cmd (qconn, frequired)
     struct sconnection *qconn;
     boolean frequired;
{
  char *zalc;
  size_t calc;
  size_t cgot;
  long iendtime;
  int ctimeout;
#if DEBUG > 1
  int cchars;
  int iolddebug;
#endif

  iendtime = isysdep_time ((long *) NULL);
  if (frequired)
    iendtime += CTIMEOUT;
  else
    iendtime += CSHORTTIMEOUT;

#if DEBUG > 1
  cchars = 0;
  iolddebug = iDebug;
  if (FDEBUGGING (DEBUG_HANDSHAKE))
    {
      ulog (LOG_DEBUG_START, "zget_uucp_cmd: Got \"");
      iDebug &=~ (DEBUG_INCOMING | DEBUG_PORT);
    }
#endif

  zalc = NULL;
  calc = 0;
  cgot = -1;
  while ((ctimeout = (int) (iendtime - isysdep_time ((long *) NULL))) > 0)
    {
      int b;
      
      b = breceive_char (qconn, ctimeout, frequired);
      /* Now b == -1 on timeout, -2 on error.  */
      if (b < 0)
	{
#if DEBUG > 1
	  if (FDEBUGGING (DEBUG_HANDSHAKE))
	    {
	      ulog (LOG_DEBUG_END, "\" (%s)",
		    b == -1 ? "timeout" : "error");
	      iDebug = iolddebug;
	    }
#endif
	  if (b == -1 && frequired)
	    ulog (LOG_ERROR, "Timeout");
	  ubuffree (zalc);
	  return NULL;
	}

      /* Apparently some systems use parity on these strings, so we
	 strip the parity bit.  This may need to be configurable at
	 some point, although only if system names can have eight bit
	 characters.  */
      if (! isprint (BUCHAR (b)))
	b &= 0x7f;

#if DEBUG > 1
      if (FDEBUGGING (DEBUG_HANDSHAKE))
	{
	  char ab[5];

	  ++cchars;
	  if (cchars > 60)
	    {
	      ulog (LOG_DEBUG_END, "\"");
	      ulog (LOG_DEBUG_START, "zget_uucp_cmd: Got \"");
	      cchars = 0;
	    }
	  (void) cdebug_char (ab, b);
	  ulog (LOG_DEBUG_CONTINUE, "%s", ab);
	}
#endif

      if (cgot < 0)
	{
	  if (b != '\020')
	    continue;
	  cgot = 0;
	  continue;
	}

      /* If we see another DLE, something has gone wrong; continue
	 as though this were the first one we saw.  */
      if (b == '\020')
	{
	  cgot = 0;
	  continue;
	}

      /* Some systems send a trailing \n on the Shere line.  As far as
	 I can tell this line can never contain a \n, so this
	 modification should be safe enough.  */
      if (b == '\r' || b == '\n')
	b = '\0';

      if (cgot >= calc)
	{
	  char *znew;

	  calc += CINCREMENT;
	  znew = zbufalc (calc);
	  memcpy (znew, zalc, cgot);
	  ubuffree (zalc);
	  zalc = znew;
	}

      zalc[cgot] = (char) b;
      ++cgot;

      if (b == '\0')
	{
#if DEBUG > 1
	  if (FDEBUGGING (DEBUG_HANDSHAKE))
	    {
	      ulog (LOG_DEBUG_END, "\"");
	      iDebug = iolddebug;
	    }
#endif
	  return zalc;
	}
    }

#if DEBUG > 1
  if (FDEBUGGING (DEBUG_HANDSHAKE))
    {
      ulog (LOG_DEBUG_END, "\" (timeout)");
      iDebug = iolddebug;
    }
#endif

  ubuffree (zalc);

  if (frequired)
    ulog (LOG_ERROR, "Timeout");
  return NULL;
}

/* Read a sequence of characters up to a newline or carriage return, and
   return the line without the line terminating character.  */

static char *
zget_typed_line (qconn)
     struct sconnection *qconn;
{
  char *zalc;
  size_t calc;
  size_t cgot;

#if DEBUG > 1
  int cchars;
  int iolddebug;

  cchars = 0;
  iolddebug = iDebug;
  if (FDEBUGGING (DEBUG_CHAT))
    {
      ulog (LOG_DEBUG_START, "zget_typed_line: Got \"");
      iDebug &=~ (DEBUG_INCOMING | DEBUG_PORT);
    }
#endif

  zalc = NULL;
  calc = 0;
  cgot = 0;
  while (TRUE)
    {
      int b;
      
      b = breceive_char (qconn, CTIMEOUT, FALSE);

      /* Now b == -1 on timeout, -2 on error.  */

      if (b == -2 || FGOT_SIGNAL ())
	{
#if DEBUG > 1
	  if (FDEBUGGING (DEBUG_CHAT))
	    {
	      ulog (LOG_DEBUG_END, "\" (error)");
	      iDebug = iolddebug;
	    }
#endif
	  ubuffree (zalc);
	  return NULL;
	}

      if (b == -1)
	continue;

#if DEBUG > 1
      if (FDEBUGGING (DEBUG_CHAT))
	{
	  char ab[5];

	  ++cchars;
	  if (cchars > 60)
	    {
	      ulog (LOG_DEBUG_END, "\"");
	      ulog (LOG_DEBUG_START, "zget_typed_line: Got \"");
	      cchars = 0;
	    }
	  (void) cdebug_char (ab, b);
	  ulog (LOG_DEBUG_CONTINUE, "%s", ab);
	}
#endif

      if (cgot >= calc)
	{
	  char *znew;

	  calc += CINCREMENT;
	  znew = zbufalc (calc);
	  memcpy (znew, zalc, cgot);
	  ubuffree (zalc);
	  zalc = znew;
	}

      if (b == '\r' || b == '\n')
	b = '\0';

      zalc[cgot] = (char) b;
      ++cgot;

      if (b == '\0')
	{
#if DEBUG > 1
	  if (FDEBUGGING (DEBUG_CHAT))
	    {
	      ulog (LOG_DEBUG_END, "\"");
	      iDebug = iolddebug;
	    }
#endif
	  return zalc;
	}
    }
}
