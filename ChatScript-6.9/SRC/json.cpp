#include "common.h"

#ifndef DISCARDJSON // ---------------------------- CURL/JSON related code donated by anonymous user and revised by wilcox  ---------------------

// GENERAL JSON SUPPORT

#include "jsmn.h"
static bool curl_done_init = false; 

#define MAX_JSON_LABEL 50
static char jsonLabel[MAX_JSON_LABEL+1];
bool safeJsonParse = false;
int jsonIdIncrement = 1;
int jsonStore = 0; // where to put json fact refs
int jsonIndex;
int jsonOpenSize = 0;
unsigned int jsonPermanent = FACTTRANSIENT;
bool jsonNoduplicate = false;
bool jsonDuplicate = false;
bool jsonDontKill = false;
bool directJsonText = false;
static int objectcnt = 0;
static FunctionResult JSONpath(char* buffer, char* path, char* jsonstructure,bool raw,bool nofail);
static MEANING jcopy(WORDP D);

static int JSONArgs() 
{
	int index = 1;
	directJsonText = false;
	bool used = false;
	jsonPermanent = FACTTRANSIENT; // default
	jsonNoduplicate = false;
	jsonDuplicate = true;
	char* arg1 = ARGUMENT(1);
	if (*arg1 == '"') // remove quotes
	{
		++arg1;
		size_t len = strlen(arg1);
		if (arg1[len-1] == '"') arg1[len-1] = 0; 
	}
	char word[MAX_WORD_SIZE];
	while (*arg1)
	{
		arg1 = ReadCompiledWord(arg1,word);
		if (!stricmp(word,(char*)"permanent"))  
		{
			jsonPermanent = 0;
			used = true;
		}
		else if (!stricmp(word,(char*)"unique"))  
		{
			jsonNoduplicate = true;
			used = true;
		}
		else if (!stricmp(word,(char*)"duplicate"))  
		{
			jsonDuplicate = true;
			used = true;
		}
		else if (!stricmp(word,(char*)"transient"))  used = true;
		else if (!stricmp(word,(char*)"direct"))  directJsonText = true; // used by jsonopen
		else if (!stricmp(word,(char*)"safe")) safeJsonParse = used = true;
	}
	if (used) ++index;
	return index;
}

void InitJSONNames()
{
	objectcnt = 0; // also for json arrays
	jsonIdIncrement = 1;
}

MEANING GetUniqueJsonComposite(char* prefix) 
{
	char namebuff[MAX_WORD_SIZE];
	char* permanence = "";
	if (jsonPermanent == FACTTRANSIENT) permanence = "t";
	while (1)
	{
		sprintf(namebuff, "%s%s%s%d", prefix,permanence, jsonLabel,objectcnt);
		objectcnt += jsonIdIncrement;
		WORDP D = FindWord(namebuff);
		if (!D) break;
	}
	jsonIdIncrement = 1; // return to linear hunting of open slots from here
	return MakeMeaning(StoreWord(namebuff,AS_IS));
}

static char* IsJsonNumber(char* str)
{
	if (IsDigit(*str) || (*str == '-' && IsDigit(str[1]))) // +number is illegal in json
	{
		// validate the number
		char* at = str;
		if (*at != '-') --at;
		bool periodseen = false;
		bool exponentseen = false;
		while (*++at)
		{
			if (*at == '.' && !periodseen && !exponentseen) periodseen = true;
			else if ((*at == 'e' || *at == 'E')  && !exponentseen) 
			{
				if (at[1] == '+' || at[1] == '-') ++at;
				exponentseen = true;
			}
			else if (*at == ' ' || *at == ',' || *at == '}' || *at == ']') return at;
			else if (!IsDigit(*at)) return NULL; // cannot be number
		}
		return at;
	}
	return NULL;
}

int factsJsonHelper(char *jsontext, jsmntok_t *tokens, int currToken, MEANING *retMeaning, int* flags, bool key, bool nofail) {
	// Always build with duplicate on. create a fresh copy of whatever
	jsmntok_t curr = tokens[currToken];
	char namebuff[256];
	*flags = 0;
	int retToken = currToken + 1;
	char str[MAX_WORD_SIZE ];
	*str = 0;
	int size = curr.end - curr.start;
	if (size >= (MAX_WORD_SIZE )) 
		size = (MAX_WORD_SIZE ) - 4;
	switch (curr.type) {
	case JSMN_PRIMITIVE: { //  true  false, numbers, null 
		strncpy(str,jsontext + curr.start,size);
		str[size] = 0;
		*flags = JSON_PRIMITIVE_VALUE; // json primitive type
		if (*str == USERVAR_PREFIX || *str == SYSVAR_PREFIX || *str == '_' || *str == '\'') // variable values from CS
		{
			// get path to safety if any
			char mainpath[MAX_WORD_SIZE];
			char* path = strchr(str,'.');
			char* pathbracket = strchr(str,'[');
			char* first = path;
			if (pathbracket && path && pathbracket < path) first = pathbracket;
			if (first) strcpy(mainpath,first);
			else *mainpath = 0;
			if (path) *path = 0;
			if (pathbracket) *pathbracket = 0;

			char word1[MAX_WORD_SIZE];
			FunctionResult result;
			ReadShortCommandArg(str,word1,result); // get the basic item
			strcpy(str,word1);
			char* numberEnd = NULL;

			// now see if we must process a path
			if (*mainpath) // access field given
			{
				char word[MAX_WORD_SIZE];
				result = JSONpath(word, mainpath, str,true,nofail); // raw mode
				if (result != NOPROBLEM_BIT) 
				{
					if (!nofail) ReportBug((char*)"Bad Json path building facts from templace %s%s data: %s", str,mainpath,jsontext); // if we are not expecting it to fail
					return 0;
				}
				else strcpy(str,word);
			}
			if (!*str) 
			{
				strcpy(str,(char*)"null");
				*flags = JSON_STRING_VALUE; // string null
			}
			else if (!strcmp(str,(char*)"true") || !strcmp(str,(char*)"false")) {}
			else if (!strncmp(str,(char*)"ja-",3)) 
			{
				*flags = JSON_ARRAY_VALUE;
				MEANING M = jcopy(StoreWord(str));
				WORDP D = Meaning2Word(M);
				strcpy(str,D->word);

			}
			else if (!strncmp(str,(char*)"jo-",3)) 
			{
				*flags = JSON_OBJECT_VALUE;
				MEANING M = jcopy(StoreWord(str)); // json never shares ptrs
				WORDP D = Meaning2Word(M);
				strcpy(str,D->word);
			}
			else if ((numberEnd = IsJsonNumber(str)) != NULL) {;} 
			else *flags = JSON_STRING_VALUE; // cannot be number
		}
		*retMeaning = MakeMeaning(StoreWord(str,AS_IS)); 
		break;
	}
	case JSMN_STRING: {
		strncpy(str,jsontext + curr.start,size);
		str[size] = 0;
		*flags = JSON_STRING_VALUE; // string null
		if (!PreallocateString(size)) return FAILRULE_BIT;
		if (size == 0)  *retMeaning = MakeMeaning(StoreWord((char*)"null",AS_IS));
		else  *retMeaning = MakeMeaning(StoreWord(str,AS_IS));
		break;
	}
	case JSMN_OBJECT: {
		// Build the object name
		MEANING objectName = GetUniqueJsonComposite((char*)"jo-");
		*retMeaning = objectName;
		for (int i = 0; i < curr.size / 2; i++) { // each entry takes an id and a value
			MEANING keyMeaning = 0;
			int jflags = 0;
			retToken = factsJsonHelper(jsontext, tokens, retToken, &keyMeaning, &jflags,true,nofail);
			if (retToken == 0) return 0;
			MEANING valueMeaning = 0;
			retToken = factsJsonHelper(jsontext, tokens, retToken, &valueMeaning, &jflags,false,nofail);
			if (retToken == 0) return 0;
			CreateFact(objectName, keyMeaning, valueMeaning, jsonPermanent|jflags|JSON_OBJECT_FACT); // only the last value of flags matters. 5 means object fact in subject
		}
		*flags = JSON_OBJECT_VALUE;
		break;
	}
	case JSMN_ARRAY: {
		// Build the array name
		MEANING arrayName = GetUniqueJsonComposite((char*)"ja-");
		*retMeaning = arrayName;

		for (int i = 0; i<curr.size; i++) {
			sprintf(namebuff, "%d", i); // Build the array index
			MEANING index = MakeMeaning(StoreWord(namebuff,AS_IS));
			MEANING arrayMeaning = 0;
			int jflags = 0;
			retToken = factsJsonHelper(jsontext, tokens, retToken, &arrayMeaning, &jflags,false,nofail);
			if (retToken == 0) return 0;
			CreateFact(arrayName, index, arrayMeaning, jsonPermanent|jflags|JSON_ARRAY_FACT); // flag6 means subject is arrayfact
		}
		*flags = JSON_ARRAY_VALUE; 
		break;
	}
	default: 
		ReportBug((char*)"FATAL: (factsJsonHelper) Unknown JSON type encountered.");
	} 
	currentFact = NULL;
	return retToken;
}

MEANING factsPreBuildFromJson(char *jsontext, jsmntok_t *tokens,bool nofail) {
	MEANING retMeaning = 0;
	int flags = 0;
	int X = factsJsonHelper(jsontext, tokens, 0, &retMeaning, &flags,false,nofail);
	currentFact = NULL;	 // used up by putting into json
	if (!X) return 0;
	return retMeaning;
}

