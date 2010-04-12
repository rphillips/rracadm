#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <curl/curl.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>


#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

typedef struct racadm_conf_t {
    const char *host;
    const char *username;
    const char *password;
    short port;
    int debug;
} racadm_conf_t;

typedef struct racadm_transport_t {
    CURL *curl;
    racadm_conf_t *conf;
    char *data;
    ssize_t size;
    int sid;
    int cmd_response;
    struct curl_slist *headers;
} racadm_transport_t;

racadm_transport_t* racadm_transport_create(racadm_conf_t *conf);
int                 racadm_execute(racadm_transport_t *t, const char *cmd);
void                racadm_destroy(racadm_transport_t *t);
void                usage();

int main(int argc, char *const *argv)
{
    int c;
    racadm_conf_t *conf;
    
    conf = calloc(1, sizeof(*conf));
    conf->port = 443;

    curl_global_init(CURL_GLOBAL_ALL);
    xmlInitParser();

    while ((c = getopt(argc, argv, "dr:u:p:P:")) != -1) {
        switch (c) {
        case 'u':
            conf->username = optarg;
            break;
        case 'p':
            conf->password = optarg;
            break;
        case 'P':
            conf->port = atoi(optarg);
            break;
        case 'r':
            conf->host = optarg;
            break;
        case 'd':
            conf->debug++;
            break;
        case '?':
            usage();
            break;
        }
    }

    if (conf->username == NULL || 
        conf->password == NULL ||
        conf->host == NULL) {
        usage();
    }

    /* flatten the command line arguments for racadm */
    char *racadm_args, *p;
    int i, len;
    for (i=optind,len=0; i<argc; i++) {
        len += strlen(argv[i]) + 1;
    }

    p = racadm_args = malloc(len);
    for (i=optind; i<argc; i++) {
        strcpy(p, argv[i]);
        if (i != (argc-1))
            strcat(p, " ");
        p += strlen(argv[i]) + 1;
    }

    racadm_transport_t *t = racadm_transport_create(conf);
    racadm_execute(t, racadm_args);
    racadm_destroy(t);

    free(racadm_args);

    xmlCleanupParser();

    return 0;
}

void 
usage()
{
    fprintf(stdout, "rracadm [options] -- commands \n");
    fprintf(stdout, "Options:\n");
    fprintf(stdout, "-r [value] : hostname\n");
    fprintf(stdout, "-P [value] : port\n");
    fprintf(stdout, "-u [value] : username\n");
    fprintf(stdout, "-p [value] : password\n");
    exit(0);
}

static size_t
racadm_write_cb(const void *ptr, size_t size, size_t nmemb, void *stream)
{
    racadm_transport_t *t = stream;
    size_t len = size * nmemb;

    if (t->data == NULL) {
        t->data = malloc(len + 2);
    }
    else {
        t->data = realloc(t->data, t->size + len + 1); 
    }   

    if (t->data) {
        memcpy(&(t->data[t->size]), ptr, len);
        t->size += len;
        t->data[t->size] = 0;
    }   

    return len;
}

