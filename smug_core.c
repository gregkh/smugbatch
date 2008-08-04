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
#include <ctype.h>
#include <alloca.h>
#include <curl/curl.h>
#include "list.h"
#include "md5.h"
#include "smug.h"
#include "smugbatch_version.h"



static char *api_key = "ABW1oenNznek2rD4AIiFn7OhkEkmzEIb";
static char *user_agent = "smugbatch/"SMUGBATCH_VERSION" (greg@kroah.com)";


static char *session_id_tag = "Session id";

static char *smugmug_album_list_url =
	"https://api.smugmug.com/hack/rest/1.2.0/?"
		"method=smugmug.albums.get"
		"&SessionID=%s&APIKey=%s";
static char *smugmug_album_create_url =
	"https://api.smugmug.com/hack/rest/1.2.0/?"
		"method=smugmug.albums.create"
		"&SessionID=%s&Title=%s&CategoryID=%s&AlbumTemplateID=%s";
static char *smugmug_category_list_url =
	"https://api.smugmug.com/hack/rest/1.2.0/?"
		"method=smugmug.categories.get"
		"&SessionID=%s";
static char *smugmug_quicksettings_list_url =
	"https://api.smugmug.com/hack/rest/1.2.0/?"
		"method=smugmug.albumtemplates.get"
		"&SessionID=%s";
static char *smugmug_login_url =
	"https://api.smugmug.com/hack/rest/1.2.0/?"
		"method=smugmug.login.withPassword"
		"&EmailAddress=%s&Password=%s&APIKey=%s";
static char *smugmug_logout_url =
	"https://api.smugmug.com/hack/rest/1.2.0/?"
		"method=smugmug.logout"
		"&SessionID=%s&APIKey=%s";
static char *smugmug_image_list_url =
	"https://api.smugmug.com/hack/rest/1.2.0/?"
		"method=smugmug.images.get"
		"&SessionID=%s&Heavy=1&AlbumID=%s&AlbumKey=%s";
static char *smugmug_upload_url =
	"http://upload.smugmug.com/%s";


CURL *curl_init(void)
{
	CURL *curl;

	curl = curl_easy_init();
	if (!curl) {
		fprintf(stderr, "Can not init CURL!\n");
		return NULL;
	}
	curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);

	/* some ssl sanity checks on the connection we are making */
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
	return curl;
}


char *my_basename(char *name)
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

/**
 * get_string_from_stdin - reads a string from standard input
 *
 * returns a pointer to a buffer that must be later freed by the caller
 * with a call to free().
 */
char *get_string_from_stdin(void)
{
	char *temp;
	char *string;

	string = zalloc(100);
	if (!string)
		return NULL;

	if (!fgets(string, 99, stdin))
		return NULL;
	temp = strchr(string, '\n');
	*temp = '\0';
	return string;
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
		files_list_free(&album->files);
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
		if (filename->basename)
			free(filename->basename);
		if (filename->id)
			free(filename->id);
		if (filename->key)
			free(filename->key);
		if (filename->caption)
			free(filename->caption);
		if (filename->original_url)
			free(filename->original_url);
		free(filename);
	}
}

/**
 * find_value - attempts to find a value within a string
 *
 * Works a lot like strstr() except we are searching through a
 * "string = value" type buffer.  The value that the needle is set
 * to is then copied into a new buffer and returned.  This buffer must be
 * later freed with a call to free().
 *
 * This lets us do a "poor mans" xml parser without really having to parse
 * xml.  We just rely on the fact that the tag values come in a specific
 * order.  Yeah, it's cheating, but for now, we seem to get away with it.
 *
 * Returns NULL if the needle is not found in the haystack, or if we are
 * unable to allocate the needed memory buffer.
 */