#ifdef WIN32
#include "curl.h"
	#ifdef DEBUG
		#pragma comment(lib, "../SRC/curl/libcurld.lib")#else
	#else
		#pragma comment(lib, "../SRC/curl/libcurl.lib")
	#endif
#else
	#include <curl/curl.h>
#endif


// Define our struct for accepting LCs output
struct CurlBufferStruct {
	char * buffer;
	size_t size;
};

// This is the function we pass to LC, which writes the output to a BufferStruct
static size_t CurlWriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data) {
	size_t realsize = size * nmemb;

	struct CurlBufferStruct* mem = (struct CurlBufferStruct*) data;
	size_t len = mem->size + realsize + 1;
	mem->buffer = expandAllocation(mem->buffer, (char*)ptr, len); // exits if runs out of transient space - could improve by guessing sizes and checking if we even need to allocate
	memcpy(&(mem->buffer[mem->size]), ptr, realsize); // add to buffer
	mem->size += realsize;
	mem->buffer[mem->size] = 0;
	return realsize;
}

static void dump(const char *text, FILE *stream, unsigned char *ptr, size_t size) // libcurl  callback when verbose is on
{
  size_t i;
  size_t c;
  unsigned int width=0x10;
  printf((char*)"%s, %10.10ld bytes (0x%8.8lx)\n", text, (long)size, (long)size);
  for(i=0; i<size; i+= width) 
  {
    printf((char*)"%4.4lx: ", (long)i);
 
    /* show hex to the left */
    for(c = 0; c < width; c++) 
	{
      if (i+c < size)  printf((char*)"%02x ", ptr[i+c]);
      else printf((char*)"%s",(char*)"   ");
    }
 
    /* show data on the right */
    for(c = 0; (c < width) && (i+c < size); c++) printf( "%c",(ptr[i+c]>=0x20) && (ptr[i+c]<0x80)?ptr[i+c]:'.');
    printf((char*)"%s",(char*)"\n");
  }
}
 
static int my_trace(CURL *handle, curl_infotype type, char *data, size_t size, void *userp)
{
  const char *text;
  (void)handle; /* prevent compiler warning */
 
  switch (type) {
  case CURLINFO_TEXT:
    printf( "== Info: %s", data);
  default: /* in case a new one is introduced to shock us */
    return 0;
  case CURLINFO_HEADER_OUT:
    text = "=> Send header";
    break;
  case CURLINFO_DATA_OUT:
    text = "=> Send data";
    break;
  case CURLINFO_SSL_DATA_OUT:
    text = "=> Send SSL data";
    break;
  case CURLINFO_HEADER_IN:
    text = "<= Recv header";
    break;
  case CURLINFO_DATA_IN:
    text = "<= Recv data";
    break;
  case CURLINFO_SSL_DATA_IN:
    text = "<= Recv SSL data";
    break;
  }
  dump(text, stderr, (unsigned char *)data, size);
  return 0;
}

/*
----------------------
FUNCTION: JSONOpenCode

		  Function arguments :
optional argument(1) - permanent, transient, direct
ARGUMENT(1) - request method : POST, GET, POSTU, GETU
ARGUMENT(2) - URL. The URL to use in the request
ARGUMENT(3) - If a POST request, this argument contains the post data
ARGUMENT(4) - This argument contains any needed extra REQUEST headers for the request(see note above).
	
	e.g.
	$$url = "https://api.github.com/users/test/repos"
	$$user_agent = ^"User-Agent: Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.2; WOW64; Trident/6.0)"
	^jsonopen(GET $$url "" $$user_agent)
	
	# GitHub requires a valid user agent header or it will reject the request.  Note, although
	#  not shown, if there are multiple extra headers they should be separated by the 
	#  tilde character ("~").

	E.g.
	$$url = "https://en.wikipedia.org/w/api.php?action=query&titles=Main%20Page&rvprop=content&format=json"
	$$user_agent = ^"myemail@hotmail.com User-Agent: Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.2; WOW64; Trident/6.0)"
	
*/
#define REQUEST_HEADER_NVP_SEPARATOR "~"
#define REQUEST_NVP_SEPARATOR ':'

// This function reimplements the semi-standard function strlcpy so we can use it on both Windows, Linux and Mac
size_t our_strlcpy(char *dst, const char *src, size_t siz) {
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0 && --n != 0) {
		do {
			if ((*d++ = *s++) == 0) break;
		} while (--n != 0);
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0) *d = '\0';                /* NUL-terminate dst */
		while (*s++) {;}
	}

	return(s - src - 1);    /* count does not include NUL */
}

#ifdef WIN32
// If we're on Windows, just use the safe strncpy version, strncpy_s.
# define SAFE_SPRINTF sprintf_s
#else
// Use snprintf for Linux.
# define SAFE_SPRINTF snprintf
#endif 

static int EncodingValue(char* name, char* field, int value)
{
	size_t len = strlen(name);
	char* at = strstr(field,name);
	if (!at) return value; // not found
	at += len;
	if (at[0] != ';') return 2; // autho
	if (at[1] != 'q' && at[1] != 'Q') return 2;
	if (at[2] != '=' || at[3] != '0') return 2;  // gzip;q=1
	if (at[4] == '.') return 2; // gzip;q=0.5
	return 1;
}

void CurlShutdown()
{
	if (curl_done_init) curl_global_cleanup ();
}


