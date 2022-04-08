#include <_ansi.h>
#include <reent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include "local.h"

#define sscanf siscanf	/* avoid to pull in FP functions. */

#define TZNAME_MIN	3	/* POSIX min TZ abbr size local def */
#define TZNAME_MAX	10	/* POSIX max TZ abbr size local def */

static char __tzname_std[TZNAME_MAX + 1];
static char __tzname_dst[TZNAME_MAX + 1];
static char *prev_tzenv = NULL;

void
_tzset_unlocked_r (struct _reent *reent_ptr)
{
  char *tzenv;
  unsigned short hh, mm, ss, m, w, d;
  int sign, n;
  int i, ch;
  __tzinfo_type *tz = __gettzinfo ();

  if ((tzenv = _getenv_r (reent_ptr, "TZ")) == NULL)
      {
	_timezone = 0;
	_daylight = 0;
	_tzname[0] = "GMT";
	_tzname[1] = "GMT";
	free(prev_tzenv);
	prev_tzenv = NULL;
	return;
      }

  if (prev_tzenv != NULL && strcmp(tzenv, prev_tzenv) == 0)
    return;

  free(prev_tzenv);
  prev_tzenv = _malloc_r (reent_ptr, strlen(tzenv) + 1);
  if (prev_tzenv != NULL)
    strcpy (prev_tzenv, tzenv);

  /* ignore implementation-specific format specifier */
  if (*tzenv == ':')
    ++tzenv;  

  /* allow POSIX angle bracket < > quoted signed alphanumeric tz abbr e.g. <MESZ+0330> */
  if (*tzenv == '<')
    {
      ++tzenv;

      /* quit if no items, too few or too many chars, or no close quote '>' */
      if (sscanf (tzenv, "%10[-+0-9A-Za-z]%n", __tzname_std, &n) <= 0
		|| n < TZNAME_MIN || TZNAME_MAX < n || '>' != tzenv[n])
        return;

      ++tzenv;	/* bump for close quote '>' */
    }
  else
    {
      /* allow POSIX unquoted alphabetic tz abbr e.g. MESZ */
      if (sscanf (tzenv, "%10[A-Za-z]%n", __tzname_std, &n) <= 0
				|| n < TZNAME_MIN || TZNAME_MAX < n)
        return;
    }
 
  tzenv += n;

  sign = 1;
  if (*tzenv == '-')
    {
      sign = -1;
      ++tzenv;
    }
  else if (*tzenv == '+')
    ++tzenv;

  mm = 0;
  ss = 0;
 
  if (sscanf (tzenv, "%hu%n:%hu%n:%hu%n", &hh, &n, &mm, &n, &ss, &n) < 1)
    return;
  
  tz->__tzrule[0].offset = sign * (ss + SECSPERMIN * mm + SECSPERHOUR * hh);
  _tzname[0] = __tzname_std;
  tzenv += n;

  /* allow POSIX angle bracket < > quoted signed alphanumeric tz abbr e.g. <MESZ+0330> */
  if (*tzenv == '<')
    {
      ++tzenv;

      /* quit if no items, too few or too many chars, or no close quote '>' */
      if (sscanf (tzenv, "%10[-+0-9A-Za-z]%n", __tzname_dst, &n) <= 0
		|| n < TZNAME_MIN || TZNAME_MAX < n || '>' != tzenv[n])
	{ /* No dst */
	  _tzname[1] = _tzname[0];
	  _timezone = tz->__tzrule[0].offset;
	  _daylight = 0;
	  return;
	}

      ++tzenv;	/* bump for close quote '>' */
    }
  else
    {
      /* allow POSIX unquoted alphabetic tz abbr e.g. MESZ */
      if (sscanf (tzenv, "%10[A-Za-z]%n", __tzname_dst, &n) <= 0
				|| n < TZNAME_MIN || TZNAME_MAX < n)
	{ /* No dst */
	  _tzname[1] = _tzname[0];
	  _timezone = tz->__tzrule[0].offset;
	  _daylight = 0;
	  return;
	}
    }

  _tzname[1] = __tzname_dst;
  tzenv += n;

  /* otherwise we have a dst name, look for the offset */
  sign = 1;
  if (*tzenv == '-')
    {
      sign = -1;
      ++tzenv;
    }
  else if (*tzenv == '+')
    ++tzenv;

  hh = 0;  
  mm = 0;
  ss = 0;
  
  n  = 0;
  if (sscanf (tzenv, "%hu%n:%hu%n:%hu%n", &hh, &n, &mm, &n, &ss, &n) <= 0)
    tz->__tzrule[1].offset = tz->__tzrule[0].offset - 3600;
  else
    tz->__tzrule[1].offset = sign * (ss + SECSPERMIN * mm + SECSPERHOUR * hh);

  tzenv += n;

  for (i = 0; i < 2; ++i)
    {
      if (*tzenv == ',')
        ++tzenv;

      if (*tzenv == 'M')
	{
	  if (sscanf (tzenv, "M%hu%n.%hu%n.%hu%n", &m, &n, &w, &n, &d, &n) != 3 ||
	      m < 1 || m > 12 || w < 1 || w > 5 || d > 6)
	    return;
	  
	  tz->__tzrule[i].ch = 'M';
	  tz->__tzrule[i].m = m;
	  tz->__tzrule[i].n = w;
	  tz->__tzrule[i].d = d;
	  
	  tzenv += n;
	}
      else 
	{
	  char *end;
	  if (*tzenv == 'J')
	    {
	      ch = 'J';
	      ++tzenv;
	    }
	  else
	    ch = 'D';
	  
	  d = strtoul (tzenv, &end, 10);
	  
	  /* if unspecified, default to US settings */
	  /* From 1987-2006, US was M4.1.0,M10.5.0, but starting in 2007 is
	   * M3.2.0,M11.1.0 (2nd Sunday March through 1st Sunday November)  */
	  if (end == tzenv)
	    {
	      if (i == 0)
		{
		  tz->__tzrule[0].ch = 'M';
		  tz->__tzrule[0].m = 3;
		  tz->__tzrule[0].n = 2;
		  tz->__tzrule[0].d = 0;
		}
	      else
		{
		  tz->__tzrule[1].ch = 'M';
		  tz->__tzrule[1].m = 11;
		  tz->__tzrule[1].n = 1;
		  tz->__tzrule[1].d = 0;
		}
	    }
	  else
	    {
	      tz->__tzrule[i].ch = ch;
	      tz->__tzrule[i].d = d;
	    }
	  
	  tzenv = end;
	}
      
      /* default time is 02:00:00 am */
      hh = 2;
      mm = 0;
      ss = 0;
      n = 0;
      
      if (*tzenv == '/')
	sscanf (tzenv, "/%hu%n:%hu%n:%hu%n", &hh, &n, &mm, &n, &ss, &n);

      tz->__tzrule[i].s = ss + SECSPERMIN * mm + SECSPERHOUR  * hh;
      
      tzenv += n;
    }

  __tzcalc_limits (tz->__tzyear);
  _timezone = tz->__tzrule[0].offset;  
  _daylight = tz->__tzrule[0].offset != tz->__tzrule[1].offset;
}

void
_tzset_r (struct _reent *reent_ptr)
{
  TZ_LOCK;
  _tzset_unlocked_r (reent_ptr);
  TZ_UNLOCK;
}
