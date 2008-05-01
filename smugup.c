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
#include "list.h"
#include "md5.h"
#include "smugbatch_version.h"
#include "smug.h"


int debug;

static void display_help(void)
{
	fprintf(stdout, "smugup - upload photos to smugmug.com\n");
	fprintf(stdout, "Version: " SMUGBATCH_VERSION "\n");
	fprintf(stdout, "Usage:\n");
	fprintf(stdout, "  smugup [options] files\n");
	fprintf(stdout, "options are:\n");
	fprintf(stdout, "  --email email@address\n");
	fprintf(stdout, "  --password password\n");
	fprintf(stdout, "  --album album\n");
	fprintf(stdout, "  --quiet\n");
	fprintf(stdout, "  --debug\n");
	fprintf(stdout, "  --help\n");
}

int main(int argc, char *argv[], char *envp[])
{
	static const struct option options[] = {
		{ "debug", 0, NULL, 'd' },
		{ "email", 1, NULL, 'e' },
		{ "password", 1, NULL, 'p' },
		{ "album", 1, NULL, 'a' },
		{ "help", 0, NULL, 'h' },
		{ "quiet", 0, NULL, 'q' },
		{ }
	};
	struct filename *filename;
	struct session *session;
	struct album *album;
	char *album_title = NULL;
	int retval;
	int option;
	int i;
	int found_album;

	session = session_alloc();
	if (!session) {
		fprintf(stderr, "no more memory...\n");
		return -1;
	}

	curl_global_init(CURL_GLOBAL_ALL);
	smug_parse_configfile(session);

	while (1) {
		option = getopt_long_only(argc, argv, "dqe:p:a:h", options, NULL);
		if (option == -1)
			break;
		switch (option) {
		case 'd':
			debug = 1;
			break;
		case 'e':
			session->email = strdup(optarg);
			dbg("email = %s\n", session->email);
			break;
		case 'p':
			session->password = strdup(optarg);
			dbg("password = %s\n", session->password);
			break;
		case 'a':
			album_title = strdup(optarg);
			dbg("album_title = %s\n", album_title);
			break;
		case 'q':
			session->quiet = 1;
			break;
		case 'h':
			display_help();
			goto exit;
		default:
			display_help();
			goto exit;
		}
	}

	if (!session->email) {
		fprintf(stdout, "Enter smugmug.com email address: ");
		session->email = get_string_from_stdin();
	}

	if (!session->password) {
		fprintf(stdout, "Enter smugmug.com password: ");
		session->password = get_string_from_stdin();
	}

	/* build up a list of all filenames to be used here */
	for (i = optind; i < argc; ++i) {
		filename = zalloc(sizeof(*filename));
		if (!filename)
			// FIXME
			return -ENOMEM;
		filename->filename = strdup(argv[i]);
		filename->basename = my_basename(filename->filename);
		dbg("adding filename '%s'\n", argv[i]);
		list_add_tail(&filename->entry, &session->files_upload);
	}

	retval = smug_login(session);
	if (retval) {
		fprintf(stderr, "Can not login\n");
		return -1;
	}

	dbg("1\n");
	retval = smug_get_albums(session);
	if (retval) {
		fprintf(stderr, "Can not read albums\n");
		return -1;
	}

	retval = generate_md5s(&session->files_upload);
	if (retval) {
		fprintf(stderr, "Error calculating md5s\n");
		return -1;
	}

	if (!album_title) {
		fprintf(stdout, "Available albums:\n");
		list_for_each_entry(album, &session->albums, entry)
			fprintf(stdout, "\t%s\n", album->title);
		fprintf(stdout, "\nwhich album id to upload to? ");
		album_title = get_string_from_stdin();
	}

	found_album = 0;
	list_for_each_entry(album, &session->albums, entry) {
		if (strcmp(album->title, album_title) == 0) {
			found_album = 1;
			break;
		}
	}
	if (!found_album) {
		fprintf(stdout, "Album %s is not found\n", album_title);
		return -1;
	}

	retval = upload_files(session, album);
	if (retval) {
		fprintf(stderr, "Error uploading files\n");
		return -1;
	}

	smug_logout(session);

	session_free(session);
exit:
	return 0;
}