// Open a URL using the given arguments and return the JSON object's returned by querying the given URL as a set of ChatScript facts.
FunctionResult JSONOpenCode(char* buffer)
{
	int index = JSONArgs();
	size_t len;
	char* url = NULL;
	char* arg = NULL;
	char* extraRequestHeadersRaw = NULL;
	char kind = 0;

	char fieldName[1000];
	char fieldValue[1000];
	char headerLine[1000];

	bool encoded = false;
	char *raw_kind = ARGUMENT(index++);

	if (!stricmp(raw_kind, "POST"))  kind = 'P';
	else if (!stricmp(raw_kind, "GET")) kind = 'G';
	else if (!stricmp(raw_kind, "POSTU")) 
	{
		kind = 'P';
		encoded = true;
	}
	else if (!stricmp(raw_kind, "GETU")) 
	{
		kind = 'G';
		encoded = true;
	}
	else if (!stricmp(raw_kind, "PUT"))  kind = 'U';
	else if (!stricmp(raw_kind, "DELETE"))  kind = 'D';
	else {
		char* msg = "jsonopen- only POST, GET, PUT AND DELETE allowed\r\n";
		SetUserVariable((char*)"$$tcpopen_error", msg);	// pass along the error
		ReportBug(msg);
		return FAILRULE_BIT;
	}

	url = ARGUMENT(index++);

	// Now fix starting and ending quotes around url if there are any

	if (*url == '"') ++url;
	len = strlen(url);
	if (url[len - 1] == '"') url[len - 1] = 0;

	// convert \" to " within params and remove any wrapper
	arg = ARGUMENT(index++);
	if (*arg == '"') ++arg;
	len = strlen(arg);
	if (arg[len - 1] == '"') arg[len - 1] = 0;
	if (!stricmp(arg,(char*)"null")) *arg = 0; // empty string replaces null
	bool bIsExtraHeaders = false;

	extraRequestHeadersRaw = ARGUMENT(index++);

	char* timeout = GetUserVariable("$cs_jsontimeout");
	if (IsDigit(*ARGUMENT(index))) timeout = ARGUMENT(index); // local override
	long timelimit = (*timeout) ? atoi(timeout) : 300L;

	// Make sure the raw extra REQUEST headers parameter value is not empty and
	//  not the ChatScript empty argument character.
	if (strlen(extraRequestHeadersRaw) > 0)
	{
		// If the parameter value is only 1 characters long and it is a question mark,
		//  then ignore it since it's the "placeholder" (i.e. - "empty") parameter value
		//  indicating the parameter should be ignored.
		if (!((strlen(extraRequestHeadersRaw) == 1) && (*extraRequestHeadersRaw == '?')))
		{
			// Remove surrounding double-quotes if found.
			if (*extraRequestHeadersRaw == '"') ++extraRequestHeadersRaw;
			len = strlen(extraRequestHeadersRaw);
			if (extraRequestHeadersRaw[len - 1] == '"') extraRequestHeadersRaw[len - 1] = 0;
			bIsExtraHeaders = true;
		}

	} // if (strlen(extraRequestHeadersRaw) > 0)
	
	if (trace & TRACE_JSON)
	{
		Log(STDTRACELOG,(char*)"\r\n");
		Log(STDTRACETABLOG,(char*)"Json method/url: %s %s\r\n",raw_kind, url);
		if (bIsExtraHeaders) 
		{
			Log(STDTRACELOG,(char*)"\r\n");
			Log(STDTRACETABLOG,(char*)"Json header: %s\r\n", extraRequestHeadersRaw);
			Log(STDTRACETABLOG,(char*)"");
		}
		if (kind == 'P' || kind == 'U') 
		{
			Log(STDTRACELOG,(char*)"\r\n");
			Log(STDTRACETABLOG,(char*)"Json  data: %s\r\n ",arg);
			Log(STDTRACETABLOG,(char*)"");
		}
	}
	clock_t start_time = ElapsedMilliseconds();
	
	CURLcode res;
	struct CurlBufferStruct output;
	output.buffer = NULL;
	output.size = 0;

	// Get curl ready -- do this ONCE only during run of CS
	if (!curl_done_init) {
#ifdef WIN32
		if (InitWinsock() == FAILRULE_BIT) // only init winsock one per any use- we might have done this from TCPOPEN or PGCode
		{
			ReportBug((char*)"Winsock init failed");
			return FAILRULE_BIT;
		}
#endif
		curl_global_init(CURL_GLOBAL_SSL);
		curl_done_init = true;
	}
	CURL * curl  = curl_easy_init();
	if (!curl)
	{
		if (trace & TRACE_JSON) Log(STDTRACELOG,(char*)"Curl easy init failed");
		return FAILRULE_BIT;
	}

	// Add the necessary headers for the request.
	struct curl_slist *header = NULL;

	if (kind == 'P')
	{
		curl_easy_setopt(curl, CURLOPT_POST, 1);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, arg);
	} 
	if (kind == 'U')
	{
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, arg);
	} 
	if (kind == 'D')
	{
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, arg);
	}

	// Assuming a content return type of JSON.
	header = curl_slist_append(header, "Content-Type: application/json");
	int gzip = 0;
	int deflate = 0;
	int compress = 0;
	int identity = 0;
	int wild = 0;

	// If any extra REQUEST headers were specified, add them now.
	if (bIsExtraHeaders) 
	{
		// REQUEST header name/value pairs are separated by tildes ("~").
		char *p = strtok(extraRequestHeadersRaw, REQUEST_HEADER_NVP_SEPARATOR);

		// Process each REQUEST header.
		while (p) 
		{
			// Split the REQUEST header label and field value.
			char *p2 = strchr(p, REQUEST_NVP_SEPARATOR);

			if (p2) 
			{
				// Delimiter found.  Split out the field name and it's value.
				*p2 = 0;
				our_strlcpy(fieldName, p, sizeof(fieldName));
				char name[MAX_WORD_SIZE];
				MakeLowerCopy(name,fieldName);

				p2++;
				our_strlcpy(fieldValue, p2, sizeof(fieldValue));
				char value[MAX_WORD_SIZE];
				MakeLowerCopy(value,fieldValue);
				len = strlen(value);
				while (value[len-1] == ' ') value[--len] = 0;	// remove trailing blanks, forcing the field to abut the ~

				if (strstr(name,(char*)"accept-encoding"))
				{
					gzip = EncodingValue((char*)"gzip",value,gzip);
					deflate = EncodingValue((char*)"deflate",value,deflate);
					compress = EncodingValue((char*)"compress",value,compress);
					identity = EncodingValue((char*)"identity",value,identity);
					wild = EncodingValue((char*)"*",value,wild);
				}
			}
			else 
			{
				// No delimiter found.  Use the entire string as the field name and wipe the field value.
				our_strlcpy(fieldName, p, sizeof(fieldValue));
				strcpy(fieldValue, "");
				char value[MAX_WORD_SIZE];
				MakeLowerCopy(value,fieldName);
				if (strstr(value,(char*)"accept-encoding"))
				{
					gzip = EncodingValue((char*)"gzip",value,gzip);
					deflate = EncodingValue((char*)"deflate",value,deflate);
					compress = EncodingValue((char*)"compress",value,compress);
					identity = EncodingValue((char*)"identity",value,identity);
					wild = EncodingValue((char*)"*",value,wild);
				} 
			}
			// Build the REQUEST header line for CURL.

			SAFE_SPRINTF(headerLine, sizeof(headerLine), "%s: %s", fieldName, fieldValue);

			// Add the new REQUEST header to the headers list for this request.
			header = curl_slist_append(header, headerLine);

			// Next REQUEST header.
			p = strtok(NULL, REQUEST_HEADER_NVP_SEPARATOR);
		} // while (p)

	} // if (extraRequestHeadersRaw)
	CURLcode val;

	char coding[MAX_WORD_SIZE];
	*coding = 0;
	if (wild == 2) // authorizes anything not mentioned
	{
		if (gzip == 0) gzip = 2;
		if (compress == 0) compress = 2;
		if (identity == 0) identity = 2;
		if (deflate == 0) deflate = 2;
	}
	if (compress == 2)
	{	
		if (gzip == 0) gzip = 2;
		if (deflate == 0) deflate = 2;
	}
	if (gzip == 2) strcat(coding,(char*)"gzip,(char*)");
	if (deflate == 2) strcat(coding,(char*)"deflate,(char*)");
	if (identity == 2) strcat(coding,(char*)"identity,(char*)");
	if (!*coding) strcpy(coding,(char*)"Accept-Encoding: identity");
	size_t len1 = strlen(coding);
	coding[len1-1] = 0; // remove terminal comma
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, coding);

	// Set up the CURL request.
	val = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header);
	val = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteMemoryCallback); // callback for memory
	val = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&output); // store output here
	val = curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, my_trace);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timelimit); // 300 second timeout to connect (once connected no effect)
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timelimit);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1); // dont generate signals in unix

	/* the DEBUGFUNCTION has no effect until we enable VERBOSE */
	if (trace & TRACE_JSON && deeptrace) curl_easy_setopt(curl, CURLOPT_VERBOSE, (long)1);
	res = curl_easy_perform(curl);

	curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_response);
	char code[MAX_WORD_SIZE];
	sprintf(code,(char*)"%ld",http_response);

	if (header)  curl_slist_free_all(header);
	curl_easy_cleanup(curl);
	if (trace & TRACE_JSON && res != CURLE_OK)  
	{
		char word[MAX_WORD_SIZE * 10];
		char* at = word;
		sprintf(at,"Json method/url: %s %s -- ",raw_kind, url);
		at += strlen(at);
		if (bIsExtraHeaders) 
		{
			sprintf(at,"Json header: %s -- ", extraRequestHeadersRaw);
			at += strlen(at);
			if (kind == 'P' || kind == 'U')  sprintf(at,"Json  data: %s\r\n ",arg);
		}

		if (res == CURLE_URL_MALFORMAT) { ReportBug((char*)"\r\nJson url malformed %s",word); }
		else if (res == CURLE_GOT_NOTHING) { ReportBug((char*)"\r\nCurl got nothing %s",word); }
		else if (res == CURLE_UNSUPPORTED_PROTOCOL) { ReportBug((char*)"\r\nCurl unsupported protocol %s",word); }
		else if (res == CURLE_COULDNT_CONNECT || res == CURLE_COULDNT_RESOLVE_HOST || res ==  CURLE_COULDNT_RESOLVE_PROXY) Log(STDTRACELOG,(char*)"\r\nJson connect failed ");
		else if (res == CURLE_OPERATION_TIMEDOUT)
		{
			ReportBug((char*)"\r\nCurl timeout ");
		}
		else
		{ 
			if (output.buffer && output.size)  
			{
				ReportBug((char*)"\r\nOther curl return code %d %s  - %s ",(int)res,word,output.buffer);
			}
			else 
			{
				ReportBug((char*)"\r\nOther curl return code %d %s",(int)res,word); 
			}
		}
	}
	if (timing & TIME_JSON) {
		int diff = ElapsedMilliseconds() - start_time;
		if (timing & TIME_ALWAYS || diff > 0) Log(STDTIMETABLOG, (char*)"Json open time: %d ms for %s %s\r\n", diff,raw_kind,url);
	}
	if (res != CURLE_OK) return FAILRULE_BIT;
	// 300 seconds is the default timeout
	// CUrl is gone, we have the json data now to convert
	FunctionResult result = NOPROBLEM_BIT;
	if (directJsonText)
	{
		strncpy(buffer,output.buffer,output.size); // be wary because this may be too much for caller
		buffer[output.size] = 0;
		jsonOpenSize = output.size;
	}
	else
	{
		ChangeDepth(1,(char*)"ParseJson");
		result = ParseJson(buffer, output.buffer,output.size,false);
		char x[MAX_WORD_SIZE];
		if (result == NOPROBLEM_BIT)
		{
			ReadCompiledWord(buffer,x);
			if (*x) // empty json object should not be returned, will not survive on its own
			{
				WORDP X = FindWord(x);
				if (X && X->subjectHead == 0) 
					*buffer = 0; 
			}
		}
		ChangeDepth(-1,(char*)"ParseJson");
	}
	if (trace & TRACE_JSON)
	{
		Log(STDTRACELOG,(char*)"\r\n");
		Log(STDTRACETABLOG,(char*)"\r\nJSON response: %d size: %d",http_response,output.size);
		if (output.size < (MAX_BUFFER_SIZE - SAFE_BUFFER_MARGIN)) Log(STDTRACELOG,(char*)"%s\r\n",output.buffer);
		Log(STDTRACETABLOG,(char*)"");
	}

	return result;
}

