
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

static char *password = "";
static char *email = "";
static char *api_key = "ABW1oenNznek2rD4AIiFn7OhkEkmzEIb";
static char *session_id;

static char *smugmug_album_list_url = "https://api.smugmug.com/hack/rest/1.1.1/?method=smugmug.albums.get&SessionID=%s&APIKey=%s";
static char *smugmug_login_url = "https://api.smugmug.com/hack/rest/1.1.1/?method=smugmug.login.withPassword&EmailAddress=%s&Password=%s&APIKey=%s";
static char *smugmug_logout_url = "https://api.smugmug.com/hack/rest/1.1.1/?method=smugmug.logout&SessionID=%s&APIKey=%s";


size_t parse_login(void *buffer, size_t size, size_t nmemb, void *userp)
{
	static char *session_id_string = "<SessionID>";
	char *location;
	char *temp;

	session_id = NULL;

	if ((!buffer) || (!size)) {
		printf("1\n");
		goto exit;
	}

	/* all we care about is <SessionID> */
//	printf("buffer = '%s'\nsession_id_string='%s'\n", buffer, session_id_string);
	location = strstr(buffer, session_id_string);
	if (!location)
		goto exit;

	location += strlen(session_id_string);
	session_id = malloc(1000);
	if (!session_id)
		goto exit;

	temp = session_id;

	while (*location != '<') {
		*temp = *location;
		++temp;
		++location;
	}
	*temp = '\0';

	printf("session_id = %s\n", session_id);

exit:
	if (!session_id)
		printf("SessionID not found!");
	return size;
}


int main(void)
{
	CURL *curl;
	CURLcode res;
	static char url[1000];

	sprintf(url, smugmug_login_url, email, password, api_key);
	printf("url = %s\n", url);

	curl = curl_easy_init();
	if (!curl) {
		printf("Can not init CURL!\n");
		return 1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);

	//#ifdef SKIP_PEER_VERIFICATION
	/*
	* If you want to connect to a site who isn't using a certificate that is
	* signed by one of the certs in the CA bundle you have, you can skip the
	* verification of the server's certificate. This makes the connection
	* A LOT LESS SECURE.
	*
	* If you have a CA cert for the server stored someplace else than in the
	* default bundle, then the CURLOPT_CAPATH option might come handy for
	* you.
	*/
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	//#endif

	//#ifdef SKIP_HOSTNAME_VERFICATION
	/*
	* If the site you're connecting to uses a different host name that what
	* they have mentioned in their server certificate's commonName (or
	* subjectAltName) fields, libcurl will refuse to connect. You can skip
	* this check, but this will make the connection less secure.
	*/
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
	//#endif

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_login);
	res = curl_easy_perform(curl);
	if (res) {
		printf("error(%d) trying to login, res\n");
		goto exit;
	}

exit:
	curl_easy_cleanup(curl);

	return 0;
}