char *find_value(const char *haystack, const char *needle, char **new_pos)
{
	char *location;
	char *temp;
	char *value;

	location = strstr(haystack, needle);
	if (!location)
		return NULL;

	value = zalloc(1000);
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

/**
 * smug_curl_buffer_alloc- allocates a struct smug_curl_buffer
 *
 * returns a pointer to a struct smug_curl_buffer that must be later freed
 * with a call to smug_curl_buffer_free()
 */
struct smug_curl_buffer *smug_curl_buffer_alloc(void)
{
	struct smug_curl_buffer *buffer;

	buffer = zalloc(sizeof(*buffer));
	if (!buffer)
		return NULL;

	/* start out with a data buffer of 1 byte to
	 * make the buffer fill logic simpler */
	buffer->data = zalloc(1);
	if (!buffer->data) {
		free(buffer);
		return NULL;
	}
	buffer->length = 0;
	return buffer;
}

void smug_curl_buffer_free(struct smug_curl_buffer *buffer)
{
	if (!buffer)
		return;
	free(buffer->data);
	free(buffer);
}

/**
 * session_alloc - creates a struct session
 *
 * returns a pointer to a struct session that must be later freed with a
 * call to smug_curl_buffer_free()
 */
struct session *session_alloc(void)
{
	struct session *session;

	session = zalloc(sizeof(*session));
	if (!session)
		return NULL;
	INIT_LIST_HEAD(&session->albums);
	INIT_LIST_HEAD(&session->files_upload);
	INIT_LIST_HEAD(&session->files_download);
	return session;
}

void session_free(struct session *session)
{
	if (!session)
		return;
	free(session->password);
	free(session->email);
	free(session->session_id);
	album_list_free(&session->albums);
	files_list_free(&session->files_upload);
	files_list_free(&session->files_download);
	free(session);
}

size_t curl_callback(void *buffer, size_t size, size_t nmemb, void *userp)
{
	struct smug_curl_buffer *curl_buf = userp;
	size_t buffer_size = size * nmemb;
	char *temp;

	if ((!buffer) || (!buffer_size) || (!curl_buf))
		return -EINVAL;

	/* add to the data we already have */
	temp = zalloc(curl_buf->length + buffer_size + 1);
	if (!temp)
		return -ENOMEM;

	memcpy(temp, curl_buf->data, curl_buf->length);
	free(curl_buf->data);
	curl_buf->data = temp;
	memcpy(&curl_buf->data[curl_buf->length], (char *)buffer, buffer_size);
	curl_buf->length += buffer_size;

	dbg("%s\n", curl_buf->data);

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
	int album_number = 0;

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
		album = zalloc(sizeof(*album));
		INIT_LIST_HEAD(&album->files);
		album->id = id;
		album->key = key;
		album->title = title;
		album->number = ++album_number;

		list_add_tail(&album->entry, &session->albums);
		found_one++;
	}

	if (!found_one)
		return -EINVAL;
	return 0;
}

static int get_images(struct smug_curl_buffer *buffer, struct album *album)
{
	char *temp = buffer->data;
	struct filename *filename;
	char *id;
	char *key;
	char *name;
	char *caption;
	char *original_url;
	int found_one = 0;

	while (1) {
		id = find_value(temp, "Image id", &temp);
		if (!id)
			break;
		key = find_value(temp, "Key", &temp);
		if (!key)
			break;
		name = find_value(temp, "FileName", &temp);
		if (!name)
			break;
		caption = find_value(temp, "Caption", &temp);
		if (!caption)
			break;
		original_url = find_value(temp, "OriginalURL", &temp);
		if (!original_url)
			break;
		dbg("%s: %s: %s: %s: %s\n",
		    id, key, name, caption, original_url);
		filename = zalloc(sizeof(*filename));
		if (!filename)
			break;
		filename->id = id;
		filename->key = key;
		filename->filename = name;
		filename->caption = caption;
		filename->original_url = original_url;
		list_add_tail(&filename->entry, &album->files);
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

int generate_md5s(struct list_head *files)
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
			fclose(fp);
			return -err;
		}
		memcpy(filename->md5, md5, 16);
		sprintf_md5(md5_string, &md5[0]);
		dbg("md5 of %s is %s\n", filename->filename, md5_string);
		fclose(fp);
	}
	return 0;
}