FunctionResult ParseJson(char* buffer, char* message, size_t size, bool nofail)
{
	jsonIdIncrement = 1000; // fast hunt for a slot for names since this collection may be sizeable
	*buffer = 0;
	if (trace & TRACE_JSON) 
	{
		if (size < (MAX_BUFFER_SIZE - 100)) Log(STDTRACETABLOG, "JsonParse Call: %s", message);
		else 
		{
			char msg[MAX_WORD_SIZE];
			strncpy(msg,message,MAX_WORD_SIZE-100);
			msg[MAX_WORD_SIZE-100] = 0;
			Log(STDTRACETABLOG, "JsonParse Call: %d-byte message too big to show all %s\r\n",size,msg);
		}
	}
	if (size < 1) return NOPROBLEM_BIT; // nothing to parse

	clock_t start_time = ElapsedMilliseconds();

	jsmn_parser parser;
	// First run it once to count the tokens
	jsmn_init(&parser);
	jsmnerr_t jtokenCount = jsmn_parse(&parser, message, size, NULL, 0);
	FACT* start = factFree;
	if (jtokenCount > 0) 
	{
		// Now run it with the right number of tokens
		jsmntok_t *tokens = (jsmntok_t *)AllocateInverseString(NULL,sizeof(jsmntok_t) * jtokenCount);
		if (!tokens) return FAILRULE_BIT;
		jsmn_init(&parser);
		jsmn_parse(&parser, message, size, tokens, jtokenCount);
		MEANING id = factsPreBuildFromJson(message, tokens,nofail);
		if (timing & TIME_JSON) {
			int diff = ElapsedMilliseconds() - start_time;
			if (timing & TIME_ALWAYS || diff > 0) Log(STDTIMETABLOG, (char*)"Json parse time: %d ms\r\n", diff);
		}
		ReleaseInverseString((char*)tokens);
		if (id != 0) // worked
		{
			WORDP D = Meaning2Word(id);
			strcpy(buffer,D->word);
			return NOPROBLEM_BIT;
		}
		else // failed, delete any facts created along the way
		{
			while (factFree > start) FreeFact(factFree--); //   restore back to facts alone
		}
	}
	return (nofail)  ? NOPROBLEM_BIT : FAILRULE_BIT;	
}

static char* jtab(int depth, char* buffer)
{
	while (depth--) *buffer++ = ' ';
	*buffer = 0;
	return buffer;
}

static int orderJsonArrayMembers(WORDP D, FACT** store,int limit) 
{
	--limit;
	int max = -1;
	int size = -1;
	FACT* G = GetSubjectNondeadHead(D);	
	bool show = false;
	while (G) // get facts in order - but if user manually deleted externally, we will have a hole.
	{
		if (show) TraceFact(G);
		if (G->flags & JSON_ARRAY_FACT) // in case of accidental collisions with normal words
		{
			int index = atoi(Meaning2Word(G->verb)->word);
			if (index >= limit) 
			{
				ReportBug((char*)"array fact out of ordering range - %s sample: %s index %d limit %d value %s", D->word,index,limit,Meaning2Word(G->object)->word);
				return -1; // cannot store this
			}
			store[index] = G;
			if (index > max) max = index;
			++size;
		}
		G = GetSubjectNondeadNext(G);
	}
	if (max > size || max < size) 
	{
		show = true;
		G = GetSubjectNondeadHead(D);
		char* info = (G) ? Meaning2Word(G->object)->word : (char*)"";
		ReportBug((char*)"Erased json array fact illegally previously - %s sample: %s max %d size %d", D->word,info,max,size);
		return -1;
	}
	return max + 1; // for the 0th value
}

static char* jwritehierarchy(int depth, char* buffer, WORDP D, int subject, int nest )
{
	FACT* stack[JSON_LIMIT];
	char* original = buffer;
	unsigned int size = (buffer - currentOutputBase + 200); // 200 slop to protect us
	if (size >= currentOutputLimit) 
	{
		ReportBug((char*)"Too much json hierarchy");
		return buffer; // too much output
	}

	int index = 0;
	if (!stricmp(D->word,(char*)"null")) 
	{
		if (subject & JSON_STRING_VALUE) strcpy(buffer,(char*)"\"\"");
		else strcpy(buffer,D->word); // primitive
		return buffer + strlen(buffer);
	}	
	if (!(subject&(JSON_ARRAY_VALUE|JSON_OBJECT_VALUE)))
	{
		if (subject & JSON_STRING_VALUE) *buffer++ = '"';
		if (subject & JSON_STRING_VALUE) AddEscapes(buffer,D->word,true);
		else strcpy(buffer,D->word);
		buffer += strlen(buffer);
		if (subject & JSON_STRING_VALUE) *buffer++ = '"';
		return buffer;
	}

	if (D->inferMark == inferMark)
	{
		char* kind = (D->word[1] == 'a') ? (char*)"[...]" : (char*) "{...}";
		sprintf(buffer,"%s %s",D->word,kind);
		return buffer + strlen(buffer);
	}
	D->inferMark = inferMark;
	
	if (D->word[1] == 'a') strcat(buffer,(char*)"[    # ");
	else strcat(buffer,(char*)"{    # ");
	strcat(buffer,D->word);
	buffer += strlen(buffer);

	if (nest-- <= 0) // immediately close a composite
	{
		if (D->word[1] == 'a') strcpy(buffer,(char*)"]");
		else strcpy(buffer,(char*)"}");
		buffer += strlen(buffer);
		return buffer; // do nothing now. dont do this composite
	}
	strcat(buffer,(char*)"\n");
	buffer += strlen(buffer);

	FACT* F =  GetSubjectNondeadHead(MakeMeaning(D));
	int indexsize = 0;
	bool invert = false;
	if (F && F->flags & JSON_ARRAY_FACT) indexsize = orderJsonArrayMembers(D, stack,JSON_LIMIT); // tests for illegal delete
	else // json object
	{
		invert = true; 
		while (F) // stack object key data
		{
			if (F->flags & JSON_OBJECT_FACT) 
			{
				stack[index++] = F;
				++indexsize;
			}
			F = GetSubjectNondeadNext(F);
			if (indexsize > 1999) F = 0; // abandon extra
		}
	}
	int flags = 0;
	for (int i = 0; i < indexsize; ++i)
	{
		unsigned int itemIndex = (invert) ? ( indexsize - i - 1) : i;
		size = (buffer - currentOutputBase + 400); // 400 slop to protect us
		if (size >= currentOutputLimit) 
		{
			ReportBug((char*)"Json too much");
			return buffer; // too much output
		}
		F = stack[itemIndex];
		if (F->flags & JSON_ARRAY_FACT)  
		{
			buffer = jtab(depth,buffer);
			flags = JSON_ARRAY_FACT;
		}
		else if (F->flags & JSON_OBJECT_FACT) 
		{
			buffer = jtab(depth,buffer);
			flags = JSON_OBJECT_FACT;
			strcpy(buffer++,(char*)"\""); // write key in quotes
			strcpy(buffer,WriteMeaning(F->verb));
			buffer += strlen(buffer);
			strcpy(buffer,(char*)"\": ");
			buffer += 3;
		}
		else continue;	 // not a json fact, an accident of something else that matched
		// continuing composite
		buffer = jwritehierarchy(depth+2,buffer, Meaning2Word(F->object),F->flags, nest);
		if (i < (indexsize-1)) strcpy(buffer++,(char*)",");
		strcpy(buffer++,(char*)"\n");
	}
	buffer = jtab(depth-2,buffer);
	if (D->word[1] == 'a') strcpy(buffer,(char*)"]");
	else strcpy(buffer,(char*)"}");
	buffer += strlen(buffer);
	return buffer;
}

FunctionResult JSONTreeCode(char* buffer)
{
	char* arg1 = ARGUMENT(1); // names a fact label
	WORDP D = FindWord(arg1);
	if (!D) return FAILRULE_BIT;
	char* arg2 = ARGUMENT(2);
	int nest = atoi(arg2);
	strcpy(buffer,(char*)"JSON=> \n");
	NextInferMark();
	buffer += strlen(buffer);
	buffer = jwritehierarchy(2,buffer,D,(arg1[1] == 'o') ? JSON_OBJECT_VALUE : JSON_ARRAY_VALUE,nest > 0 ? nest : 20000); // nest of 0 (unspecified) is infinitiy
	strcpy(buffer,(char*)"\n<=JSON \n");
	buffer += strlen(buffer);
	return NOPROBLEM_BIT;
}

FunctionResult JSONKindCode(char* buffer)
{
	char* arg = ARGUMENT(1);
	if (!strnicmp(arg,"jo-",3)) strcpy(buffer,(char*)"object");
	else if (!strnicmp(arg,"ja-",3)) strcpy(buffer,(char*)"array");
	else return FAILRULE_BIT; // otherwise fails
	return NOPROBLEM_BIT;	
}

