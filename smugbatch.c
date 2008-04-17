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
#include "smugbatch_version.h"


#define dbg(format, arg...)						\
	do {								\
		if (debug)						\
			printf("%s: " format , __func__ , ## arg );	\
	} while (0)


static char *api_key = "ABW1oenNznek2rD4AIiFn7OhkEkmzEIb";
static char *user_agent = "smugbatch/"SMUGBATCH_VERSION" (greg@kroah.com)";

static char *password;
static char *email;
static char *session_id;
static int debug;

static char *session_id_tag = "Session id";

static char *smugmug_album_list_url = "https://api.smugmug.com/hack/rest/1.2.0/?method=smugmug.albums.get&SessionID=%s&APIKey=%s";
static char *smugmug_login_url = "https://api.smugmug.com/hack/rest/1.2.0/?method=smugmug.login.withPassword&EmailAddress=%s&Password=%s&APIKey=%s";
static char *smugmug_logout_url = "https://api.smugmug.com/hack/rest/1.2.0/?method=smugmug.logout&SessionID=%s&APIKey=%s";
static char *smugmug_upload_url = "http://upload.smugmug.com/%s";

struct album {
	struct list_head entry;
	char *id;
	char *key;
	char *title;
};

struct filename {
	struct list_head entry;
	char *filename;
	unsigned char md5[16];
};

static LIST_HEAD(album_list);
static LIST_HEAD(filename_list);
static int num_files_to_transfer;

static void free_album_list(void)
{
	struct album *album;
	struct album *temp;

	list_for_each_entry_safe(album, temp, &album_list, entry) {
		dbg("cleaning up album %s\n", album->title);
		free(album->id);
		free(album->key);
		free(album->title);
		free(album);
	}
}

static void free_filename_list(void)
{
	struct filename *filename;
	struct filename *temp;

	list_for_each_entry_safe(filename, temp, &filename_list, entry) {
		dbg("cleaning up filename %s\n", filename->filename);
		free(filename);
	}
}

static char *find_value(const char *haystack, const char *needle,
			char **new_pos)
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

static int sanitize_buffer(char *buffer, size_t size, size_t nmemb)
{
	size_t buffer_size = size * nmemb;
	char *temp;

	if ((!buffer) || (!buffer_size))
		return -EINVAL;

	/* we aren't supposed to get a \0 terminated string, so make sure */
	temp = buffer;
	temp[buffer_size-1] = '\0';
	return 0;
}

static size_t parse_login(void *buffer, size_t size, size_t nmemb, void *userp)
{
	size_t buffer_size = size * nmemb;
	char *temp = buffer;

	session_id = NULL;

	if (sanitize_buffer(buffer, size, nmemb))
		goto exit;

	dbg("buffer = '%s'\n", temp);
	session_id = find_value(buffer, session_id_tag, NULL);

	dbg("session_id = %s\n", session_id);

exit:
	if (!session_id)
		dbg("SessionID not found!");
	return buffer_size;
}

static size_t parse_albums(void *buffer, size_t size, size_t nmemb, void *userp)
{
	size_t buffer_size = size * nmemb;
	char *temp = buffer;
	struct album *album;
	char *id;
	char *key;
	char *title;

	if (sanitize_buffer(buffer, size, nmemb))
		goto exit;

	dbg("%s: buffer = '%s'\n", __func__, temp);

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
		list_add_tail(&album->entry, &album_list);
	}

exit:
	return buffer_size;
}

static size_t parse_upload(void *buffer, size_t size, size_t nmemb, void *userp)
{
	size_t buffer_size = size * nmemb;
	char *temp = buffer;

	if (sanitize_buffer(buffer, size, nmemb))
		goto exit;

	dbg("%s: buffer = '%s'\n", __func__, temp);

exit:
	return buffer_size;
}