int curl_progress_func(void *arg_progress,
		       double dltotal, double dlnow,
		       double ultotal, double ulnow)
{
	int bytes_now;
	int total;
	int percent;
	int data_avg = 0;
	struct progress *progress = (struct progress *)arg_progress;

	if (!progress)
		return -EINVAL;

	time_t now = time(NULL);
	if (progress->start == 0)
		progress->start = now;
	time_t timediff = now - progress->start;
	if (progress->upload) {
		bytes_now = (int)ulnow;
		total = (int)ultotal;
		percent = (int)(ulnow*100.0/ultotal);
	} else {
		bytes_now = (int)dlnow;
		total = (int)dltotal;
		percent = (int)(dlnow*100.0/dltotal);
	}
	if (timediff > 0)
		data_avg = (bytes_now / timediff / 1024);
	fprintf(stdout, "      \r%d of %d: %s: %d of %d (%d%%) %d KiB/s",
		progress->position, progress->total,
		progress->filename, bytes_now, total, percent, data_avg);
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

	progress = zalloc(sizeof(*progress));
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
	sprintf(buf, "X-Smug-FileName: %s", filename->basename);
	dbg("%s\n", buf);
	headers = curl_slist_append(headers, buf);

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	if (!session->quiet) {
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION,
				 curl_progress_func);
		curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, progress);
	}

	dbg("starting upload...\n");
	res = curl_easy_perform(curl);
	if (res)
		fprintf(stderr, "upload error %d, exiting\n", res);

	curl_slist_free_all(headers);
	if (!session->quiet) {
		fprintf(stdout, "\n");
		fflush(stdout);
	}
	curl_easy_cleanup(curl);
	smug_curl_buffer_free(buffer);
	free(progress);
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

	dbg("num_to_upload = %d\n", num_to_upload);
	list_for_each_entry(filename, &session->files_upload, entry) {
		++i;
		retval = upload_file(session, filename, album,
				     i, num_to_upload);
		if (retval)
			return retval;
	}
	return 0;
}

struct album *select_album(const char *album_title, const char *category_title,
			   const char *qs_name, struct session *session)
{
	struct album *album;
	int found_album;
	char *string;
	int album_no = 0;

	if (!album_title) {
		fprintf(stdout, "Available albums:\n");
		list_for_each_entry(album, &session->albums, entry)
			fprintf(stdout, "\t%3d: %s\n",
				album->number, album->title);
		fprintf(stdout, "\n");
		fprintf(stdout, "Please enter number of album to upload to: ");
		string = get_string_from_stdin();
		if (string) {
			album_no = atoi(string);
			free(string);
		}
		found_album = 0;
		list_for_each_entry(album, &session->albums, entry) {
			if (album->number == album_no) {
				found_album = 1;
				album_title = album->title;
				break;
			}
		}
		if (!found_album) {
			fprintf(stdout, "Album number %d is not found\n",
				album_no);
			return NULL;
		}
	} else {
		found_album = 0;
		list_for_each_entry(album, &session->albums, entry) {
			if (strcmp(album->title, album_title) == 0) {
				found_album = 1;
				break;
			}
		}
		if (!found_album && category_title) {
			album = smug_create_album(album_title, category_title,
						  qs_name, session);
			if (album)
				found_album = 1;
		}
		if (!found_album) {
			fprintf(stdout, "Album %s is not found\n", album_title);
			return NULL;
		}
	}
	return album;
}

int smug_login(struct session *session)
{
	char url[500];
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

	/* log into smugmug */
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);
	res = curl_easy_perform(curl);
	if (res) {
		printf("error(%d) trying to login\n", res);
		return -EINVAL;
	}

	char *rsp_stat = find_value(curl_buf->data, "rsp stat", NULL);
	if (!rsp_stat)
		return -EINVAL;
	if (strcmp(rsp_stat, "fail") == 0) {
		char *msg = find_value(curl_buf->data, "msg", NULL);
		printf("error to login: %s\n", msg);
		free(msg);
	}
	free(rsp_stat);

	retval = get_session_id(curl_buf, session);
	if (retval) {
		fprintf(stderr, "session_id was not found\n");
		return -EINVAL;
	}

	curl_easy_cleanup(curl);
	smug_curl_buffer_free(curl_buf);
	return 0;
}

int smug_logout(struct session *session)
{
	char url[500];
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

	curl_easy_cleanup(curl);
	smug_curl_buffer_free(curl_buf);
	return 0;
}

int smug_get_albums(struct session *session)
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

	sprintf(url, smugmug_album_list_url, session->session_id, api_key);
	dbg("url = %s\n", url);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);
	res = curl_easy_perform(curl);
	if (res) {
		fprintf(stderr, "error(%d) trying to read list of albums\n",
			res);
		return -EINVAL;
	}

	retval = get_albums(curl_buf, session);
	if (retval) {
		fprintf(stderr, "error parsing albums\n");
		return -EINVAL;
	}

	curl_easy_cleanup(curl);
	smug_curl_buffer_free(curl_buf);
	return 0;
}

/**
 * returns a pointer to a buffer that must be later freed by the caller
 * with a call to free().
 */
