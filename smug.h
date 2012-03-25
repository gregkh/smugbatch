/*
 * Copyright (C) 2008 Greg Kroah-Hartman <greg@kroah.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#ifndef __SMUG_H
#define __SMUG_H

#include <curl/curl.h>
#include "list.h"

#define dbg(format, arg...)						\
	do {								\
		if (debug)						\
			printf("%s: " format , __func__ , ## arg);	\
	} while (0)

/**
 * struct album: holds the information for one album/gallery
 *
 * entry: list structure to hook it with other albums
 * id: the album id
 * key: the album key
 * title: the album title
 * number: unique number for selection
 * files: the list of files currently in this album
 */
struct album {
	struct list_head entry;
	char *id;
	char *key;
	char *title;
	int number;
	struct list_head files;
};

struct filename {
	struct list_head entry;
	char *filename;
	char *basename;
	unsigned char md5[16];
	char *id;
	char *key;
	char *caption;
	char *original_url;
};

struct session {
	char *password;
	char *email;
	char *session_id;
	char *su_cookie;
	struct list_head albums;
	struct list_head files_upload;
	struct list_head files_download;
	int quiet;
};

struct progress {
	char *filename;
	int position;
	int total;
	int upload;
	time_t start;
};

struct smug_curl_buffer {
	char *data;
	int length;
};

#define zalloc(size)	calloc(size, 1)

extern char *my_basename(char *name);
extern char *get_string_from_stdin(void);
extern struct smug_curl_buffer *smug_curl_buffer_alloc(void);
extern void smug_curl_buffer_free(struct smug_curl_buffer *buffer);
extern struct session *session_alloc(void);
extern void session_free(struct session *session);
extern size_t curl_callback(void *buffer, size_t size, size_t nmemb,
			    void *userp);
extern int curl_progress_func(void *arg_progress,
			      double dltotal, double dlnow,
			      double ultotal, double ulnow);

extern void album_list_free(struct list_head *albums);
extern void files_list_free(struct list_head *files);
extern char *find_value(const char *haystack, const char *needle,
			char **new_pos);

extern int get_session_id(struct smug_curl_buffer *buffer,
			  struct session *session);
extern int get_albums(struct smug_curl_buffer *buffer,
		      struct session *session);

extern CURL *curl_init(void);

extern int upload_file(struct session *session, struct filename *filename,
		       struct album *album, int position, int total);
extern int upload_files(struct session *session, struct album *album);
extern struct album *select_album(const char *album_title,
				  const char *category_title,
				  const char *quicksettings_name,
				  struct session *session);
extern int smug_login(struct session *session);
extern int smug_logout(struct session *session);
extern int generate_md5s(struct list_head *files);
extern char *smug_get_category_id(const char *category_title,
				  struct session *session);
extern struct album *smug_create_album(const char *album_title,
				       const char *category_title,
				       const char *quicksettings_name,
				       struct session *session);
extern int smug_get_albums(struct session *session);
extern int smug_read_images(struct session *session, struct album *album);
extern void smug_parse_configfile(struct session *session);
extern int smug_download(struct session *session, struct filename *filename);
extern char *smug_get_quicksettings_id(const char *qs_name,
				       struct session *session);

extern int debug;


#endif