static FunctionResult JSONpath(char* buffer, char* path, char* jsonstructure, bool raw,bool nofail)
{
	char mypath[MAX_WORD_SIZE];
	if (*path != '.' && *path != '[')  // default a dot notation if not given
	{
		mypath[0] = '.';
		strcpy(mypath+1,path);
	}
	else strcpy(mypath,path);
	path = mypath;	
	 
	WORDP D = FindWord(jsonstructure); 
	FACT* F;
	if (!D) return FAILRULE_BIT;
	path = SkipWhitespace(path);
	MEANING M;
	if (trace & TRACE_JSON) 
	{
		Log(STDTRACELOG,(char*)"\r\n");
		Log(STDTRACETABLOG,(char*)"");
	}

	while(1)
	{
		path = SkipWhitespace(path);
		if (!*path) // reached the bottom of the path
		{
			if (!D) return FAILRULE_BIT;
			// if it has whitespace or JSON special characters in it, we must return it as a string in quotes so JsonFormat can detect
			// unless raw was requested
			if (!raw)
			{
				char* at = D->word - 1;
				while (*++at)
				{
					char c = *at;
					if (c == '{' || c == '}' || c == '[' || c == ']' || c == ',' || c == ':' || c == ' ' || c == '\n' || c == '\t' ) break;
				}
				if (!*at) raw = true; // safe to use raw output
			}
			if (!raw) *buffer++ = '"'; 
			strcpy(buffer,D->word);
			if (!raw)  
			{
				buffer += strlen(buffer);
				*buffer++ = '"';
				*buffer = 0;
			}
			break;
		}
		if (*path == '[' || *path == '.') // we accept field names as single words in CS, not as quoted possibly weird strings
		{
			if (!D) return FAILRULE_BIT;
			char* next = path + 1;
			if (*next == '"') // handle quoted key
			{
				while (*++next)
				{
					if (*next == '"' && *(next-1) != '\\')  break; // leave on the closing quote
				}
			}
			while (*next && *next != '[' && *next != ']' && *next != '.' && *next != '*') ++next; // find token break
			char c = *next;
			*next = 0; // close off continuation
			if (*path == '[' && !IsDigit(path[1]) && path[1] != USERVAR_PREFIX) return FAILRULE_BIT;
			F = GetSubjectNondeadHead(D);
			char what[MAX_WORD_SIZE];
			if (path[1] == '"') // was quoted fieldname
			{
				strcpy(what,path+2);
				what[strlen(what)-1] = 0; // remove closing quote
			}
			else strcpy(what,path+1);
			if (*what == USERVAR_PREFIX) strcpy(what,GetUserVariable(what));
			M = MakeMeaning(FindWord(what)); // is CASE sensitive
			if (!M) return FAILRULE_BIT; // cant be in a fact if it cant be found
			while (F)
			{
				if (F->verb == M) 
				{
					if (trace & TRACE_JSON) TraceFact(F); // dont trace all, just the matching one
					D = Meaning2Word(F->object);
					break;
				}
				F = GetSubjectNondeadNext(F);
			}
			*next = c;
			path = next;
			if (*path == ']') ++path;

			if (F && *path == '*' && !path[1]) // return the fact ID at the bottom
			{
				sprintf(buffer,"%d",Fact2Index(F));
				return NOPROBLEM_BIT;
			}

			if (!F) 
				return FAILRULE_BIT;
		}
		else return FAILRULE_BIT;
	}
	return NOPROBLEM_BIT;
}

FunctionResult JSONPathCode(char* buffer)
{
	char* path = ARGUMENT(1);
	bool safe = !stricmp(ARGUMENT(3),(char*)"safe"); // dont quote the selected value if a complex string
	if (*path == '^') ++path; // skip opening functional marker
	if (*path == '"') ++path; // skip opening quote
	size_t len = strlen(path);
	if (path[len-1] == '"') path[len-1] = 0;
	if (*path == 0) 
	{
		strcpy(buffer,ARGUMENT(2));	// return the item itself
		return NOPROBLEM_BIT;
	}
	return JSONpath(buffer,path,ARGUMENT(2),!safe,false);
}

static MEANING jcopy(WORDP D)
{
	FACT* stack[JSON_LIMIT];
	int index = 0;
	MEANING composite = 0;
	if (D->word[1] == 'a')  composite =  GetUniqueJsonComposite((char*)"ja-");
	else composite =  GetUniqueJsonComposite((char*)"jo-");

	bool invert = false;
	int indexsize = 0;
	FACT* F = GetSubjectNondeadHead(D);
	if (F && F->flags & JSON_ARRAY_FACT) indexsize = orderJsonArrayMembers(D, stack,JSON_LIMIT); // tests for illegal delete
	else
	{
		invert = true;
		while (F) // stack them
		{
			if (F->flags & JSON_OBJECT_FACT) // no collision with possible outside weird words
			{
				stack[index++] = F;
				++indexsize;
			}
			F = GetSubjectNondeadNext(F);
			if (indexsize > 1999) F = 0; // abandon extra
		}
	}
	int flags = 0;
	for (int i = 0; i < indexsize; ++i)
	{
		unsigned int itemIndex = (invert) ? ( indexsize - i - 1) : i;
		F = stack[itemIndex];
		if (F->flags & (JSON_ARRAY_VALUE |  JSON_OBJECT_VALUE)) // composite
			CreateFact(composite,F->verb,jcopy(Meaning2Word(F->object)),(F->flags & JSON_FLAGS) | jsonPermanent);
		else CreateFact(composite,F->verb,F->object,(F->flags & JSON_FLAGS) | jsonPermanent ); // noncomposite
	}
	return composite;
}

void jkillfact(WORDP D)
{
	if (!D) return;
	FACT* F = GetSubjectNondeadHead(D);
	while (F) // stack them
	{
		FACT* G = GetSubjectNondeadNext(F);
		if (F->flags & (JSON_ARRAY_VALUE | JSON_OBJECT_VALUE)) jkillfact(Meaning2Word(F->object));
		if (F->flags & (JSON_ARRAY_FACT | JSON_OBJECT_FACT)) KillFact(F);
		F = G;
	}
}

static char* jwrite(char* buffer, WORDP D, int subject )
{
	FACT* stack[JSON_LIMIT];
	char* original = buffer;
	unsigned int size = (buffer - currentOutputBase + 200); // 200 slop to protect us
	if (size >= currentOutputLimit) return buffer; // too much output

	int index = 0;
	if (!(subject & (JSON_OBJECT_VALUE |JSON_ARRAY_VALUE)) && subject & JSON_FLAGS)
	{
		if (!stricmp(D->word, (char*)"null"))
		{
			if (subject & JSON_STRING_VALUE) strcpy(buffer, (char*)"\"\"");
			else strcpy(buffer, D->word); // primitive
			return buffer + strlen(buffer);
		}
		if (subject & JSON_STRING_VALUE) strcpy(buffer++,(char*)"\"");
		if (subject & JSON_STRING_VALUE) AddEscapes(buffer,D->word,true);
		else strcpy(buffer,D->word);
		buffer += strlen(buffer);
		if (subject & JSON_STRING_VALUE) strcpy(buffer++,(char*)"\"");
		return buffer;
	}

	// CS (not JSON) can have recursive structures. Protect against this
	if (D->inferMark == inferMark)
	{
		strcpy(buffer,D->word);
		return buffer + strlen(buffer);
	}
	D->inferMark = inferMark;

	if (D->word[1] == 'a')  strcpy(buffer,(char*)"[");
	else strcpy(buffer,(char*)"{ ");
	buffer += strlen(buffer);
	bool invert = false;
	int indexsize = 0;
	FACT* F = GetSubjectNondeadHead(D);
	if (F && F->flags & JSON_ARRAY_FACT) indexsize = orderJsonArrayMembers(D, stack,JSON_LIMIT); // tests for illegal delete
	else
	{
		invert = true;
		while (F) // stack them
		{
			if (F->flags & JSON_OBJECT_FACT) // no collision with possible outside weird words
			{
				stack[index++] = F;
				++indexsize;
			}
			F = GetSubjectNondeadNext(F);
			if (indexsize > 1999) F = 0; // abandon extra
		}
	}
	int flags = 0;
	for (int i = 0; i < indexsize; ++i)
	{
		unsigned int itemIndex = (invert) ? ( indexsize - i - 1) : i;
		size = (buffer - currentOutputBase + 400); // 400 slop to protect us
		if (size >= currentOutputLimit) return buffer; // too much output
		F = stack[itemIndex];
		if (F->flags & JSON_ARRAY_FACT) flags = JSON_ARRAY_FACT; // write out its elements
		else if (F->flags & JSON_OBJECT_FACT) 
		{
			flags = JSON_OBJECT_FACT;
			strcpy(buffer++,(char*)"\""); // put out key in quotes
			strcpy(buffer,WriteMeaning(F->verb));
			buffer += strlen(buffer);
			strcpy(buffer,(char*)"\": ");
			buffer += 3;
		}
		else continue;	 // not a json fact, an accident of something else that matched
 		buffer = jwrite(buffer, Meaning2Word(F->object),F->flags & JSON_FLAGS);
		if (i < (indexsize-1)) 
		{
			strcpy(buffer,(char*)", ");
			buffer += 2;
		}
	}
	if (D->word[1] == 'a')  strcpy(buffer,(char*)"] ");
	else strcpy(buffer,(char*)"} ");
	buffer += strlen(buffer);
	return buffer;
}

FunctionResult JSONWriteCode(char* buffer) // FACT to text
{
	char* arg1 = ARGUMENT(1); // names a fact label
	WORDP D = FindWord(arg1);
	if (!D) return FAILRULE_BIT;
	clock_t start_time = ElapsedMilliseconds();
	NextInferMark();
	jwrite(buffer,D,true);
	if (timing & TIME_JSON) {
		int diff = ElapsedMilliseconds() - start_time;
		if (timing & TIME_ALWAYS || diff > 0) Log(STDTIMETABLOG, (char*)"Json write time: %d ms\r\n", diff);
	}
	return NOPROBLEM_BIT;
}

FunctionResult JSONUndecodeStringCode(char* buffer) // undo escapes
{
	char* arg1 = ARGUMENT(1); 
	unsigned int remainingSize = currentOutputLimit - (buffer - currentOutputBase) - 1;
	CopyRemoveEscapes(buffer,arg1,remainingSize,true); // removing ALL escapes
	return NOPROBLEM_BIT;
}

