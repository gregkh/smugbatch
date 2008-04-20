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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include "list.h"
#include "md5.h"
#include "smug.h"
#include "smugbatch_version.h"



static char *api_key = "ABW1oenNznek2rD4AIiFn7OhkEkmzEIb";
static char *user_agent = "smugbatch/"SMUGBATCH_VERSION" (greg@kroah.com)";


static char *session_id_tag = "Session id";

static char *smugmug_album_list_url = "https://api.smugmug.com/hack/rest/1.2.0/?method=smugmug.albums.get&SessionID=%s&APIKey=%s";
static char *smugmug_login_url = "https://api.smugmug.com/hack/rest/1.2.0/?method=smugmug.login.withPassword&EmailAddress=%s&Password=%s&APIKey=%s";
static char *smugmug_logout_url = "https://api.smugmug.com/hack/rest/1.2.0/?method=smugmug.logout&SessionID=%s&APIKey=%s";
static char *smugmug_upload_url = "http://upload.smugmug.com/%s";


CURL *curl_init(void)
{
	CURL *curl;

	curl = curl_easy_init();
	if (!curl) {
		fprintf(stderr, "Can not init CURL!\n");
		return NULL;
	}
	curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
	return curl;
}


static char *my_basename(char *name)
{
	char *temp;
	int length = strlen(name);

	temp = &name[length];
	while (length && *temp != '/') {
		--temp;
		--length;
	}
	if (*temp == '/')
		++temp;
	return strdup(temp);
}

void album_list_free(struct list_head *albums)
{
	struct album *album;
	struct album *temp;

	list_for_each_entry_safe(album, temp, albums, entry) {
		dbg("cleaning up album %s\n", album->title);
		free(album->id);
		free(album->key);
		free(album->title);
		free(album);
	}
}

void files_list_free(struct list_head *files)
{
	struct filename *filename;
	struct filename *temp;

	list_for_each_entry_safe(filename, temp, files, entry) {
		dbg("cleaning up filename %s\n", filename->filename);
		free(filename->filename);
		free(filename->basename);
		free(filename);
	}
}

char *find_value(const char *haystack, const char *needle, char **new_pos)
{
	char *location;
	char *temp;
	char *value;

	location = strstr(haystack, needle);
	if (!location)
		return NULL;

	value = malloc(1000);
	if (!value)
		return NULL;

	location += strlen(needle);
	temp = value;
	++location;	/* '=' */
	++location;	/* '"' */
	while (*location != '"') {
		*temp = *location;
		++temp;
		++location;
	}
	*temp = '\0';
	if (new_pos)
		*new_pos = location;
	return value;
}

struct smug_curl_buffer *smug_curl_buffer_alloc(void)
{
	struct smug_curl_buffer *buffer;

	buffer = malloc(sizeof(*buffer));
	if (!buffer)
		return NULL;

	/* start out with a data buffer of 1 byte to
	 * make the buffer fill logic simpler */
	buffer->data = malloc(1);
	if (!buffer->data) {
		free(buffer);
		return NULL;
	}
	buffer->length = 0;
	buffer->data[0] = 0x00;
	return buffer;
}

void smug_curl_buffer_free(struct smug_curl_buffer *buffer)
{
	if (!buffer)
		return;
	if (buffer->data)
		free(buffer->data);
	free(buffer);
}

size_t curl_callback(void *buffer, size_t size, size_t nmemb, void *userp)
{
	struct smug_curl_buffer *curl_buf = userp;
	size_t buffer_size = size * nmemb;
	char *temp;

	if ((!buffer) || (!buffer_size) || (!curl_buf))
		return -EINVAL;

	/* add to the data we already have */
	temp = malloc(curl_buf->length + buffer_size + 1);
	if (!temp)
		return -ENOMEM;

	memcpy(temp, curl_buf->data, curl_buf->length);
	free(curl_buf->data);
	curl_buf->data = temp;
	memcpy(&curl_buf->data[curl_buf->length], (char *)buffer, buffer_size);
	curl_buf->length += buffer_size;

	/* null terminate the string as we are going to end up
	 * using string functions on the buffer */
	curl_buf->data[curl_buf->length + 1] = 0x00;

	return buffer_size;
}