char *smug_get_quicksettings_id(const char *qs_name, struct session *session)
{
	char url[1000];
	struct smug_curl_buffer *curl_buf;
	CURL *curl = NULL;
	CURLcode res;

	if (!session)
		return NULL;

	curl_buf = smug_curl_buffer_alloc();
	if (!curl_buf)
		return NULL;

	curl = curl_init();
	if (!curl) {
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}

	sprintf(url, smugmug_quicksettings_list_url, session->session_id);
	dbg("url = %s\n", url);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res) {
		fprintf(stderr, "error(%d) trying to get quicksettings\n", res);
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}

	char *rsp_stat = find_value(curl_buf->data, "rsp stat", NULL);
	if (!rsp_stat) {
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}
	if (strcmp(rsp_stat, "fail") == 0) {
		char *msg = find_value(curl_buf->data, "msg", NULL);
		printf("error to get quicksettings: %s\n", msg);
		free(msg);
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}
	free(rsp_stat);


	char *temp = curl_buf->data;
	char *id = NULL;
	char *name = NULL;
	while (1) {
		id = find_value(temp, "AlbumTemplate id", &temp);
		name = find_value(temp, "AlbumTemplateName", &temp);
		if (!id || !name)
			break;
		dbg("%s: %s\n", id, name);
		if (strcmp(name, qs_name) == 0) {
			free(name);
			smug_curl_buffer_free(curl_buf);
			return id;
		}
	}

	if (id)
		free(id);
	if (name)
		free(name);
	smug_curl_buffer_free(curl_buf);
	return NULL;
}

/**
 * returns a pointer to a buffer that must be later freed by the caller
 * with a call to free().
 */
char *smug_get_category_id(const char *category_title, struct session *session)
{
	char url[1000];
	struct smug_curl_buffer *curl_buf;
	CURL *curl = NULL;
	CURLcode res;

	if (!session)
		return NULL;

	curl_buf = smug_curl_buffer_alloc();
	if (!curl_buf)
		return NULL;

	curl = curl_init();
	if (!curl) {
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}

	sprintf(url, smugmug_category_list_url, session->session_id);
	dbg("url = %s\n", url);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res) {
		fprintf(stderr, "error(%d) trying to get category\n", res);
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}

	char *rsp_stat = find_value(curl_buf->data, "rsp stat", NULL);
	if (!rsp_stat) {
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}
	if (strcmp(rsp_stat, "fail") == 0) {
		char *msg = find_value(curl_buf->data, "msg", NULL);
		printf("error to get category: %s\n", msg);
		free(msg);
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}
	free(rsp_stat);


	char *temp = curl_buf->data;
	char *id = NULL;
	char *title = NULL;
	while (1) {
		id = find_value(temp, "Category id", &temp);
		/* NOTE:  In 1.2.1 "Title" will change to "Name" */
		title = find_value(temp, "Title", &temp);
		if (!id || !title)
			break;
		dbg("%s: %s\n", id, title);
		if (strcmp(title, category_title) == 0) {
			free(title);
			smug_curl_buffer_free(curl_buf);
			return id;
		}
	}

	if (id)
		free(id);
	if (title)
		free(title);
	smug_curl_buffer_free(curl_buf);
	return NULL;
}

struct album *smug_create_album(const char *album_title,
				const char *category_title, const char *qs_name,
				struct session *session)
{
	char url[1000];
	struct smug_curl_buffer *curl_buf;
	CURL *curl = NULL;
	CURLcode res;

	if (!session)
		return NULL;

	char *category_id = smug_get_category_id(category_title, session);
	if (!category_id)
		return NULL;

	char *qs_id = smug_get_quicksettings_id(qs_name, session);
	if (!category_id) {
		free(category_id);
		return NULL;
	}

	curl_buf = smug_curl_buffer_alloc();
	if (!curl_buf) {
		free(category_id);
		free(qs_id);
		return NULL;
	}

	curl = curl_init();
	if (!curl) {
		free(category_id);
		free(qs_id);
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}