FunctionResult JSONLabelCode(char* buffer) 
{
	char* arg1 = ARGUMENT(1); // names a  label
	if (strlen(arg1) > MAX_JSON_LABEL) arg1[MAX_JSON_LABEL] = 0;
	if (*arg1 == '"')
	{
		size_t len = strlen(arg1);
		arg1[len-1] = 0;
		++arg1;
	}
	if (strchr(arg1,' ') || 
		strchr(arg1,'\\') || strchr(arg1,'"') || strchr(arg1,'\'') || strchr(arg1,0x7f) || strchr(arg1,'\n') 
		|| strchr(arg1,'\r') || strchr(arg1,'\t')) return FAILRULE_BIT;	// must be legal unescaped json content and safe CS content
	if (*arg1 == 't' && !arg1[1]) return FAILRULE_BIT;	//  reserved for CS to mark transient
	strcpy(jsonLabel, arg1);
	return NOPROBLEM_BIT;
}

static bool jsonGather(WORDP D,int limit, int depth)
{
	if (limit && depth >= limit) return true;	// not allowed to do this level or lower
	FACT* F = GetSubjectNondeadHead(D);
	while (F) // flip the order
	{
		if (jsonIndex >= MAX_FIND) return false; // abandon extra
		if (F->flags & JSON_FLAGS) factSet[jsonStore][++jsonIndex] = F;
		if (F->flags & (JSON_ARRAY_FACT | JSON_OBJECT_FACT) && !jsonGather(Meaning2Word(F->object),limit,depth+1)) return false;
		F = GetSubjectNondeadNext(F);
	}
	return true;
}

FunctionResult JSONGatherCode(char* buffer) // jason FACT cluster by name to factSet
{
	int index = 1;
	if (impliedSet != ALREADY_HANDLED) jsonStore = impliedSet;
	else
	{
		jsonStore = GetSetID(ARGUMENT(index++)); 
		if (jsonStore == ILLEGAL_FACTSET) return FAILRULE_BIT;
	}
	if (impliedSet == ALREADY_HANDLED) // clear the set passed as argument
	{
		jsonIndex = 0;
		SET_FACTSET_COUNT(jsonStore,0);
	}
	else jsonIndex = FACTSET_COUNT(jsonStore); // keep going in set (like +=)
	
	WORDP D = FindWord(ARGUMENT(index++));
	if (!D) return FAILRULE_BIT;
	if (strnicmp(D->word,"jo-",3) && strnicmp(D->word,"ja-",3)) return FAILRULE_BIT;
	char* depth = ARGUMENT(index);
	int level = atoi(depth);	// 0 is infinite, 1 is top level
	if (!jsonGather(D,level,0)) return FAILRULE_BIT;
	SET_FACTSET_COUNT(jsonStore,jsonIndex);
	impliedSet = ALREADY_HANDLED;
	return NOPROBLEM_BIT;
}

FunctionResult JSONParseCode(char* buffer)
{
	safeJsonParse = false;
	int index = JSONArgs();
	bool nofail = !stricmp(ARGUMENT(index),(char*)"nofail");
	if (nofail) index++;
	char* data  = ARGUMENT(index++);
	if (*data == '^') ++data; // skip opening functional marker
	if (*data == '"') ++data; // skip opening quote
	size_t len = strlen(data);
	if (len && data[len-1] == '"') data[--len] = 0;
	data = SkipWhitespace(data);
	char close = 0;
	char* closer = NULL;

	// if safe, locate proper end of OOB data we assume all [] are balanced except for final OOB which has the extra ]
	int bracket = 1; // for the initial one  - match off {} and [] and stop immediately after
	if (safeJsonParse)
	{
		safeJsonParse = false;
		char* at = data - 1;
		bool quote = false;
		while (*++at)
		{
			if (quote)
			{
				if (*at == '"' && at[-1] != '\\') quote = false; // turn off quoted expr
				continue;
			}
			else if (*at == ':' || *at == ',' || *at == ' ') continue;
			else if (*at == '{' || *at == '[' ) ++bracket; // an opener
			else if (*at == '}' || *at == ']') // a closer
			{
				--bracket;
				// have we ended the item
				if (bracket <= 1) 
				{
					closer = at+1;
					close = *closer;
					at[1] = 0;
					break;
				}
			}
			else if (*at == '"' && !quote) 
			{
				quote = true;
				if (bracket == 1) 
					return FAILRULE_BIT; // dont accept quoted string as top level
			}
		}
		len = strlen(data);
	}
	FunctionResult result = ParseJson(buffer, data, len,nofail);
	if (close) *closer = close;
	return result;
}

FunctionResult JSONParseFileCode(char* buffer)
{
	safeJsonParse = false;
	int index = JSONArgs();
	FILE* in = NULL;
	char* file = ARGUMENT(index);
	if (*file == '"')
	{
		++file;
		size_t len = strlen(file);
		if (file[len-1] == '"') file[len-1] = 0; // remove parens
	}
	in = FopenReadOnly(file);
	if (!in) return FAILRULE_BIT;
	char* data = AllocateBuffer();
	char* start = data;
	*data++ = ' '; // insure buffer starts with space
	bool quote = false;
	while (ReadALine(readBuffer,in) >= 0) 
	{
		char* at = readBuffer - 1;
		while (*++at) // copy over w/o excessive whitespace
		{
			if (quote) {;} // dont touch quoted data
			else if (*at == ' ' || *at == '\n' || *at == '\r' || *at == '\t') // dont copy over if can be avoided
			{
				if ( *(at-1) == ' ') continue; // already have one
			}
			*data++ = *at;
			if ((data-start) >= MAX_BUFFER_SIZE) 
			{
				start = 0; // signal failure
				break;
			}
			if (*at == '"' && *(at-1) != '\\') quote = !quote; // flip quote state
		}
	}
	FClose(in);
	FunctionResult result = FAILRULE_BIT;
	if (start) // didnt overflow bufer
	{
		*data = 0;
		result = ParseJson(buffer, start, data-start,false);
	}
	FreeBuffer();
	return result;
}

#define OBJECTJ 1
#define ARRAYJ 2
#define DIDONE 3
#define MAXKIND 4000

FunctionResult JSONFormatCode(char* buffer)
{
	char* original = buffer;
	char nest[1000];
	int index = 0;
	char* arg = ARGUMENT(1);
	int field = 0;
	char* numberEnd = NULL;
	--arg;
	char kind[MAXKIND]; // just assume it wont overflow
	int level = 0;
	*kind = 0;
	while (*++arg) 
	{
		if (*arg == ' ' || *arg == '\n' || *arg == '\t') *buffer++ = *arg;
		else if (kind[0] == DIDONE) break; // finished already, shouldnt see more
		else if (*arg == '{') // json object open
		{
			nest[++index] = '{';
			*buffer++ = *arg;
			field = 1; // expecting element
			kind[++level] = OBJECTJ;
			if (level >= MAXKIND) break;
		}
		else if (*arg == '[') // json array open
		{
			*buffer++ = *arg;
			nest[++index] = '[';
			kind[++level] = ARRAYJ;
			if (level >= MAXKIND) break;
		}
		else if (*arg == '}' || *arg == ']') // object/array close
		{
			if (*arg == '}' && kind[level] != OBJECTJ) break;
			if (*arg == ']' && kind[level] != ARRAYJ) break;
			--level;
			if (!level) kind[level] = DIDONE;
			--index;
			*buffer++ = *arg;
			field = 0;
		}
		else if (*arg == ',')
		{
			// Unexpectedly came across the end of a property without a value
			if (field == 2) {
				*buffer++ = '"';
				*buffer++ = '"';
			}
			if (kind[level] != OBJECTJ && kind[level] != ARRAYJ) break;
			*buffer++ = *arg;
			field = 1;  // ready for fresh element - eg the keyword
		}
		else if (*arg == ':' ) 
		{
			if (kind[level] != OBJECTJ) break;
			*buffer++ = *arg;
			field = 2;
		}
		else if (*arg == '"') 
		{
			char* start = arg + 1;
			while (*++arg)
			{
				if (*arg == '"') break;
				if (*arg == '\\') {
					if (!*(arg+1)) return FAILRULE_BIT;  // nothing left to escape
					arg++;
					if (*arg == '\"' || *arg == '/' || *arg == '\\' || *arg == 'b' || *arg == 'f' || *arg == 'r' || *arg == 'n' || *arg == 't') ;
					else if (*arg == 'u') {
						arg++;
						// must have 4 hex digits
						for (int i = 0; i < 4 && *arg; i++) {
							char c = GetLowercaseData(*arg++);
							if (!IsAlphaUTF8OrDigit(c) || c > 'f') return FAILRULE_BIT;  // not a hex character
						}
						arg--;
					}
					else return FAILRULE_BIT;  // not a valid escaped character
				}
			}
			if (!*arg) return FAILRULE_BIT;
			*arg = 0; // remove closing quote

			*buffer++ = '"';
			AddEscapes(buffer,start,true);
			buffer += strlen(buffer);
			*buffer++ = '"'; // add back trailing quote
			if (level == 0) kind[0] = DIDONE;
			if (field == 2) field = 0;
		}
		else if ((numberEnd = IsJsonNumber(arg)) != NULL) // json number
		{
			strncpy(buffer,arg,numberEnd-arg);
			buffer += numberEnd - arg;
			arg = numberEnd - 1;
			if (level == 0) kind[0] = DIDONE;
			if (field == 2) field = 0;
		}
		else // literal or simple field name nonquoted
		{
			int fieldType = field;
			char word[MAX_WORD_SIZE];
			char* at = word;
			*at++ = *arg++;
			while (*arg && *arg != ',' && *arg != '}' && *arg != ']' && *arg != ':') *at++ = *arg++;
			--arg;
			*at = 0;
			TrimSpaces(word);

			if (!strcmp(word,(char*)"null") || !strcmp(word,(char*)"false") || !strcmp(word,(char*)"true")) fieldType = 0; // simple literal
			if (fieldType == 1 || fieldType == 2) *buffer++ = '"';	// field name quote
			if (fieldType == 1 || fieldType == 2) AddEscapes(buffer, word, true);
			else strcpy(buffer,word);
			buffer += strlen(buffer);
			if (fieldType == 1 || fieldType == 2) *buffer++ = '"';	// field name closing quote
			if (level == 0) kind[0] = DIDONE;
			if (field == 2) field = 0;
		}
	}
	if (*arg) // didnt finish, must have been faulty format
	{
		*original = 0;
		return FAILRULE_BIT;
	}
	*buffer = 0;
	return NOPROBLEM_BIT;
}

