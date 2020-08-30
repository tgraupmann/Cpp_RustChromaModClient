// This is a project uses the following packages
//
// libCurl - HTTP GET
// jsoncpp - Constructing JSON
//

#include <stdio.h>
#include <stdlib.h>
#include <iostream>

#include <json/json.h>

#define CURL_STATICLIB
#include <curl.h>

using namespace std;

static string _sServerName = "localhost";
static string _sServerPort = "5000";

size_t CurlWrite_CallbackFunc_StdString(void* contents, size_t size, size_t nmemb, string* s)
{
    size_t newLength = size * nmemb;
    try
    {
        s->append((char*)contents, newLength);
    }
    catch (std::bad_alloc&)
    {
        //handle memory problem
        return 0;
    }
    return newLength;
}

void DoHttpPost()
{
    CURL* curl;
    CURLcode res;

    /* In windows, this will init the winsock stuff */
    curl_global_init(CURL_GLOBAL_ALL);

    /* get a curl handle */
    curl = curl_easy_init();
    if (curl) {
        /* First set the URL that is about to receive our POST. This URL can
           just as well be a https:// URL if that is what should receive the
           data. */
        string url = "http://";
        url += _sServerName;
        url += ":";
        url += _sServerPort;
        url += "/players.json";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        // ignore SSL errors
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); //uncomment to see verbose output
        /* Now specify the POST data */
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "name=daniel&project=curl");

        string content;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content);

        /* Perform the request, res will get the return code */
        res = curl_easy_perform(curl);

        cout << "Curl Response CURLE_OK: " << (res == CURLE_OK ? "YES" : "NO") << endl;

        /* Check for errors */
        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
        }
        else
        {
            cout << "CURL RESPONSE BEGIN:::" << endl;
            cout << content;
            cout << ":::CURL RESPONSE END" << endl;
        }

        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}


int main()
{
    DoHttpPost();
}

#ifdef _DEBUG
#pragma comment( lib, "msvcrtd" )
#else
#pragma comment( lib, "msvcrt" )
#endif
#pragma comment( lib, "Wldap32" )
#pragma comment( lib, "Ws2_32" )
#pragma comment (lib, "crypt32")
#pragma comment( lib, "libcurl" )
#pragma comment( lib, "libssh2" )
#pragma comment( lib, "libcrypto" )
#pragma comment( lib, "libssl" )
#pragma comment( lib, "zlibstat" )
#pragma comment (lib, "jsoncpp")