int get_session_id(struct smug_curl_buffer *buffer, struct session *session)
{
	session->session_id = find_value(buffer->data, session_id_tag, NULL);
	if (!session->session_id)
		return -EINVAL;
	return 0;
}

int get_albums(struct smug_curl_buffer *buffer, struct session *session)
{
	char *temp = buffer->data;
	struct album *album;
	char *id;
	char *key;
	char *title;
	int found_one = 0;

	while (1) {
		id = find_value(temp, "Album id", &temp);
		if (!id)
			break;
		key = find_value(temp, "Key", &temp);
		if (!key)
			break;
		title = find_value(temp, "Title", &temp);
		if (!title)
			break;
		dbg("%s: %s: %s\n", id, key, title);
		album = malloc(sizeof(*album));
		album->id = id;
		album->key = key;
		album->title = title;
		list_add_tail(&album->entry, &session->albums);
		found_one++;
	}

	if (!found_one)
		return -EINVAL;
	return 0;
}

static unsigned char md5_data[100];

/* from coreutils */
static inline void *ptr_align(void const *ptr, size_t alignment)
{
	char const *p0 = ptr;
	char const *p1 = p0 + alignment - 1;
	return (void *) (p1 - (size_t) p1 % alignment);
}

static void sprintf_md5(char *string, unsigned char *md5)
{
	sprintf(string,
		"%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x"
		"%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x",
		md5[0], md5[1], md5[2], md5[3], md5[4], md5[5], md5[6],
		md5[7], md5[8], md5[9], md5[10], md5[11], md5[12], md5[13],
		md5[14], md5[15]);
}

static int generate_md5(struct list_head *files)
{
	struct filename *filename;
	FILE *fp;
	int err;
	unsigned char *md5 = ptr_align(md5_data, 4);
	char md5_string[64];

	if (!files)
		return 0;

	/* let's generate the md5 of the files we are going to upload */
	list_for_each_entry(filename, files, entry) {
		dbg("calculating md5 of %s\n", filename->filename);
		fp = fopen(filename->filename, "rb");
		if (!fp) {
			printf("Can not open %s, exiting\n",
			       filename->filename);
			return -EINVAL;
		}
		err = md5_stream(fp, &md5[0]);
		if (err) {
			printf("error generating md5 for %s, exiting\n",
			       filename->filename);
			return -err;
		}
		memcpy(filename->md5, md5, 16);
		sprintf_md5(md5_string, &md5[0]);
		dbg("md5 of %s is %s\n", filename->filename, md5_string);
		fclose(fp);
	}
	return 0;
}

int curl_progress_func(struct progress *progress,
		       double dltotal, double dlnow,
		       double ultotal, double ulnow)
{
	int now;
	int total;
	int percent;

	if (!progress)
		return -EINVAL;

	if (progress->upload) {
		now = (int)ulnow;
		total = (int)ultotal;
		percent = (int)(ulnow*100.0/ultotal);
	} else {
		now = (int)dlnow;
		total = (int)dltotal;
		percent = (int)(dlnow*100.0/dltotal);
	}
	fprintf(stdout, "      \r%d of %d: %s: %dbytes of %dbytes (%d%%)",
		progress->position, progress->total,
		progress->filename, now, total, percent);
	return 0;
}

int upload_file(struct session *session, struct filename *filename,
		struct album *album, int position, int total)
{
	struct smug_curl_buffer *buffer;
	CURL *curl;
	FILE *fd;
	int file_handle;
	struct stat file_info;
	CURLcode res;
	char buf[100];
	char url[1000];
	char md5_string[64];
	struct curl_slist *headers = NULL;
	struct progress *progress;

	buffer = smug_curl_buffer_alloc();
	if (!buffer)
		return -ENOMEM;

	progress = malloc(sizeof(*progress));
	if (!progress)
		return -ENOMEM;
	progress->filename = filename->basename;
	progress->upload = 1;
	progress->position = position;
	progress->total = total;