racadm_transport_t*
racadm_transport_create(racadm_conf_t *conf)
{
    racadm_transport_t *t = calloc(1, sizeof(*t));

    t->headers = NULL;
    t->headers = curl_slist_append(t->headers, "Content-type: text/xml"); 
    t->headers = curl_slist_append(t->headers, "Connection: Keep-Alive"); 

    t->conf = conf;
    t->curl = curl_easy_init();
    t->data = NULL;
    t->size = 0;

    curl_easy_setopt(t->curl, CURLOPT_NOPROGRESS, 1);
    curl_easy_setopt(t->curl, CURLOPT_FAILONERROR, 1);
    curl_easy_setopt(t->curl, CURLOPT_CONNECTTIMEOUT, 5);
    curl_easy_setopt(t->curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(t->curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(t->curl, CURLOPT_HTTPHEADER, t->headers);
    curl_easy_setopt(t->curl, CURLOPT_WRITEDATA, t);
    curl_easy_setopt(t->curl, CURLOPT_WRITEFUNCTION, racadm_write_cb);

    if (conf->debug) {
        curl_easy_setopt(t->curl, CURLOPT_VERBOSE, 1);
    }

    /* the remote racadm CGIs currently do not return the sid within a cookie.
     * if it did, then this line could be used instead of the parse routine to
     * fetch the sid.
     */
    //curl_easy_setopt(t->curl, CURLOPT_COOKIEFILE, "");
    return t;
}

static void 
racadm_transport_reset(racadm_transport_t *t)
{
    free(t->data);
    t->data = NULL;
    t->size = 0;
}

static int
racadm_login(racadm_transport_t *t)
{
    char url[1024];
    char data[1024];

    racadm_transport_reset(t);

    snprintf(url, sizeof(url), "https://%s:%i/cgi-bin/login",
             t->conf->host, t->conf->port);
    snprintf(data, sizeof(data), 
             "<?xml version='1.0'?><LOGIN><REQ><USERNAME>%s</USERNAME><PASSWORD>%s</PASSWORD></REQ></LOGIN>",
             t->conf->username, t->conf->password);

    curl_easy_setopt(t->curl, CURLOPT_URL, url);
    curl_easy_setopt(t->curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(t->curl, CURLOPT_POSTFIELDSIZE, strlen(data));
    curl_easy_setopt(t->curl, CURLOPT_POST, 1);

    return curl_easy_perform(t->curl);
}

static int
racadm_logout(racadm_transport_t *t)
{
    char url[1024];
    racadm_transport_reset(t);
    snprintf(url, sizeof(url), "https://%s:%i/cgi-bin/logout", 
             t->conf->host, t->conf->port);
    curl_easy_setopt(t->curl, CURLOPT_URL, url);
    return curl_easy_perform(t->curl);
}

static int
racadm_cmd(racadm_transport_t *t, const char *cmd)
{
    char url[1024];
    char data[1024];

    racadm_transport_reset(t);

    snprintf(url, sizeof(url), "https://%s:%i/cgi-bin/exec", t->conf->host, t->conf->port);
    snprintf(data, sizeof(data), 
             "<?xml version='1.0'?><EXEC><REQ><CMDINPUT>racadm %s</CMDINPUT><MAXOUTPUTLEN>0x0fff</MAXOUTPUTLEN></REQ></EXEC>",
             cmd);
    curl_easy_setopt(t->curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(t->curl, CURLOPT_POSTFIELDSIZE, strlen(data));
    curl_easy_setopt(t->curl, CURLOPT_POST, 1);
    curl_easy_setopt(t->curl, CURLOPT_URL, url);
    return curl_easy_perform(t->curl);
}

static char*
racadm_parse(racadm_transport_t *t, const xmlChar* xpathExpr)
{
    char *result = NULL;
    xmlDocPtr doc;
    xmlXPathContextPtr xpathCtx; 
    xmlXPathObjectPtr xpathObj; 

    /* Load XML document */
    doc = xmlParseMemory(t->data, t->size);
    if (doc == NULL) {
        fprintf(stderr, "Error: unable to parse memory\n");
        return NULL;
    }

    /* Create xpath evaluation context */
    xpathCtx = xmlXPathNewContext(doc);
    if(xpathCtx == NULL) {
        fprintf(stderr,"Error: unable to create new XPath context\n");
        xmlFreeDoc(doc); 
        return NULL;
    }

    /* Evaluate xpath expression */
    xpathObj = xmlXPathEvalExpression(xpathExpr, xpathCtx);
    if(xpathObj == NULL) {
        fprintf(stderr,"Error: unable to evaluate xpath expression \"%s\"\n", xpathExpr);
        xmlXPathFreeContext(xpathCtx); 
        xmlFreeDoc(doc); 
        return NULL;
    }

    /* Print results */
    if (xpathObj->nodesetval->nodeTab) {
        result = xmlNodeListGetString(doc, 
                                       xpathObj->nodesetval->nodeTab[0]->xmlChildrenNode, 
                                       1);
    }

    /* Cleanup */
    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx); 
    xmlFreeDoc(doc); 
    
    return result;
}

static void
racadm_setup_cookie(racadm_transport_t *t, const char *sid)
{
    char cookie[1024];
    snprintf(cookie, sizeof(cookie), "sid=%s", sid);
    curl_easy_setopt(t->curl, CURLOPT_COOKIE, cookie);
}

int
racadm_execute(racadm_transport_t *t, const char *cmd)
{
    char *result;
    int res;

    /* login */
    res = racadm_login(t);
    if (res != 0) {
        fprintf(stderr, "login error: %d %s\n", res, curl_easy_strerror(res));
        return -1;
    }
    /* parse for sid, since the CGI doesn't return the SID in a cookie */
    result = racadm_parse(t, "//SID");
    if (!result || *result == '0') {
        fprintf(stderr, "could not login to device\n");
        return -1;
    }
    racadm_setup_cookie(t, result);
    free(result);
    /* execute */
    res = racadm_cmd(t, cmd);
    if (res != 0) {
        fprintf(stderr, "cmd error: %d %s\n", res, curl_easy_strerror(res));
        return -1;
    }
    /* display */
    result = racadm_parse(t, "//CMDOUTPUT");
    if (result) {
        fprintf(stdout, "%s\n", result);
        free(result);
    }
    /* logout */
    res = racadm_logout(t);
    if (res != 0) {
        fprintf(stderr, "logout error: %d %s\n", res, curl_easy_strerror(res));
        return -1;
    }

    return 0;
}

void
racadm_destroy(racadm_transport_t *t)
{
    curl_easy_cleanup(t->curl);
    free(t);
}