static size_t parse_logout(void *buffer, size_t size, size_t nmemb, void *userp)
{
	size_t buffer_size = size * nmemb;
	char *temp = buffer;

	if (sanitize_buffer(buffer, size, nmemb))
		goto exit;

	dbg("%s: buffer = '%s'\n", __func__, temp);

exit:
	return buffer_size;
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

static int generate_md5(void)
{
	struct filename *filename;
	FILE *fp;
	int err;
	unsigned char *md5 = ptr_align(md5_data, 4);
	char md5_string[64];

	if (!num_files_to_transfer)
		return 0;

	/* let's generate the md5 of the files we are going to upload */
	list_for_each_entry(filename, &filename_list, entry) {
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

static int upload_file(CURL *curl, struct filename *filename,
		       struct album *album)
{
	FILE *fd;
	int file_handle;
	struct stat file_info;
	CURLcode res;
	char buffer[100];
	char url[1000];
	char md5_string[64];
	struct curl_slist *headers = NULL;

	file_handle = open(filename->filename, O_RDONLY);
	fstat(file_handle, &file_info);
	close(file_handle);
	fd = fopen(filename->filename, "rb");
	if (!fd)
		return -EINVAL;

	dbg("%s is %d bytes big\n", filename->filename, (int)file_info.st_size);

	sprintf(url, smugmug_upload_url, filename->filename);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_upload);
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(curl, CURLOPT_PUT, 1);
	curl_easy_setopt(curl, CURLOPT_READDATA, fd);
	curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
			 (curl_off_t)file_info.st_size);

	sprintf_md5(&md5_string[0], &filename->md5[0]);
	sprintf(buffer, "Content-MD5: %s", md5_string);
	dbg("%s\n", buffer);
	headers = curl_slist_append(headers, buffer);
	headers = curl_slist_append(headers, "X-Smug-Version: 1.2.0");
	headers = curl_slist_append(headers, "X-Smug-ResponseType: REST");
	sprintf(buffer, "X-Smug-SessionID: %s", session_id);
	dbg("%s\n", buffer);
	headers = curl_slist_append(headers, buffer);
	sprintf(buffer, "X-Smug-AlbumID: %s", album->id);
	dbg("%s\n", buffer);
	headers = curl_slist_append(headers, buffer);

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	dbg("starting upload...\n");
	res = curl_easy_perform(curl);
	if (res)
		printf("upload error %d, exiting\n", res);

	curl_slist_free_all(headers);
	return (int)res;
}

static int upload_files(CURL *curl, struct album *album)
{
	struct filename *filename;
	int err;

	if (!num_files_to_transfer)
		return 0;

	list_for_each_entry(filename, &filename_list, entry) {
		err = upload_file(curl, filename, album);
		if (err)
			return err;
	}
	curl_easy_cleanup(curl);
	return 0;
}

static void display_help(void)
{
	printf("help goes here...\n");
}

int main(int argc, char *argv[], char *envp[])
{
	static const struct option options[] = {
		{ "debug", 0, NULL, 'd' },
		{ "email", 1, NULL, 'e' },
		{ "password", 1, NULL, 'p' },
		{ "help", 0, NULL, 'h' },
		{ }
	};
	CURL *curl = NULL;
	CURLcode res;
	static char url[1000];
	int option;
	int i;
	struct filename *filename;

	while (1) {
		option = getopt_long(argc, argv, "de:p:h", options, NULL);
		if (option == -1)
			break;
		switch (option) {
		case 'd':
			debug = 1;
			break;
		case 'e':
			email = strdup(optarg);
			dbg("email = %s\n", email);
			break;
		case 'p':
			password = strdup(optarg);
			dbg("password = %s\n", password);
			break;
		case 'h':
			display_help();
			goto exit;
		default:
			display_help();
			goto exit;
		}
	}

	/* build up a list of all filenames to be used here */
	for (i = optind; i < argc; ++i) {
		filename = malloc(sizeof(*filename));
		if (!filename)
			goto exit;
		filename->filename = argv[i];
		dbg("adding filename '%s'\n", argv[i]);
		list_add_tail(&filename->entry, &filename_list);
		++num_files_to_transfer;
	}

	if ((!email) || (!password)) {
		display_help();
		goto exit;
	}

	sprintf(url, smugmug_login_url, email, password, api_key);
	dbg("url = %s\n", url);

	curl = curl_easy_init();
	if (!curl) {
		printf("Can not init CURL!\n");
		return 1;
	}

	curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);

	curl_easy_setopt(curl, CURLOPT_URL, url);

	/* some ssl sanity checks on the connection we are making */
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);

	/* log into smugmug */
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_login);
	res = curl_easy_perform(curl);
	if (res) {
		printf("error(%d) trying to login\n", res);
		goto exit;
	}

	if (!session_id) {
		printf("session_id was not found, exiting\n");
		goto exit;
	}

	/* Get list of albums for this user */
	sprintf(url, smugmug_album_list_url, session_id, api_key);
	dbg("url = %s\n", url);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_albums);
	res = curl_easy_perform(curl);
	if (res) {
		printf("error(%d) trying to read list of albums\n", res);
		goto exit;
	}

	if (generate_md5())
		goto exit;

	printf("%d files to tranfer:\n", num_files_to_transfer);
	list_for_each_entry(filename, &filename_list, entry) {
		printf("%s\n", filename->filename);
		}

	{
		struct album *album;
		printf("availble albums:\nalbum id\talbum name\n");
		list_for_each_entry(album, &album_list, entry) {
			printf("%s\t\t%s\n", album->id, album->title);
			if (strcmp(album->title, "temp") == 0)
				break;
			}
		printf("\nwhich album id to upload to?\n");
		upload_files(curl, album);
	}

	/* logout */
	curl = curl_easy_init();
	if (!curl) {
		printf("Can not init CURL!\n");
		return 1;
	}
	curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
	sprintf(url, smugmug_logout_url, session_id, api_key);
	dbg("url = %s\n", url);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_logout);
	res = curl_easy_perform(curl);
	if (res) {
		printf("error(%d) trying to logout\n", res);
		goto exit;
	}


exit:
	if (curl)
		curl_easy_cleanup(curl);
	if (email)
		free(email);
	if (password)
		free(password);
	if (session_id)
		free(session_id);
	free_album_list();
	free_filename_list();

	return 0;
}