	curl = curl_init();
	if (!curl)
		return -EINVAL;

	file_handle = open(filename->filename, O_RDONLY);
	fstat(file_handle, &file_info);
	close(file_handle);
	fd = fopen(filename->filename, "rb");
	if (!fd)
		return -EINVAL;

	dbg("%s is %d bytes big\n", filename->filename, (int)file_info.st_size);

	sprintf(url, smugmug_upload_url, filename->basename);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(curl, CURLOPT_PUT, 1);
	curl_easy_setopt(curl, CURLOPT_READDATA, fd);
	curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
			 (curl_off_t)file_info.st_size);

	sprintf_md5(&md5_string[0], &filename->md5[0]);
	sprintf(buf, "Content-MD5: %s", md5_string);
	dbg("%s\n", buf);
	headers = curl_slist_append(headers, buf);
	headers = curl_slist_append(headers, "X-Smug-Version: 1.2.0");
	headers = curl_slist_append(headers, "X-Smug-ResponseType: REST");
	sprintf(buf, "X-Smug-SessionID: %s", session->session_id);
	dbg("%s\n", buf);
	headers = curl_slist_append(headers, buf);
	sprintf(buf, "X-Smug-AlbumID: %s", album->id);
	dbg("%s\n", buf);
	headers = curl_slist_append(headers, buf);

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, curl_progress_func);
	curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, progress);

	dbg("starting upload...\n");
	res = curl_easy_perform(curl);
	if (res)
		printf("upload error %d, exiting\n", res);

	curl_slist_free_all(headers);
	fprintf(stdout, "\n");
	fflush(stdout);
	curl_easy_cleanup(curl);
	return (int)res;
}

int upload_files(struct session *session, struct album *album)
{
	struct filename *filename;
	int num_to_upload = 0;
	int i = 0;
	int retval;

	list_for_each_entry(filename, &session->files_upload, entry)
		++num_to_upload;

	list_for_each_entry(filename, &session->files_upload, entry) {
		++i;
		retval = upload_file(session, filename, album,
				     i, num_to_upload);
		if (retval)
			return retval;
	}
	return 0;
}

int smug_login(struct session *session)
{
	char url[1000];
	struct smug_curl_buffer *curl_buf;
	CURL *curl = NULL;
	CURLcode res;
	int retval;

	if (!session)
		return -EINVAL;

	curl_buf = smug_curl_buffer_alloc();
	if (!curl_buf)
		return -ENOMEM;

	curl = curl_init();
	if (!curl)
		return -EINVAL;

	sprintf(url, smugmug_login_url, session->email,
		session->password, api_key);
	dbg("url = %s\n", url);

	curl_easy_setopt(curl, CURLOPT_URL, url);

	/* some ssl sanity checks on the connection we are making */
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);

	/* log into smugmug */
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);
	res = curl_easy_perform(curl);
	if (res) {
		printf("error(%d) trying to login\n", res);
		return -EINVAL;
	}

	retval = get_session_id(curl_buf, session);
	if (retval) {
		fprintf(stderr, "session_id was not found\n");
		return -EINVAL;
	}

	smug_curl_buffer_free(curl_buf);
	curl_easy_cleanup(curl);
	return 0;
}

int smug_logout(struct session *session)
{
	char url[1000];
	struct smug_curl_buffer *curl_buf;
	CURL *curl = NULL;
	CURLcode res;

	if (!session)
		return -EINVAL;

	curl_buf = smug_curl_buffer_alloc();
	if (!curl_buf)
		return -ENOMEM;

	curl = curl_init();
	if (!curl)
		return -EINVAL;

	sprintf(url, smugmug_logout_url, session->session_id, api_key);
	dbg("url = %s\n", url);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);
	res = curl_easy_perform(curl);
	if (res) {
		fprintf(stderr, "error(%d) trying to logout\n", res);
		return -EINVAL;
	}

	smug_curl_buffer_free(curl_buf);
	curl_easy_cleanup(curl);
	return 0;
}