MEANING jsonValue(char* value, unsigned int& flags) 
{
	bool number = true;
	int decimal = 0;
	char* at = value;
	if (*at == '+') ++at;
	else if (*at == '-') ++at;
	int exponent = 0;
	// numbers cannot start with a zero unless next character is an exponent or a decimal or nothing
	// and numbers cannot start with an exponent or a decimal
	if (*at == '0' && *(at+1) && !(*(at+1) == 'e' || *(at+1) == 'E' || *(at+1) == '.')) number = false;
	else if (*at == 'e' || *at == 'E' || *at == '.') number = false;
	while (*at && number) 
	{  
		if (*at == 'e' || *at == 'E')
		{
			if (IsDigit(*(at-1))) ++exponent;
			else break;
		}
		if (*at == '.') ++decimal;
		else if (!IsDigit(*at)) break; // end of a number maybe
		++at;
	}
	if (*at  || decimal > 1 || exponent > 1) number = false;
	if (!*value || (*value == '"' && value[1] == '"' && strlen(value) == 2)) // treat empty strings as null
	{
		flags |= JSON_PRIMITIVE_VALUE;
		strcpy(value,"null");
	}
	else if (*value == '"') // explicit string or just a quote
	{
		flags |= JSON_STRING_VALUE;
		// strip off quotes for CS, replace later in jsonwrite for output
		// special characters are also escaped later on serialization
		size_t len = strlen(value);
		if (len > 2 && value[len-1] == '"') // dont touch " or "" 
		{
			value[--len] = 0;
			++value; 
		}
	}
	else if (!strnicmp(value,(char*)"jo-",3)) flags |= JSON_OBJECT_VALUE;
	else if (!strnicmp(value,(char*)"ja-",3))  flags |= JSON_ARRAY_VALUE;
	else if (!stricmp(value,(char*)"true"))  flags |= JSON_PRIMITIVE_VALUE;
	else if (!stricmp(value,(char*)"false"))  flags |= JSON_PRIMITIVE_VALUE;
	else if (!stricmp(value,(char*)"null"))  flags |= JSON_PRIMITIVE_VALUE;
	else if (!*value)  // empty string treat as null
	{
		flags |= JSON_PRIMITIVE_VALUE;
		value = "null";
	}
	else if (number) flags |= JSON_PRIMITIVE_VALUE;
	else flags |= JSON_STRING_VALUE; // all others are also strings but without quotes

	WORDP V = StoreWord(value,AS_IS); // new value
	return MakeMeaning(V);
}

FunctionResult JSONObjectInsertCode(char* buffer) //  objectname objectkey objectvalue  
{
	int index = JSONArgs();
	unsigned int flags = JSON_OBJECT_FACT | jsonPermanent;
	char* objectname = ARGUMENT(index++);
	if (strnicmp(objectname,(char*)"jo-",3)) return FAILRULE_BIT;
	WORDP D = FindWord(objectname);
	if (!D) return FAILRULE_BIT;

	char* keyname = ARGUMENT(index++);
	if (*keyname == '"') 
	{
		size_t len = strlen(keyname);
		if (keyname[len-1] == '"') keyname[--len] = 0;
		++keyname;
	}

	WORDP keyvalue = StoreWord(keyname,AS_IS); // new key - can be ANYTHING
	char* val = ARGUMENT(index);
	MEANING key = MakeMeaning(keyvalue);
	MEANING object = MakeMeaning(D);

	// remove old value if it exists, do not allow multiple values UNLESS jsonDuplicate is set
	FACT* F = GetSubjectNondeadHead(D);
	if (jsonDuplicate) F = NULL; // allow multiple values - if not allowing multiple values, remove all
	while (F)	// already there, delete it, 
	{
		FACT* G = GetSubjectNondeadNext(F);
		if (F->verb == key)
		{
			WORDP object = Meaning2Word(F->object);
			FACT* H = GetObjectNondeadHead(object);
			bool jsonkill = false;
			if (safeJsonParse) jsonkill = false; // dont destroy (assumed stored on global var)
			else if (!H) jsonkill = true; // should never be true, since this fact counts
			else if (!GetObjectNondeadNext(H)) jsonkill = true;
			KillFact(F,jsonkill);
		}
		F = G;
	}

	if (stricmp(val,"null")) // null means the removal, "" means the use of JSON null
	{
		MEANING value = jsonValue(val,flags);
		CreateFact(object, key,value, flags);
	}
	currentFact = NULL;	 // used up by putting into json
	return NOPROBLEM_BIT;
}

FunctionResult JSONVariableAssign(char* word,char* value)
{
	char variable[MAX_WORD_SIZE];
	// get the object referred to
	char* separator = strchr(word+1,'.'); // find first level - we MUST find a dot because we dont allow direct array ops
	if (!separator) return FAILRULE_BIT;
	char* bracket = strchr(word+1,'['); 
	if (bracket && bracket < separator) separator = bracket;
	char c = *separator;
	*separator = 0;

	char* val = GetUserVariable(word); // gets the initial variable
	if (*separator == '.' && strnicmp(val,"jo-",3)) 
		return FAILRULE_BIT;	// not a json object
	else if (*separator == '[' && strnicmp(val,"ja-",3)) 
		return FAILRULE_BIT;	// not a json array
	strcpy(variable,val);
	WORDP objectname = FindWord(val);
	if (!objectname) 
		return FAILRULE_BIT;	// doesnt exist?
	WORDP base = FindWord(word);

LOOP: // now we look at $x.key or $x[0]
	*separator = c; // are we about to do .key or [array]

	// is there a followon after this or is it final
	char* separator1 = strchr(separator+1,'.');
	char* bracket1 = strchr(separator+1,'[');
	if (separator1 && bracket1 && bracket1 < separator1) separator1 = bracket1;
	else if (!separator1 && bracket1) separator1 = bracket1;
	if (separator1) // the current level is not the end, there will be more
	{
		if (*(separator1-1) == ']') --separator1;	// true end of key token if we have $x[5][$t]
		c = *separator1;
		*separator1 = 0; // end of current key name
	}

	// get final key to use
	WORDP keyname;
	char keyx[MAX_WORD_SIZE];
	strcpy(keyx,separator+1);
	if (*keyx == '$') // indirection key
	{
		char* answer = GetUserVariable(keyx);
		strcpy(keyx,answer);
		if (*keyx == '$') 
		{
			if (trace & TRACE_VARIABLESET) Log(STDTRACELOG,(char*)"JsonVar: %s.%s\r\n",variable,keyx);
			return FAILRULE_BIT;	// cannot be indirection
		}
	}
	// now we have retrieved the key/index
	if (separator1) keyname = FindWord(keyx);// its a key along the way
	else keyname =  StoreWord(keyx,AS_IS); // its a terminal key, create it (not needed for arrays because we dont allow blind assignment)
	if (!keyname) 
	{
		if (trace &  TRACE_VARIABLESET) Log(STDTRACELOG,(char*)"JsonVar: %s\r\n",variable);
		return FAILRULE_BIT;	// unable to find key or index
	}
	if (*separator == '.') strcat(variable,".");
	else strcat(variable,".[");
	strcat(variable,keyx);
	if (*separator == '[') strcat(variable,"]");
	if (separator1) *separator1 = c; // restore normal values

	// does it exist, we must find this and keep going if there is something later
	if (separator1) 
	{
		FACT* F = GetSubjectNondeadHead(objectname);
		while (F)
		{
			if (F->verb == MakeMeaning(keyname)) break;
			F = GetSubjectNondeadNext(F);
		}
		if (!F) return FAILRULE_BIT;
		objectname = Meaning2Word(F->object);
		separator = separator1;
		if (*separator == ']') ++separator;
		c = *separator;
		goto LOOP;
	}

	// now at final resting place
	unsigned int flags = JSON_OBJECT_FACT;
	if (objectname->word[3] == 't') flags |= FACTTRANSIENT; // like jo-t34

	MEANING object = MakeMeaning(objectname);
	MEANING key = MakeMeaning(keyname);

	// remove old value if it exists, do not allow multiple values
	FACT* F = GetSubjectNondeadHead(objectname);
	while (F)	// already there, delete it if not referenced elsewhere
	{
		if (F->verb == key)
		{
			WORDP object = Meaning2Word(F->object);
			FACT* G = GetObjectNondeadHead(object);
			bool jsonkill = false;
			if (!G) jsonkill = true; // should never be true, since this fact counts
			else if (!GetObjectNondeadNext(G)) jsonkill = true;
			if (trace & TRACE_VARIABLESET) 
			{
				char* recurse = (jsonkill) ? (char*)"recurse" : (char*)"once";
				Log(STDTRACETABLOG,(char*)"JsonVar kill: %s %s ",variable,recurse);
				TraceFact(F,true);
			}
			KillFact(F,jsonkill);

			break;
		}
		F = GetSubjectNondeadNext(F);
	}

	if (stricmp(value,"null")) // not deleting using json literal   ^"" or "" would be the literal null in json
	{
		MEANING valx = jsonValue(value,flags);
		CreateFact(object, key,valx, flags);
	}
	if (trace & TRACE_VARIABLESET) Log(STDTRACELOG,(char*)"JsonVar: %s -> %s\r\n",variable,value);
	
	if (base->internalBits & MACRO_TRACE) 
	{
		char pattern[100];
		char label[MAX_LABEL_SIZE];
		char* ptr = GetPattern(currentRule,label,pattern,100);  // go to output
		Log(ECHOSTDTRACELOG,"%s -> %s at %s.%d.%d %s %s\r\n",word,value, GetTopicName(currentTopicID),TOPLEVELID(currentRuleID),REJOINDERID(currentRuleID),label,pattern);
	}

	
	currentFact = NULL;	 // used up by putting into json
	return NOPROBLEM_BIT;
}

