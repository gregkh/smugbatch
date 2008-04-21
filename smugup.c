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
	fprintf(stdout, "Usage:\n");
	fprintf(stdout, "  smugup [options] files\n");
	fprintf(stdout, "options are:\n");
	fprintf(stdout, "  --email email@address\n");
	fprintf(stdout, "  --password password\n");
	fprintf(stdout, "  --debug");
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
	struct filename *filename;
	struct session *session;
	struct album *album;
	int retval;
	int option;
	int i;

	session = session_alloc();
	if (!session) {
		fprintf(stderr, "no more memory...\n");
		return -1;
	}

	curl_global_init(CURL_GLOBAL_ALL);

	while (1) {
		option = getopt_long(argc, argv, "de:p:h", options, NULL);
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
		case 'h':
			display_help();
			goto exit;
		default:
			display_help();
			goto exit;
		}
	}

	if ((!session->email) || (!session->password)) {
		display_help();
		goto exit;
	}

	/* build up a list of all filenames to be used here */
	for (i = optind; i < argc; ++i) {
		filename = malloc(sizeof(*filename));
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

	printf("availble albums:\nalbum id\talbum name\n");
	list_for_each_entry(album, &session->albums, entry) {
		printf("%s\t\t%s\n", album->id, album->title);
		if (strcmp(album->title, "temp") == 0)
			break;
		}
	printf("\nwhich album id to upload to?\n");
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
