/*
 * This file is part of hildon-welcome 
 *
 * Copyright (C) 2009 Nokia Corporation.
 * 
 * Author: Gabriel Schulhof <gabriel.schulhof@nokia.com>
 * Contact: Karoliina T. Salminen <karoliina.t.salminen@nokia.com>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "conffile.h"

#define FACTORY_CONF_FILE "default.conf"

struct _ConfFileIterator
{
  char *path;
  gboolean new;
  GDir *dir;
};

ConfFileIterator *
conf_file_iterator_new()
{
  ConfFileIterator *itr = NULL;

  if ((itr = g_new(ConfFileIterator, 1))) {
    itr->new = TRUE;
    if ((itr->path = g_build_filename(SYSCONFDIR, PACKAGE_NAME ".d", NULL)) != NULL) {
      if ((itr->dir = g_dir_open(itr->path, 0, NULL)) == NULL) {
        g_free(itr->path);
        g_free(itr);
        itr = NULL;
      }
    }
    else {
      g_free(itr);
      itr = NULL;
    }
  }

  return itr;
}

static gboolean
read_conf_file(char *conffile, char **p_video, char **p_audio, int *p_duration)
{
  char *str = NULL;
  GKeyFile *file = NULL;

  g_debug("read_conf_file(%s)", conffile);
  if ((file = g_key_file_new())) {
    if (g_key_file_load_from_file(file, conffile, G_KEY_FILE_NONE | G_KEY_FILE_KEEP_COMMENTS, NULL)) {

      (*p_video) = g_key_file_get_string(file, PACKAGE_NAME, "filename", NULL);
      if ((*p_video) && (*p_video)[0] && (*p_video)[0] != '/')
        if ((str = g_build_filename(DATADIR, PACKAGE_NAME, "media", (*p_video), NULL)) != NULL) {
          g_free((*p_video));
          (*p_video) = str;
        }

      (*p_audio) = g_key_file_get_string(file, PACKAGE_NAME, "sound", NULL);
      if ((*p_audio) && (*p_audio)[0] && !('s' == (*p_audio)[0] && 0 == (*p_audio)[1]) && (*p_audio)[0] != '/')
        if ((str = g_build_filename(DATADIR, PACKAGE_NAME, "media", (*p_audio), NULL)) != NULL) {
          g_free((*p_audio));
          (*p_audio) = str;
        }

      (*p_duration) = g_key_file_get_integer(file, PACKAGE_NAME, "duration", NULL);

      return TRUE;
    }
    g_key_file_free(file);
  }

  return FALSE;
}

gboolean
conf_file_iterator_get(ConfFileIterator *itr, char **p_video, char **p_audio, int *p_duration)
{
  gboolean read_success = FALSE;
  const char *fname;
  char *conffile;

  if (!(p_video && p_audio && p_duration)) return FALSE;

  (*p_video) = NULL;
  (*p_audio) = NULL;
  (*p_duration) = 0;

  /* Special case: read default.conf file */
  if (itr->new) {
    if ((conffile = g_build_filename(itr->path, FACTORY_CONF_FILE, NULL)) != NULL) {
      read_success = read_conf_file(conffile, p_video, p_audio, p_duration);
      g_free(conffile);
    }
    itr->new = FALSE;
  }

  while (!read_success && (fname = g_dir_read_name(itr->dir)))
    if (strcmp(fname, FACTORY_CONF_FILE))
      if ((conffile = g_build_filename(itr->path, fname, NULL)) != NULL) {
        read_success = read_conf_file(conffile, p_video, p_audio, p_duration);
        g_free(conffile);
      }

  if (!read_success) {
    g_free((*p_video)); (*p_video) = NULL;
    g_free((*p_audio)); (*p_audio) = NULL;
    (*p_duration) = 0;
  }

  return read_success;
}

void
conf_file_iterator_destroy(ConfFileIterator *itr)
{
  if (!itr) return;
  g_dir_close(itr->dir);
  g_free(itr->path);
  g_free(itr);
}