FunctionResult JSONArraySizeCode(char* buffer) // like ^length()
{
	return LengthCode(buffer); // a rename only
}

FunctionResult JSONArrayDeleteCode(char* buffer) //  array, index	
{
	FACT* stack[JSON_LIMIT];
	char arrayname[MAX_WORD_SIZE];
	int index = 0;
	MEANING object = 0;
	int argument = 1;
	char* arg1 = ARGUMENT(1);
	bool useIndex = true;
	MakeLowerCase(arg1);

	// check mode of use 
	bool all = false;
	bool safe = false;
	if (strstr(arg1,"index")) index = atoi(ARGUMENT(3)); 
	else if (strstr(arg1,"value"))
	{
		char* match = ARGUMENT(3);
		WORDP D = FindWord(match);
		if (!D) return FAILRULE_BIT;
		object = MakeMeaning(D);
		useIndex = false;
	}
	else return FAILRULE_BIT; 
	if (strstr(arg1,"all")) all = true; // default is one but can do ALL
	if (strstr(arg1,"safe")) safe = true; // default is delete

	// get array and prove it legal
	strcpy(arrayname,ARGUMENT(2));
	if (strnicmp(arrayname,(char*)"ja-",3)) return FAILRULE_BIT;
	WORDP O = FindWord(arrayname);
	if (!O) return FAILRULE_BIT;

	bool once = false;
DOALL:	
	// find the fact we want (we only delete 1 fact per call) if you have dups say all
	FACT* F = GetSubjectNondeadHead(O);
	while (F) 
	{
		if (useIndex)
		{
			int val = atoi(Meaning2Word(F->verb)->word);
			if (val == index) break;	// found it
		}
		else if (object == F->object) break;
		F = GetSubjectNondeadNext(F);
	}
	if (!F) 
	{
		if (once) return NOPROBLEM_BIT;
		return FAILRULE_BIT;		// not findable.
	}
	once = true;
	int indexsize = orderJsonArrayMembers(O, stack,JSON_LIMIT); 
	WORDP objectx = Meaning2Word(F->object);
	FACT* G = GetObjectNondeadHead(objectx);
	bool jsonkill = false;
	if (safe) jsonkill = false;
	else if (!G) jsonkill = true; // should never be true, since this fact counts
	else if (!GetObjectNondeadNext(G)) jsonkill = true;
	KillFact(F,jsonkill);		// delete it, not recursive json structure, just array element
	for (int i = index+1; i < indexsize; ++i) // renumber these downwards
	{
		F = stack[i];
		ARGUMENT(2) = AllocateInverseString(arrayname);
		char num[MAX_WORD_SIZE];
		sprintf(num,"%d",Fact2Index(F));
		ARGUMENT(1) = AllocateInverseString(num);
		sprintf(num,"%d",i-1);
		ARGUMENT(3) = AllocateInverseString(num);
		ARGUMENT(4) = AllocateInverseString(Meaning2Word(F->object)->word);
		FunctionResult result = ReviseFactCode(buffer);
	}
	if (all) goto DOALL; // keep trying
	return NOPROBLEM_BIT;
}

FunctionResult JSONArrayInsertCode(char* buffer) //  objectfact objectvalue  BEFORE/AFTER 
{	
	int index = JSONArgs();
	unsigned int flags = JSON_ARRAY_FACT | jsonPermanent;

	char* arrayname = ARGUMENT(index++);
	if (strnicmp(arrayname,(char*)"ja-",3)) return FAILRULE_BIT;
	WORDP O = FindWord(arrayname);
	if (!O) return FAILRULE_BIT;

	// get the field values
	char arrayIndex[20];
	char* val = ARGUMENT(index);
	MEANING value = jsonValue(val,flags);
	
	// how many existing elements
	FACT* F = GetSubjectNondeadHead(O);
	int count = 0;
	FACT* lastF = F;
	while (F) 
	{
		lastF = F;
		if (jsonNoduplicate && F->object == value)  return NOPROBLEM_BIT;	// already there UNIQUE flag given else defaults DUPLICATE
		++count;
		F = GetSubjectNondeadNext(F);
	}

	// you may NOT add permanent values after transient values lest earlier elements disappear out of order
	if (lastF && lastF->flags & FACTTRANSIENT)  flags |=  FACTTRANSIENT;	

	sprintf(arrayIndex,(char*)"%d",count); // add at end
	WORDP Idex = StoreWord(arrayIndex);

	// create fact
	CreateFact(MakeMeaning(O), MakeMeaning(Idex),value, flags);
	currentFact = NULL;	 // used up by putting into json
	return NOPROBLEM_BIT;
}

FunctionResult JSONCopyCode(char* buffer)
{
	int index = JSONArgs();
	char* arg = ARGUMENT(index++);
	WORDP D = FindWord(arg);
	if (!D || (strncmp(D->word,(char*)"ja-",3) && strncmp(D->word,(char*)"jo-",3))) 
	{
		strcpy(buffer,arg);
		return NOPROBLEM_BIT;
	}
	MEANING M = jcopy(D);
	currentFact = NULL; // used up in json
	D = Meaning2Word(M);
	strcpy(buffer,D->word);
	return NOPROBLEM_BIT;
}

FunctionResult JSONCreateCode(char* buffer) 
{
	int index = JSONArgs(); // not needed but allowed
	char* arg = ARGUMENT(index);
	MEANING M;
	if (!stricmp(arg,(char*)"array")) M = GetUniqueJsonComposite((char*)"ja-") ;
	else if (!stricmp(arg,(char*)"object"))  M = GetUniqueJsonComposite((char*)"jo-") ;
	else return FAILRULE_BIT;
	sprintf(buffer, "%s", Meaning2Word(M)->word);
	return NOPROBLEM_BIT;
}

void JsonRenumber(FACT* G) // given array fact dying, renumber after it
{
	FACT* stack[JSON_LIMIT];
	WORDP D = Meaning2Word(G->subject);
	int index = atoi(Meaning2Word(G->verb)->word);
	int indexsize = orderJsonArrayMembers(D, stack,JSON_LIMIT); 
	for (int i = index+1; i < indexsize; ++i) // renumber these downwards
	{
		FACT* F = stack[i];
		char val[MAX_WORD_SIZE];
		sprintf(val,"%d",i-1);
		WORDP oldverb = Meaning2Word(F->verb);
		WORDP newverb = StoreWord(val);
		FACT* X = DeleteFromList(GetVerbHead(oldverb),F,GetVerbNext,SetVerbNext);  // dont use nondead
		SetVerbHead(oldverb,X);
		X = AddToList(GetVerbHead(newverb),F,GetVerbNext,SetVerbNext);  // dont use nondead
		SetVerbHead(newverb,X);
		F->verb = MakeMeaning(newverb);
	}
}

FunctionResult JSONDeleteCode(char* buffer) 
{
	char* arg = ARGUMENT(1); // a json object or array
	WORDP D = FindWord(arg);
	if (!D)  return NOPROBLEM_BIT;

	FACT* F = GetSubjectNondeadHead(D); // if we have someone below us, not json, thats a problem
	if (F && !(F->flags & JSON_FLAGS)) return FAILRULE_BIT;
	if (F) jkillfact(D); // kill everything we own

	// remove upper link to us if someone above us uses us JSON wise
	F = GetObjectNondeadHead(D); // who are we the object of, can only be one
	while (F)
	{
		if (F->flags & (JSON_ARRAY_FACT | JSON_OBJECT_FACT)) // we are object of array or json object
		{
			KillFact(F); 
			break;
		}
		F = GetObjectNondeadNext(F);
	}
	
	// now confirm we have nothing left
	F = GetSubjectNondeadHead(D); // should all be dead now
	if (F && !(F->flags & JSON_FLAGS)) return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

#endif // ---------------------------- END  : CURL/JSON related code ---------------------