	sprintf(url, smugmug_album_create_url, session->session_id, album_title,
		category_id, qs_id);
	dbg("url = %s\n", url);
	free(category_id);
	free(qs_id);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);
	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res) {
		fprintf(stderr, "error(%d) trying to create album\n",
			res);
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}

	char *rsp_stat = find_value(curl_buf->data, "rsp stat", NULL);
	if (!rsp_stat) {
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}
	if (strcmp(rsp_stat, "fail") == 0) {
		char *msg = find_value(curl_buf->data, "msg", NULL);
		printf("error to create login: %s\n", msg);
		free(msg);
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}
	free(rsp_stat);


	char *temp = curl_buf->data;
	char *id = find_value(temp, "Album id", &temp);
	char *key = find_value(temp, "Key", &temp);
	if (!id || !key) {
		printf("incomplete return from create album\n");
		smug_curl_buffer_free(curl_buf);
		return NULL;
	}
	dbg("%s: %s: %s\n", id, key, album_title);
	struct album *album = zalloc(sizeof(*album));
	INIT_LIST_HEAD(&album->files);
	album->id = id;
	album->key = key;
	album->title = strdup(album_title);
	if (session->albums.prev)
		album->number = list_entry(session->albums.prev, struct album,
					   entry)->number + 1;
	else
		album->number = 1;
	/* add to session->albums to unify cleanup */
	list_add_tail(&album->entry, &session->albums);

	smug_curl_buffer_free(curl_buf);

	return album;
}

int smug_read_images(struct session *session, struct album *album)
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

	sprintf(url, smugmug_image_list_url, session->session_id,
		album->id, album->key);
	dbg("url = %s\n", url);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);
	res = curl_easy_perform(curl);
	if (res) {
		fprintf(stderr, "error(%d) trying to read list of albums\n",
			res);
		return -EINVAL;
	}

	retval = get_images(curl_buf, album);
	if (retval) {
		fprintf(stderr, "error parsing albums\n");
		return -EINVAL;
	}

	curl_easy_cleanup(curl);
	smug_curl_buffer_free(curl_buf);
	return 0;
}

int smug_download(struct filename *filename)
{
	struct smug_curl_buffer *curl_buf;
	CURL *curl = NULL;
	CURLcode res;
	struct progress *progress;

	if (!filename)
		return -EINVAL;

	dbg("trying to download %s\n", filename->filename);

	curl_buf = smug_curl_buffer_alloc();
	if (!curl_buf)
		return -ENOMEM;

	dbg("1\n");
	progress = zalloc(sizeof(*progress));
	if (!progress)
		return -ENOMEM;
	progress->filename = filename->basename;
	progress->upload = 0;
	progress->position = 1;	/* FIXME */
	progress->total = 1;	/* FIXME */

	dbg("2\n");
	curl = curl_init();
	if (!curl)
		return -EINVAL;
	dbg("3\n");

	dbg("url = %s\n", filename->original_url);
	curl_easy_setopt(curl, CURLOPT_URL, filename->original_url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curl_buf);

	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION,
			 curl_progress_func);
	curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, progress);

	dbg("starting download...\n");
	res = curl_easy_perform(curl);
	if (res)
		fprintf(stderr, "download error %d, exiting\n", res);

/*	if (!session->quiet) { */
		fprintf(stdout, "\n");
		fflush(stdout);
/*	} */
	curl_easy_cleanup(curl);
	smug_curl_buffer_free(curl_buf);
	free(progress);
	return (int)res;
}

void smug_parse_configfile(struct session *session)
{
	FILE *config_file;
	char *line = NULL;
	size_t len = 0;
	char *email = NULL;
	char *password = NULL;
	char *smug_file;
	char *home = getenv("HOME");

	/* config file is ~/.smug  */
	smug_file = alloca(strlen(home) + 7);

	sprintf(smug_file, "%s/.smug", home);

	config_file = fopen(smug_file, "r");

	/* No error if file does not exist or is unreadable.  */
	if (config_file == NULL)
		return;

	do {
		ssize_t n = getline(&line, &len, config_file);
		if (n < 0)
			break;
		if (line[n - 1] == '\n')
			line[n - 1] = '\0';
		/* Parse file.  Format is the usual value pairs:
		   email=address
		   passwort=value
		   # is a comment character
		*/
		*strchrnul(line, '#') = '\0';
		char *c = line;
		while (isspace(*c))
			c++;
		/* Ignore blank lines.  */
		if (c[0] == '\0')
			continue;

		if (!strncasecmp(c, "email", 5) && (c[5] == '=')) {
			c += 6;
			if (c[0] != '\0')
				email = strdup(c);
		} else if (!strncasecmp(c, "password", 8) &&
			   (c[8] == '=')) {
			c += 9;
			if (c[0] != '\0')
				password = strdup(c);
		}
	} while (!feof(config_file));

	if (password)
		session->password = password;
	if (email)
		session->email = email;

	/* Free buffer and close file.  */
	free(line);
	fclose(config_file);
}
