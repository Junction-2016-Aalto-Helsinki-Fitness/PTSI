#include "common.h"
#include "evserver.h"
char* version = "6.9";
char sourceInput[200];
bool pendingRestart = false;
bool pendingUserReset = false;
bool assignedLogin = false;
int sentencePreparationIndex = 0;
int lastRestoredIndex = 0;
#define MAX_RETRIES 20
clock_t startTimeInfo;							// start time of current volley
char revertBuffer[MAX_BUFFER_SIZE];			// copy of user input so can :revert if desired
int argc;
char ** argv;
char postProcessing = 0;						// copy of output generated during MAIN control. Postprocessing can prepend to it
unsigned int tokenCount;						// for document performc
bool callback = false;						// when input is callback,alarm,loopback, dont want to enable :retry for that...
int timerLimit = 0;						// limit time per volley
int timerCheckRate = 0;					// how often to check calls for time
clock_t volleyStartTime = 0;
int timerCheckInstance = 0;
static char* privateParams = NULL;
static char* treetaggerParams = NULL;
char* encryptParams = NULL;
char* decryptParams = NULL;
char hostname[100];
bool nosuchbotrestart = false; // restart if no such bot
char users[100];
char logs[100];
char* derivationSentence[MAX_SENTENCE_LENGTH];
int derivationLength;
char* authorizations = 0;	// for allowing debug commands
char ourMainInputBuffer[MAX_BUFFER_SIZE];				// user input buffer - ptr to primaryInputBuffer
char* mainInputBuffer;								// user input buffer - ptr to primaryInputBuffer
char ourMainOutputBuffer[MAX_BUFFER_SIZE];				// main user output buffer  BUG??? why not direct
char* mainOutputBuffer;								// user input buffer - ptr to primaryInputBuffer
char* readBuffer;								// main scratch reading buffer (files, etc)
bool readingDocument = false;
bool redo = false; // retry backwards any amount
bool oobExists = false;
bool docstats = false;
unsigned int docSentenceCount = 0;
char rawSentenceCopy[MAX_BUFFER_SIZE]; // current raw sentence
char *evsrv_arg = NULL;

unsigned short int derivationIndex[256];

bool overrideAuthorization = false;

clock_t  startSystem;						// time chatscript started
unsigned int choiceCount = 0;
int always = 1;									// just something to stop the complaint about a loop based on a constant

// support for callbacks
unsigned long callBackTime = 0;			// one-shot pending time - forces callback with "[callback]" when output was [callback=xxx] when no user submitted yet
unsigned long callBackDelay = 0;			// one-shot pending time - forces callback with "[callback]" when output was [callback=xxx] when no user submitted yet
unsigned long loopBackTime = 0;	// ongoing callback forces callback with "[loopback]"  when output was "[loopback=xxx]"
unsigned long loopBackDelay = 0;	// ongoing callback forces callback with "[loopback]"  when output was "[loopback=xxx]"
unsigned long alarmTime = 0;				
unsigned long alarmDelay = 0;					// one-shot pending time - forces callback with "[alarm]" when output was [callback=xxx] when no user submitted yet
unsigned int outputLength = 100000;		// max output before breaking.

char* extraTopicData = 0;				// supplemental topic set by :extratopic

// server data
#ifdef DISCARDSERVER
bool server = false;
#else
std::string interfaceKind = "0.0.0.0";
#ifdef WIN32
bool server = false;	// default is standalone on Windows
#elif IOS
bool server = true; // default is server on LINUX
#else
bool server = true; // default is server on LINUX
#endif
#endif
unsigned int port = 1024;						// server port
bool commandLineCompile = false;

PrepareMode tmpPrepareMode = NO_MODE;						// controls what processing is done in preparation NO_MODE, POS_MODE, PREPARE_MODE
PrepareMode prepareMode = NO_MODE;						// controls what processing is done in preparation NO_MODE, POS_MODE, PREPARE_MODE
bool noReact = false;

// :source:document data
bool documentMode = false;						// read input as a document not as chat
FILE* sourceFile = NULL;						// file to use for :source
EchoSource echoSource = NO_SOURCE_ECHO;			// for :source, echo that input to nowhere, user, or log
unsigned long sourceStart = 0;					// beginning time of source file
unsigned int sourceTokens = 0;
unsigned int sourceLines = 0;

// status of user input
unsigned int volleyCount = 0;					// which user volley is this
bool moreToComeQuestion = false;				// is there a ? in later sentences
bool moreToCome = false;						// are there more sentences pending
uint64 tokenControl = 0;						// results from tokenization and prepare processing
unsigned int responseControl = ALL_RESPONSES;					// std output behaviors
char* nextInput;								// ptr to rest of users input after current sentence
static char oldInputBuffer[MAX_BUFFER_SIZE];			//  copy of the sentence we are processing
char currentInput[MAX_BUFFER_SIZE];			// the sentence we are processing  BUG why both

// general display and flow controls
bool quitting = false;							// intending to exit chatbot
int systemReset = 0;							// intending to reload system - 1 (mild reset) 2 (full user reset)
bool autonumber = false;						// display number of volley to user
bool showWhy = false;							// show user rules generating his output
bool showTopic = false;							// show resulting topic on output
bool showTopics = false;						// show all relevant topics
bool showInput = false;							// Log internal input additions
bool showReject = false;						// Log internal repeat rejections additions
bool all = false;								// generate all possible answers to input
int regression = NO_REGRESSION;						// regression testing in progress
unsigned int trace = 0;							// current tracing flags
char* bootcmd = NULL;						// current boot tracing flags
unsigned int timing = 0;						// current timing flags
bool shortPos = false;							// display pos results as you go

int inputRetryRejoinderTopic = NO_REJOINDER;				
int inputRetryRejoinderRuleID = NO_REJOINDER;
int sentenceRetryRejoinderTopic = NO_REJOINDER;				
int sentenceRetryRejoinderRuleID = NO_REJOINDER;

char oktest[MAX_WORD_SIZE];						// auto response test
char activeTopic[200];

char respondLevel = 0;							// rejoinder level of a random choice block
static bool oobPossible = false;						// 1st thing could be oob

int inputCounter = 0;							// protecting ^input from cycling
int totalCounter = 0;							// protecting ^input from cycling

static char userPrefix[MAX_WORD_SIZE];			// label prefix for user input
static char botPrefix[MAX_WORD_SIZE];			// label prefix for bot output

bool unusedRejoinder;							// inputRejoinder has been executed, blocking further calls to ^Rejoinder
	
char inputCopy[MAX_BUFFER_SIZE]; // the original input we were given, we will work on this

// outputs generated
RESPONSE responseData[MAX_RESPONSE_SENTENCES+1];
unsigned char responseOrder[MAX_RESPONSE_SENTENCES+1];
int responseIndex;

int inputSentenceCount;				// which sentence of volley is this

///////////////////////////////////////////////
/// SYSTEM STARTUP AND SHUTDOWN
///////////////////////////////////////////////

void InitStandalone()
{
	startSystem =  clock()  / CLOCKS_PER_SEC;
	*currentFilename = 0;	//   no files are open (if logging bugs)
	tokenControl = 0;
	*computerID = 0; // default bot
}

void CreateSystem()
{
	overrideAuthorization = false;	// always start safe
	loading = true;
	char* os;
	mystart("createsystem");
#ifdef WIN32
	os = "Windows";
#elif IOS
	os = "IOS";
#elif __MACH__
	os = "MACH";
#else
	os = "LINUX";
#endif

	if (!buffers) // restart asking for new memory allocations
	{
		maxBufferSize = (maxBufferSize + 63);
		maxBufferSize &= 0xffffffC0; // force 64 bit align
		unsigned int total = maxBufferLimit * maxBufferSize;
		buffers = (char*) malloc(total); // have it around already for messages
		if (!buffers)
		{
			printf((char*)"%s",(char*)"cannot allocate buffer space");
			exit(1);
		}
		bufferIndex = 0;
		readBuffer = AllocateBuffer();
		joinBuffer = AllocateBuffer();
		newBuffer = AllocateBuffer();
		baseBufferIndex = bufferIndex;
	}
	char data[MAX_WORD_SIZE];
	char* kind;
	int pid = 0;
#ifdef EVSERVER
	kind = "EVSERVER";
	pid = getpid();
#elif DEBUG
	kind = "Debug";
#else
	kind = "Release";
#endif
	sprintf(data,(char*)"ChatScript %s Version %s pid: %d %ld bit %s compiled %s",kind,version,pid,(long int)(sizeof(char*) * 8),os,compileDate);
	strcat(data,(char*)" host=");
	strcat(data,hostname);
	if (server)  Log(SERVERLOG,(char*)"Server %s\r\n",data);
	strcat(data,(char*)"\r\n");
	printf((char*)"%s",data);

	int oldtrace = trace; // in case trace turned on by default
	trace = 0;
	*oktest = 0;

	sprintf(data,(char*)"Params:   dict:%ld fact:%ld text:%ldkb hash:%ld \r\n",(long int)maxDictEntries,(long int)maxFacts,(long int)(maxStringBytes/1000),(long int)maxHashBuckets);
	if (server) Log(SERVERLOG,(char*)"%s",data);
	else printf((char*)"%s",data);
	sprintf(data,(char*)"          buffer:%dx%dkb cache:%dx%dkb userfacts:%d\r\n",(int)maxBufferLimit,(int)(maxBufferSize/1000),(int)userCacheCount,(int)(userCacheSize/1000),(int)userFactCount);
	if (server) Log(SERVERLOG,(char*)"%s",data);
	else printf((char*)"%s",data);
	InitScriptSystem();
	InitVariableSystem();
	ReloadSystem();			// builds base facts and dictionary (from wordnet)
	LoadTopicSystem();		// dictionary reverts to wordnet zone
	*currentFilename = 0;
	computerID[0] = 0;
	if (!assignedLogin) loginName[0] = loginID[0] = 0;
	*botPrefix = *userPrefix = 0;

	char word[MAX_WORD_SIZE];
	for (int i = 1; i < argc; ++i)
	{
		strcpy(word,argv[i]);
		if (*word == '"' && word[1] == 'V') 
		{
			memmove(word,word+1,strlen(word));
			size_t len = strlen(word);
			if (word[len-1] == '"') word[len-1] = 0;
		}
		if (*word == 'V') // predefined bot variable
		{
			char* eq = strchr(word,'=');
			if (eq) 
			{

				*eq = 0;
				ReturnToAfterLayer(1,true);
				*word = USERVAR_PREFIX;
				SetUserVariable(word,eq+1);
				if (server) Log(SERVERLOG,(char*)"botvariable: %s = %s\r\n",word,eq+1);
				else printf((char*)"botvariable: %s = %s\r\n",word,eq+1);
  				NoteBotVariables();
				LockLayer(1,false);
			}
		}
	}
	WORDP boot = FindWord((char*)"^csboot");
	if (boot) // run script on startup of system. data it generates will also be layer 1 data
	{
		int oldtrace = trace;
		if (bootcmd) 	
		{
			if (*bootcmd == '"') 
			{
				++bootcmd;
				bootcmd[strlen(bootcmd)-1] = 0;
			}
			DoCommand(bootcmd,NULL,false);
		}
		UnlockLevel(); // unlock it to add stuff
		FACT* F = factFree;
		Callback(boot,(char*)"()",true); // do before world is locked
		while (++F <= factFree) 
		{
			if (F->flags & FACTTRANSIENT) 
				F->flags |= FACTDEAD;
			else F->flags |= FACTBUILD1; // convert these to level 1
		}
		NoteBotVariables(); // convert user variables read into bot variables
		LockLayer(1,false); // rewrite level 2 start data with augmented from script data
		trace = (modifiedTrace) ? modifiedTraceVal : oldtrace;
		stringInverseFree = stringInverseStart; // drop any possible inverse used
	}
	InitSpellCheck(); // after boot vocabulary added

	unsigned int factUsedMemKB = ( factFree-factBase) * sizeof(FACT) / 1000;
	unsigned int factFreeMemKB = ( factEnd-factFree) * sizeof(FACT) / 1000;
	unsigned int dictUsedMemKB = ( dictionaryFree-dictionaryBase) * sizeof(WORDENTRY) / 1000;
	// dictfree shares text space
	unsigned int textUsedMemKB = ( stringBase-stringFree)  / 1000;
#ifndef SEPARATE_STRING_SPACE 
	char* endDict = (char*)(dictionaryBase + maxDictEntries);
	unsigned int textFreeMemKB = ( stringFree- endDict) / 1000;
#else
	unsigned int textFreeMemKB = ( stringFree- stringEnd) / 1000;
#endif

	unsigned int bufferMemKB = (maxBufferLimit * maxBufferSize) / 1000;
	
	unsigned int used =  factUsedMemKB + dictUsedMemKB + textUsedMemKB + bufferMemKB;
	used +=  (userTopicStoreSize + userTableSize) /1000;
	unsigned int free = factFreeMemKB + textFreeMemKB;

	unsigned int bytes = (tagRuleCount * MAX_TAG_FIELDS * sizeof(uint64)) / 1000;
	used += bytes;
	char buf2[MAX_WORD_SIZE];
	char buf1[MAX_WORD_SIZE];
	char buf[MAX_WORD_SIZE];
	strcpy(buf,StdIntOutput(factFree-factBase));
	strcpy(buf2,StdIntOutput(textFreeMemKB));
	unsigned int hole = 0;
	unsigned int maxdepth = 0;
	unsigned int count = 0;
	for (WORDP D = dictionaryBase+1; D < dictionaryFree; ++D) 
	{
		if (!D->word) ++hole; 
		else
		{
			unsigned int n = 1;
			WORDP X = D; 
			while (X != dictionaryBase)
			{
				++n;
				X = dictionaryBase + GETNEXTNODE(X);
			}  
			if (n > maxdepth) 
			{
				maxdepth = n;
				count = 1;
			}
			else if (n == maxdepth) ++count;
		}
	}
	sprintf(data,(char*)"Used %dMB: dict %s (%dkb) hashdepth %d/%d fact %s (%dkb) text %dkb\r\n",
		used/1000,
		StdIntOutput(dictionaryFree-dictionaryBase), 
		dictUsedMemKB,maxdepth,count,
		buf,
		factUsedMemKB,
		textUsedMemKB);
	if (server) Log(SERVERLOG,(char*)"%s",data);
	else printf((char*)"%s",data);

	sprintf(data,(char*)"           buffer (%dkb) cache (%dkb) POS: %d (%dkb)\r\n",
		bufferMemKB,
		(userTopicStoreSize + userTableSize)/1000,
		tagRuleCount,
		bytes);
	if(server) Log(SERVERLOG,(char*)"%s",data);
	else printf((char*)"%s",data);

	strcpy(buf,StdIntOutput(factEnd-factFree)); // unused facts
	strcpy(buf1,StdIntOutput(textFreeMemKB)); // unused text
	strcpy(buf2,StdIntOutput(free/1000));

	sprintf(data,(char*)"Free %sMB: dict %s hash %d fact %s text %sKB \r\n\r\n",buf2,StdIntOutput(((unsigned int)maxDictEntries)-(dictionaryFree-dictionaryBase)),hole,buf,buf1);
	if (server) Log(SERVERLOG,(char*)"%s",data);
	else printf((char*)"%s",data);

	trace = oldtrace;
#ifdef DISCARDSERVER 
	printf((char*)"    Server disabled.\r\n");
#endif
#ifdef DISCARDSCRIPTCOMPILER
	if(server) Log(SERVERLOG,(char*)"    Script compiler disabled.\r\n");
	else printf((char*)"    Script compiler disabled.\r\n");
#endif
#ifdef DISCARDTESTING
	if(server) Log(SERVERLOG,(char*)"    Testing disabled.\r\n");
	else printf((char*)"    Testing disabled.\r\n");
#else
	*callerIP = 0;
	if (!assignedLogin) *loginID = 0;
	if (server && VerifyAuthorization(FopenReadOnly((char*)"authorizedIP.txt"))) // authorizedIP
	{
		Log(SERVERLOG,(char*)"    *** Server WIDE OPEN to :command use.\r\n");
	}
#endif
#ifdef DISCARDDICTIONARYBUILD
	printf((char*)"%s",(char*)"    Dictionary building disabled.\r\n");
#endif
#ifdef DISCARDJSON
	printf((char*)"%s",(char*)"    JSON access disabled.\r\n");
#endif
	char route[MAX_WORD_SIZE];
#ifndef DISCARDPOSTGRES
	if (postgresparams) sprintf(route,"    Postgres enabled. FileSystem routed to %s\r\n",postgresparams);
	else sprintf(route,"    Postgres enabled.\r\n"); 
	if (server) Log(SERVERLOG,route);
	else printf(route);
#endif
#ifndef DISCARDMONGO
	if (mongodbparams) sprintf(route,"    Mongo enabled. FileSystem routed to %s\r\n",mongodbparams);
	else sprintf(route,"    Mongo enabled.\r\n"); 
	if (server) Log(SERVERLOG,route);
	else printf(route);
#endif
	printf((char*)"%s",(char*)"\r\n");
	loading = false;
}

void ReloadSystem()
{//   reset the basic system through wordnet but before topics
	InitFacts(); 
	InitDictionary();
	// make sets for the part of speech data
	LoadDictionary();
	InitFunctionSystem();
#ifndef DISCARDTESTING
	InitCommandSystem();
#endif
	ExtendDictionary(); // store labels of concepts onto variables.
	DefineSystemVariables();
	ClearUserVariables();

	if (!ReadBinaryFacts(FopenStaticReadOnly(UseDictionaryFile((char*)"facts.bin"))))  // DICT
	{
		WORDP safeDict = dictionaryFree;
		ReadFacts(UseDictionaryFile((char*)"facts.txt"),NULL,0);
		if ( safeDict != dictionaryFree) myexit((char*)"dict changed on read of facts");
		WriteBinaryFacts(FopenBinaryWrite(UseDictionaryFile((char*)"facts.bin")),factBase);
	}
	char name[MAX_WORD_SIZE];
	sprintf(name,(char*)"%s/systemfacts.txt",systemFolder);
	ReadFacts(name,NULL,0); // part of wordnet, not level 0 build 
	ReadLiveData();  // considered part of basic system before a build
	WordnetLockDictionary();
}

void ProcessArguments(int argc, char* argv[])
{
	for (int i = 1; i < argc; ++i)
	{
		if (!stricmp(argv[i],(char*)"trace")) trace = (unsigned int) -1; 
		else if (!stricmp(argv[i], (char*)"time")) timing = (unsigned int)-1 ^ TIME_ALWAYS;
		else if (!strnicmp(argv[i],(char*)"bootcmd=",8)) bootcmd = argv[i]+8; 
		else if (!strnicmp(argv[i],(char*)"dir=",4))
		{
#ifdef WIN32
			if (!SetCurrentDirectory(argv[i]+4)) printf((char*)"unable to change to %s\r\n",argv[i]+4);
#else
			chdir(argv[i]+5);
#endif
		}
		else if (!strnicmp(argv[i],(char*)"source=",7)) 
		{
			if (argv[i][7] == '"') 
			{
				strcpy(sourceInput,argv[i]+8);
				size_t len = strlen(sourceInput);
				if (sourceInput[len-1] == '"') sourceInput[len-1] = 0;
			}
			else strcpy(sourceInput,argv[i]+7);
		}
		else if (!strnicmp(argv[i],(char*)"login=",6)) 
		{
			assignedLogin = true;
			strcpy(loginID,argv[i]+6);
		}
		else if (!strnicmp(argv[i],(char*)"output=",7)) outputLength = atoi(argv[i]+7);
		else if (!strnicmp(argv[i],(char*)"save=",5)) 
		{
			volleyLimit = atoi(argv[i]+5);
			if (volleyLimit > 255) volleyLimit = 255; // cant store higher
		}

		// memory sizings
		else if (!strnicmp(argv[i],(char*)"hash=",5)) 
		{
			maxHashBuckets = atoi(argv[i]+5); // size of hash
			setMaxHashBuckets = true;
		}
		else if (!strnicmp(argv[i],(char*)"dict=",5)) maxDictEntries = atoi(argv[i]+5); // how many dict words allowed
		else if (!strnicmp(argv[i],(char*)"fact=",5)) maxFacts = atoi(argv[i]+5);  // fact entries
		else if (!strnicmp(argv[i],(char*)"text=",5)) maxStringBytes = atoi(argv[i]+5) * 1000; // string bytes in pages
		else if (!strnicmp(argv[i],(char*)"cache=",6)) // value of 10x0 means never save user data
		{
			userCacheSize = atoi(argv[i]+6) * 1000;
			char* number = strchr(argv[i]+6,'x');
			if (number) userCacheCount = atoi(number+1);
		}
		else if (!strnicmp(argv[i],(char*)"userfacts=",10)) userFactCount = atoi(argv[i]+10); // how many user facts allowed
		else if (!stricmp(argv[i],(char*)"redo")) redo = true; // enable redo
		else if (!strnicmp(argv[i],(char*)"authorize=",10)) authorizations = argv[i]+10; // whitelist debug commands
		else if (!stricmp(argv[i],(char*)"nodebug")) authorizations = (char*) 1; 
		else if (!strnicmp(argv[i],(char*)"users=",6 )) strcpy(users,argv[i]+6);
		else if (!strnicmp(argv[i],(char*)"logs=",5 )) strcpy(logs,argv[i]+5);
		else if (!strnicmp(argv[i],(char*)"private=",8)) privateParams = argv[i]+8;
		else if (!strnicmp(argv[i],(char*)"treetagger=",11)) treetaggerParams = argv[i]+11;
		else if (!strnicmp(argv[i],(char*)"encrypt=",8)) encryptParams = argv[i]+8;
		else if (!strnicmp(argv[i],(char*)"decrypt=",8)) decryptParams = argv[i]+8;
		else if (!strnicmp(argv[i],(char*)"livedata=",9) ) 
		{
			strcpy(livedata,argv[i]+9);
			sprintf(systemFolder,(char*)"%s/SYSTEM",argv[i]+9);
			sprintf(englishFolder,(char*)"%s/ENGLISH",argv[i]+9);
		}
		else if (!strnicmp(argv[i],(char*)"nosuchbotrestart=",17) ) 
		{
			if (!stricmp(argv[i]+17,"true")) nosuchbotrestart = true;
			else nosuchbotrestart = false;
		}
		else if (!strnicmp(argv[i],(char*)"system=",7) )  strcpy(systemFolder,argv[i]+7);
		else if (!strnicmp(argv[i],(char*)"english=",8) )  strcpy(englishFolder,argv[i]+8);
#ifndef DISCARDPOSTGRES
		else if (!strnicmp(argv[i],(char*)"pguser=",7) )  postgresparams = argv[i]+7;
#endif
#ifndef DISCARDMONGO
		else if (!strnicmp(argv[i],(char*)"mongo=",6) )  mongodbparams = argv[i]+6;
#endif
#ifndef DISCARDCLIENT
		else if (!strnicmp(argv[i],(char*)"client=",7)) // client=1.2.3.4:1024  or  client=localhost:1024
		{
			server = false;
			char buffer[MAX_WORD_SIZE];
			strcpy(serverIP,argv[i]+7);
		
			char* portVal = strchr(serverIP,':');
			if ( portVal)
			{
				*portVal = 0;
				port = atoi(portVal+1);
			}

			if (!*loginID)
			{
				printf((char*)"%s",(char*)"\r\nEnter client user name: ");
				ReadALine(buffer,stdin);
				printf((char*)"%s",(char*)"\r\n");
				Client(buffer);
			}
			else Client(loginID);
			myexit((char*)"client ended");
		}  
#endif
		else if (!stricmp(argv[i],(char*)"userlog")) userLog = true;
		else if (!stricmp(argv[i],(char*)"nouserlog")) userLog = false;
#ifndef DISCARDSERVER
		else if (!stricmp(argv[i],(char*)"serverretry")) serverRetryOK = true;
 		else if (!stricmp(argv[i],(char*)"local")) server = false; // local standalone
		else if (!stricmp(argv[i],(char*)"noserverlog")) serverLog = false;
		else if (!stricmp(argv[i],(char*)"serverlog")) serverLog = true;
		else if (!stricmp(argv[i],(char*)"noserverprelog")) serverPreLog = false;
		else if (!stricmp(argv[i],(char*)"serverctrlz")) serverctrlz = 1;
		else if (!strnicmp(argv[i],(char*)"port=",5))  // be a server
		{
            port = atoi(argv[i]+5); // accept a port=
			sprintf(serverLogfileName,(char*)"%s/serverlog%d.txt",logs,port);
			server = true;
		}
#ifdef EVSERVER
		else if (!strnicmp(argv[i], "fork=", 5)) 
		{
			static char forkCount[10];
			sprintf(forkCount,(char*)"evsrv:fork=%d",atoi(argv[i]+5));
			evsrv_arg = forkCount;
		}
#endif
		else if (!strnicmp(argv[i],(char*)"interface=",10)) interfaceKind = string(argv[i]+10); // specify interface
#endif
	}
}

unsigned int InitSystem(int argcx, char * argvx[],char* unchangedPath, char* readablePath, char* writeablePath, USERFILESYSTEM* userfiles)
{ // this work mostly only happens on first startup, not on a restart
	strcpy(hostname,(char*)"local");
	MakeDirectory((char*)"TMP");
	*sourceInput = 0;
	argc = argcx;
	argv = argvx;
	InitFileSystem(unchangedPath,readablePath,writeablePath);
	if (userfiles) memcpy((void*)&userFileSystem,userfiles,sizeof(userFileSystem));
	for (unsigned int i = 0; i <= MAX_WILDCARDS; ++i)
	{
		*wildcardOriginalText[i] =  *wildcardCanonicalText[i]  = 0; 
		wildcardPosition[i] = 0;
	}
	strcpy(users,(char*)"USERS");
	strcpy(logs,(char*)"LOGS");

	strcpy(language,(char*)"ENGLISH");

	strcpy(livedata,(char*)"LIVEDATA"); // default directory for dynamic stuff
	strcpy(systemFolder,(char*)"LIVEDATA/SYSTEM"); // default directory for dynamic stuff
	strcpy(englishFolder,(char*)"LIVEDATA/ENGLISH"); // default directory for dynamic stuff
	*loginID = 0;

	for (int i = 1; i < argc; ++i)
	{
		if (!strnicmp(argv[i],(char*)"buffer=",7))  // number of large buffers available  8x80000
		{
			maxBufferLimit = atoi(argv[i]+7); 
			char* size = strchr(argv[i]+7,'x');
			if (size) maxBufferSize = atoi(size+1) *1000;
			if (maxBufferSize < OUTPUT_BUFFER_SIZE)
			{
				printf((char*)"Buffer cannot be less than OUTPUT_BUFFER_SIZE of %d\r\n",OUTPUT_BUFFER_SIZE);
				myexit((char*)"buffer size less than output buffer size");
			}
		}
	}

	// need buffers for things that run ahead like servers and such.
	maxBufferSize = (maxBufferSize + 63);
	maxBufferSize &= 0xffffffc0; // force 64 bit align on size  
	unsigned int total = maxBufferLimit * maxBufferSize;
	buffers = (char*) malloc(total); // have it around already for messages
	if (!buffers)
	{
		printf((char*)"%s",(char*)"cannot allocate buffer space");
		return 1;
	}
	bufferIndex = 0;
	readBuffer = AllocateBuffer();
	joinBuffer = AllocateBuffer();
	newBuffer = AllocateBuffer(); // used for script compiling
	baseBufferIndex = bufferIndex;
	quitting = false;
	InitTextUtilities();
    sprintf(logFilename,(char*)"%s/log%d.txt",logs,port); // DEFAULT LOG
	echo = true;	
    sprintf(serverLogfileName,(char*)"%s/serverlog%d.txt",logs,port); // DEFAULT LOG
	ProcessArguments(argc,argv);

	if (redo) autonumber = true;

	// defaults where not specified
	if (userLog == LOGGING_NOT_SET) userLog = 1;	// default ON for user if unspecified
	if (serverLog == LOGGING_NOT_SET) serverLog = 1; // default ON for server if unspecified
	
	int oldserverlog = serverLog;
	serverLog = true;

#ifndef DISCARDSERVER
	if (server)
	{
#ifndef EVSERVER
		GrabPort(); 
#else
		printf("evcalled pid: %d\r\n",getpid());
		if (evsrv_init(interfaceKind, port, evsrv_arg) < 0)  exit(4); // additional params will apply to each child and they will load data each
#endif
#ifdef WIN32
		PrepareServer(); 
#endif
	}
#endif

	if (volleyLimit == -1) volleyLimit = DEFAULT_VOLLEY_LIMIT;
	CreateSystem();

	for (int i = 1; i < argc; ++i)
	{
#ifndef DISCARDSCRIPTCOMPILER
		if (!strnicmp(argv[i],(char*)"build0=",7))
		{
			sprintf(logFilename,(char*)"%s/build0_log.txt",users);
			FILE* in = FopenUTF8Write(logFilename);
			FClose(in);
			commandLineCompile = true;
			int result = ReadTopicFiles(argv[i]+7,BUILD0,NO_SPELL);
 			myexit((char*)"build0 complete",result);
		}  
		if (!strnicmp(argv[i],(char*)"build1=",7))
		{
			sprintf(logFilename,(char*)"%s/build1_log.txt",users);
			FILE* in = FopenUTF8Write(logFilename);
			FClose(in);
			commandLineCompile = true;
			int result = ReadTopicFiles(argv[i]+7,BUILD1,NO_SPELL);
 			myexit((char*)"build1 complete",result);
		}  
#endif
		if (!strnicmp(argv[i],(char*)"timer=",6))
		{
			char* x = strchr(argv[i]+6,'x'); // 15000x10
			if (x)
			{
				*x = 0;
				timerCheckRate = atoi(x+1);
			}
			timerLimit = atoi(argv[i]+6);
		}
#ifndef DISCARDTESTING
		if (!strnicmp(argv[i],(char*)"debug=",6))
		{
			char* ptr = SkipWhitespace(argv[i]+6);
			commandLineCompile = true;
			if (*ptr == ':') DoCommand(ptr,mainOutputBuffer);
 			myexit((char*)"test complete");
		}  
#endif
		if (!stricmp(argv[i],(char*)"trace")) trace = (unsigned int) -1; // make trace work on login
		if (!stricmp(argv[i], (char*)"time")) timing = (unsigned int)-1 ^ TIME_ALWAYS; // make timing work on login
	}

	if (server)
	{
#ifndef EVSERVER
		Log(SERVERLOG, "\r\n\r\n======== Began server %s compiled %s on host %s port %d at %s serverlog:%d userlog: %d\r\n",version,compileDate,hostname,port,GetTimeInfo(true),oldserverlog,userLog);
		printf((char*)"\r\n\r\n======== Began server %s compiled %s on host %s port %d at %s serverlog:%d userlog: %d\r\n",version,compileDate,hostname,port,GetTimeInfo(true),oldserverlog,userLog);
#else
		Log(SERVERLOG, "\r\n\r\n======== Began EV server pid %d %s compiled %s on host %s port %d at %s serverlog:%d userlog: %d\r\n",getpid(),version,compileDate,hostname,port,GetTimeInfo(true),oldserverlog,userLog);
		printf((char*)"\r\n\r\n======== Began EV server pid %d  %s compiled %s on host %s port %d at %s serverlog:%d userlog: %d\r\n",getpid(),version,compileDate,hostname,port,GetTimeInfo(true),oldserverlog,userLog);
#endif
	}
	serverLog = oldserverlog;

	echo = false;

	InitStandalone();

	UnlockLevel(); // unlock it to add stuff

#ifndef DISCARDPOSTGRES
#ifndef EVSERVER
	if (postgresparams)  PGUserFilesCode(); // unforked process can hook directly. Forked must hook AFTER forking
#endif
#endif
#ifndef DISCARDMONGO
	if (mongodbparams)  MongoSystemInit(mongodbparams);
#endif
	
#ifdef TREETAGGER
	InitTreeTagger(treetaggerParams);
#endif

#ifdef PRIVATE_CODE
	PrivateInit(privateParams); 
#endif
	EncryptInit(encryptParams);
	DecryptInit(decryptParams);
	LockLayer(1,false); 
	return 0;
}

void PartiallyCloseSystem() // server data (queues etc) remain available
{
	WORDP shutdown = FindWord((char*)"^csshutdown");
	if (shutdown)  Callback(shutdown,(char*)"()",false); 
#ifndef DISCARDJAVASCRIPT
	DeleteTransientJavaScript(); // unload context if there
	DeletePermanentJavaScript(); // javascript permanent system
#endif
	FreeAllUserCaches(); // user system
    CloseDictionary();	// dictionary system
    CloseFacts();		// fact system
	CloseBuffers();		// memory system
#ifndef DISCARDPOSTGRES
	PostgresShutDown();
#endif
#ifndef DISCARDMONGO
	MongoSystemRestart();
#endif
#ifdef PRIVATE_CODE
	PrivateRestart(); // must come AFTER any mongo/postgress init (to allow encrypt/decrypt override)
#endif
}

void CloseSystem()
{
	PartiallyCloseSystem();
	// server remains up on a restart
#ifndef DISCARDSERVER
	CloseServer();
#endif
	// user file rerouting stays up on a restart
#ifndef DISCARDPOSTGRES
	PGUserFilesCloseCode();
#endif
#ifndef DISCARDMONGO
	MongoSystemShutdown();
#endif
#ifdef PRIVATE_CODE
	PrivateShutdown();  // must come last after any mongo/postgress 
#endif
#ifndef DISCARDJSON
	CurlShutdown();
#endif
}

////////////////////////////////////////////////////////
/// INPUT PROCESSING
////////////////////////////////////////////////////////
#ifdef LINUX
	#define LINUXORMAC 1
#endif
#ifdef __MACH__
	#define LINUXORMAC 1
#endif

#ifdef LINUXORMAC
#include <fcntl.h>
int kbhit(void)
{
  struct termios oldt, newt;
  int ch;
  int oldf;
 
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
 
  ch = getchar();
 
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);
 
  if(ch != EOF)
  {
    ungetc(ch, stdin);
    return 1;
  }
 
  return 0;
}
#endif

int FindOOBEnd(int start)
{
	if (*wordStarts[start] == OOB_START)
	{
		while (++start < wordCount && *wordStarts[start] != OOB_END); // find a close
		return (*wordStarts[start] == OOB_END) ? (start + 1) : 1;
	}
	else return 1; // none
}

void ProcessOOB(char* output)
{
	char* ptr = SkipWhitespace(output);
	if (*ptr == OOB_START) // out-of-band data?
	{
		char* at = ptr;
		bool quote = false;
		int bracket = 1;
		while (*++at) // do full balancing
		{
			if (*at == '"' && *(at-1) != '\\') quote = !quote;
			else if (!quote)
			{
				if (*at == '[') ++bracket;
				else if (*at == ']') 
				{
					--bracket;
					if (!bracket) break; // true end
				}
			}
		}

		char* end = (*at == ']') ? at : NULL;
		if (end)
		{
			clock_t milli = ElapsedMilliseconds();
			char* at = strstr(ptr,(char*)"loopback="); // loopback is reset after every user output
			if (at) 
			{
				at = SkipWhitespace(at+9);
				loopBackDelay = atoi(at);
				loopBackTime = milli + loopBackDelay;
			}
			at = strstr(ptr,(char*)"callback="); // call back is canceled if user gets there first
			if (at) 
			{
				at = SkipWhitespace(at+9);
				callBackDelay = atoi(at);
				callBackTime = milli + callBackDelay;
			}
			at = strstr(ptr,(char*)"alarm="); // alarm stays pending until it launches
			if (at) 
			{
				at = SkipWhitespace(at+6);
				alarmDelay = atoi(at);
				alarmTime = milli + alarmDelay;
			}
			if (!oob) memmove(output,end+1,strlen(end)); // delete oob data so not printed to user
		}
	}
}

bool ProcessInputDelays(char* buffer,bool hitkey)
{
	// override input for a callback
	if (callBackDelay || loopBackDelay || alarmDelay) // want control back to chatbot when user has nothing
	{
		clock_t milli =  ElapsedMilliseconds();
		if (!hitkey && (sourceFile == stdin || !sourceFile))
		{
			if (loopBackDelay && milli > (clock_t)loopBackTime) 
			{
				strcpy(buffer,(char*)"[ loopback ]");
				printf((char*)"%s",(char*)"\r\n");
			}
			else if (callBackDelay && milli > (clock_t)callBackTime) 
			{
				strcpy(buffer,(char*)"[ callback ]");
				printf((char*)"%s",(char*)"\r\n");
				callBackDelay = 0; // used up
			}
			else if (alarmDelay && milli > (clock_t)alarmTime)
			{
				alarmDelay = 0;
				strcpy(buffer,(char*)"[ alarm ]");
				printf((char*)"%s",(char*)"\r\n");
			}
			else return true; // nonblocking check for input
		}
		if (alarmDelay && milli > (clock_t)alarmTime) // even if he hits a key, alarm has priority
		{
			alarmDelay = 0;
			strcpy(buffer,(char*)"[ alarm ]");
			printf((char*)"%s",(char*)"\r\n");
		}
	}
	return false;
}

void ProcessInputFile()
{
	int turn = 0;
	while (ALWAYS)
    {
		if (*oktest) // self test using OK or WHY as input
		{
			printf((char*)"%s\r\n    ",UTF2ExtendedAscii(ourMainOutputBuffer));
			strcpy(ourMainInputBuffer,oktest);
		}
		else if (quitting) return; 
		else if (systemReset) 
		{
			printf((char*)"%s\r\n",UTF2ExtendedAscii(ourMainOutputBuffer));
			*computerID = 0;	// default bot
			*ourMainInputBuffer = 0;		// restart conversation
		}
		else 
		{
			if ((!documentMode || *ourMainOutputBuffer)  && !silent) // if not in doc mode OR we had some output to say - silent when no response
			{
				// output bot response
				if (*botPrefix) printf((char*)"%s ",botPrefix);
			}
			if (showTopic)
			{
				GetActiveTopicName(tmpWord); // will show currently the most interesting topic
				printf((char*)"(%s) ",tmpWord);
			}
			callBackDelay = 0; // now turned off after an output
			if (!silent) 
			{
				ProcessOOB(ourMainOutputBuffer);
				printf((char*)"%s",UTF2ExtendedAscii(ourMainOutputBuffer));
			}
			if ((!documentMode || *ourMainOutputBuffer) && !silent) printf((char*)"%s",(char*)"\r\n");

			if (showWhy) printf((char*)"%s",(char*)"\r\n"); // line to separate each chunk

			//output user prompt
			if (documentMode || silent) {;} // no prompt in document mode
			else if (*userPrefix) printf((char*)"%s ",userPrefix);
			else printf((char*)"%s",(char*)"   >");
			
			*ourMainInputBuffer = ' '; // leave space at start to confirm NOT a null init message, even if user does only a cr
			ourMainInputBuffer[1] = 0;
			if (loopBackDelay) loopBackTime = ElapsedMilliseconds() + loopBackDelay; // resets every output
inputRetry:
			if (ProcessInputDelays(ourMainInputBuffer+1,KeyReady())) goto inputRetry; // use our fake callback input? loop waiting if no user input found
	
			if (ourMainInputBuffer[1]){;} // callback in progress, data put into buffer, read his input later, but will be out of date
			else if (documentMode)
			{
				if (!ReadDocument(ourMainInputBuffer+1,sourceFile)) break;
			}
			else if (ReadALine(ourMainInputBuffer+1,sourceFile,MAX_BUFFER_SIZE-100) < 0) break; // end of input

			// reading from file
			if (sourceFile != stdin)
			{
				char word[MAX_WORD_SIZE];
				ReadCompiledWord(ourMainInputBuffer,word);
				if (!stricmp(word,(char*)":quit") || !stricmp(word,(char*)":exit")) break;
				if (!stricmp(word,(char*)":debug")) 
				{
					DebugCode(ourMainInputBuffer);
					continue;
				}
				if ((!*word && !documentMode) || *word == '#') continue;
				if (echoSource == SOURCE_ECHO_USER) printf((char*)"< %s\r\n",ourMainInputBuffer);
			}
		}
		if (!server && extraTopicData) turn = PerformChatGivenTopic(loginID,computerID,ourMainInputBuffer,NULL,ourMainOutputBuffer,extraTopicData); 
		else turn = PerformChat(loginID,computerID,ourMainInputBuffer,NULL,ourMainOutputBuffer); // no ip
		if (turn == PENDING_RESTART) Restart();
	}
	if (sourceFile != stdin) 
	{
		FClose(sourceFile);  // to get here, must have been a source file that ended
		sourceFile = stdin;
	}
	Log(STDTRACELOG, "Sourcefile Time used %ld ms for %d sentences %d tokens.\r\n",ElapsedMilliseconds() - sourceStart,sourceLines,sourceTokens);
}

void MainLoop() //   local machine loop
{	
	char user[MAX_WORD_SIZE];
	sourceFile = stdin; // keep up the conversation indefinitely
	if (*sourceInput)	sourceFile = FopenReadNormal(sourceInput); 
	*ourMainInputBuffer = 0;
	if (!*loginID)
	{
		printf((char*)"%s",(char*)"\r\nEnter user name: ");
		ReadALine(user,stdin);
		printf((char*)"%s",(char*)"\r\n");
		if (*user == '*') // let human go first  -   say "*bruce
		{
			memmove(user,user+1,strlen(user));
			printf((char*)"%s",(char*)"\r\nEnter starting input: ");
			ReadALine(ourMainInputBuffer,stdin);
			printf((char*)"%s",(char*)"\r\n");
		}
	}
	else strcpy(user,loginID);
	PerformChat(user,computerID,ourMainInputBuffer,NULL,ourMainOutputBuffer); // unknown bot, no input,no ip
	
retry:
	ProcessInputFile();
	sourceFile = stdin;
	*ourMainInputBuffer = 0;
	ourMainInputBuffer[1] = 0;
	if (!quitting) goto retry;
}

void ResetToPreUser() // prepare for multiple sentences being processed - data lingers over multiple sentences
{
	// limitation on how many sentences we can internally resupply
	inputCounter = 0;
	totalCounter = 0;
	itAssigned = theyAssigned = 0;
	memset(wordStarts,0,sizeof(char*)*MAX_SENTENCE_LENGTH); // reinit for new volley - sharing of word space can occur throughout this volley
	ClearWhereInSentence();
	ResetTokenSystem();

	//  Revert to pre user-loaded state, fresh for a new user
	ReturnToAfterLayer(1,false);  // dict/fact/strings reverted and any extra topic loaded info  (but CSBoot process NOT lost)
	ReestablishBotVariables(); // any changes user made to a variable will be reset
	ResetTopicSystem(false);
	ResetUserChat();
	ResetFunctionSystem();
	ResetTopicReply();

 	//   ordinary locals
	inputSentenceCount = 0;
}

void ResetSentence() // read for next sentence to process from raw system level control only
{
	ClearWhereInSentence(); 
	ResetFunctionSystem();
	respondLevel = 0; 
	currentRuleID = NO_REJOINDER;	//   current rule id
 	currentRule = 0;				//   current rule being procesed
	currentRuleTopic = -1;
	ruleErased = false;	
}

void ComputeWhy(char* buffer,int n)
{
	strcpy(buffer,(char*)"Why:");
	buffer += strlen(buffer);
	int start = 0;
	int end = responseIndex;
	if (n >= 0) 
	{
		start = n;
		end = n + 1;
	}
	for (int i = start; i < end; ++i) 
	{
		unsigned int order = responseOrder[i];
		int topic = responseData[order].topic;
		if (!topic) continue;
		char label[MAX_WORD_SIZE];
		int id;
		char* more = GetRuleIDFromText(responseData[order].id,id); // has no label in text
		char* rule = GetRule(topic,id);
		GetLabel(rule,label);
		char c = *more;
		*more = 0;
		sprintf(buffer,(char*)"%s%s",GetTopicName(topic),responseData[order].id); // topic and rule 
		buffer += strlen(buffer);
		if (*label) 
		{
			sprintf(buffer,(char*)"=%s",label); // label if any
			buffer += strlen(buffer);
		}
		*more = c;
		
		// was there a relay
		if (*more)
		{
			int topicid = atoi(more+1); // topic number
			more = strchr(more+1,'.'); // r top level + rejoinder
			char* dotinfo = more;
			more = GetRuleIDFromText(more,id);
			char* rule = GetRule(topicid,id);
			GetLabel(rule,label);
			sprintf(buffer,(char*)".%s%s",GetTopicName(topicid),dotinfo); // topic and rule 
			buffer += strlen(buffer);
			if (*label)
			{
				sprintf(buffer,(char*)"=%s",label); // label if any
				buffer += strlen(buffer);
			}
		}
		strcpy(buffer,(char*)" ");
		buffer += strlen(buffer);
	}
}

void PrepareResult() // takes the initial given result
{
	unsigned int size = 0;
	char* copy = AllocateBuffer();
	uint64 control = tokenControl;
	tokenControl |= LEAVE_QUOTE;
	for (int i = 0; i < responseIndex; ++i) 
    {
		unsigned int order = responseOrder[i];
        if (!*responseData[order].response) continue;
		size_t len = strlen((const char*) responseData[order].response);
		if ((len + size) >= OUTPUT_BUFFER_SIZE) continue;
		char* ptr = responseData[order].response;
		
		// each sentence becomes a transient fact
		while (ptr && *ptr) // find sentences of response
		{
			char* old = ptr;
			int count;
			char* starts[MAX_SENTENCE_LENGTH];
			memset(starts,0,sizeof(char*)*MAX_SENTENCE_LENGTH);
			ptr = Tokenize(ptr,count,(char**) starts,false,true);   //   only used to locate end of sentence but can also affect tokenFlags (no longer care)
			char c = *ptr; // is there another sentence after this?
			char c1 = 0;
			if (c)  
			{
				c1 = *(ptr-1);
				*(ptr-1) = 0; // kill the separator 
			}

			//   save sentences as facts
			char* out = copy;
			char* at = old-1;
			while (*++at) // copy message and alter some stuff like space or cr lf
			{
				if (*at == '\r' || *at == '\n') {;}
				else *out++ = *at;  
			}
			*out = 0;
			if ((out-copy) > 2) // we did copy something, make a fact of it
			{
				char name[MAX_WORD_SIZE];
				sprintf(name,(char*)"%s.%s",GetTopicName(responseData[order].topic),responseData[order].id);
				CreateFact(MakeMeaning(StoreWord(copy,AS_IS)),Mchatoutput,MakeMeaning(StoreWord(name)),FACTTRANSIENT);
			}
			if (c) *(ptr-1) = c1;
		}	
	}
	tokenControl = control;
}

char* ConcatResult()
{
    static char  result[OUTPUT_BUFFER_SIZE];
  	unsigned int oldtrace = trace;
	trace = 0;
	result[0] = 0;
	if (timerLimit && timerCheckInstance == TIMEOUT_INSTANCE)
	{
		responseIndex = 0;
		currentRule = 0;
		AddResponse((char*)"Time limit exceeded.", 0);
	}
	for (int i = 0; i < responseIndex; ++i) 
    {
		unsigned int order = responseOrder[i];
        if (responseData[order].response[0]) 
		{
			char* reply = responseData[order].response;
			size_t len = strlen(reply);
			if (len >= OUTPUT_BUFFER_SIZE)
			{
				ReportBug((char*)"overly long reply %s",reply)
				reply[OUTPUT_BUFFER_SIZE-50] = 0;
			}
			AddBotUsed(reply,len);
		}
    }

	//   now join up all the responses as one output into result
	unsigned int size = 0;
	uint64 control = tokenControl;
	tokenControl |= LEAVE_QUOTE;
	for (int i = 0; i < responseIndex; ++i) 
    {
		unsigned int order = responseOrder[i];
        if (!*responseData[order].response) continue;
		size_t len = strlen((const char*) responseData[order].response);
		if ((len + size) >= OUTPUT_BUFFER_SIZE) break;
		
		char piece[OUTPUT_BUFFER_SIZE];
		strcpy(piece,responseData[order].response);
		if (*result) 
		{
			result[size++] = ENDUNIT; // add separating item from last unit for log detection
			result[size] = 0;
		}
		strcpy(result+size,piece);
		size += len;
	}
	trace = (modifiedTrace) ? modifiedTraceVal : oldtrace;
	tokenControl = control;
    return result;
}

void FinishVolley(char* incoming,char* output,char* postvalue)
{
	// massage output going to user
	if (!documentMode)
	{
		PrepareResult();
		postProcessing = 1;
		++outputNest; // this is not generating new output
		OnceCode((char*)"$cs_control_post",postvalue);
		--outputNest;
		postProcessing = 0;

		char* at = output;
		if (autonumber)
		{
			sprintf(at,(char*)"%d: ",volleyCount);
			at += strlen(at);
		}
		strcpy(at,ConcatResult());
		
		time_t curr = time(0);
		if (regression) curr = 44444444; 
		char* when = GetMyTime(curr); // now
		if (*incoming) strcpy(timePrior,GetMyTime(curr)); // when we did the last volley
		if (!stopUserWrite) WriteUserData(curr); 
		else stopUserWrite = false;
		// Log the results
		GetActiveTopicName(activeTopic); // will show currently the most interesting topic
		if (userLog && prepareMode != POS_MODE && prepareMode != PREPARE_MODE)
		{
			char buff[20000];
			char time15[MAX_WORD_SIZE];
			unsigned int lapsedMilliseconds = ElapsedMilliseconds() - volleyStartTime;
			*time15 = 0;
			sprintf(time15,(char*)" F:%d ",lapsedMilliseconds);
			*buff = 0;
			if (responseIndex && regression != NORMAL_REGRESSION) ComputeWhy(buff,-1);
			char* nl = (LogEndedCleanly()) ? (char*) "" : (char*) "\r\n";

			if (*incoming && regression == NORMAL_REGRESSION) Log(STDTRACELOG,(char*)"%s(%s) %s ==> %s %s\r\n",nl,activeTopic,TrimSpaces(incoming),Purify(output),buff); // simpler format for diff
			else if (!*incoming) 
			{
				Log(STDUSERLOG,(char*)"%sStart: user:%s bot:%s ip:%s rand:%d (%s) %d ==> %s  When:%s Version:%s Build0:%s Build1:%s 0:%s F:%s P:%s %s\r\n",nl,loginID,computerID,callerIP,randIndex,activeTopic,volleyCount,Purify(output),when,version,timeStamp[0],timeStamp[1],timeturn0,timeturn15,timePrior,buff); // conversation start
			}
			else 
			{
				Log(STDUSERLOG,(char*)"%sRespond: user:%s bot:%s ip:%s (%s) %d  %s ==> %s  When:%s %s %s\r\n",nl,loginID,computerID,callerIP,activeTopic,volleyCount,incoming,Purify(output),when,buff,time15);  // normal volley
			}
			if (shortPos) 
			{
				Log(STDTRACELOG,(char*)"%s",DumpAnalysis(1,wordCount,posValues,(char*)"Tagged POS",false,true));
				Log(STDTRACELOG,(char*)"\r\n");
			}
		}

		// now convert output separators between rule outputs to space from ' for user display result (log has ', user sees nothing extra) 
		if (prepareMode != REGRESS_MODE)
		{ 
			char* sep = output;
			while ((sep = strchr(sep,ENDUNIT))) 
			{
				if (*(sep-1) == ' ') memmove(sep,sep+1,strlen(sep));
				else *sep = ' ';
			}
		}
	}
	else *output = 0;
	ClearVolleyWordMaps(); 
	if (!documentMode) 
	{
		ShowStats(false);
		ResetToPreUser(); // back to empty state before any user
	}
}

int PerformChatGivenTopic(char* user, char* usee, char* incoming,char* ip,char* output,char* topicData)
{
	CreateFakeTopics(topicData);
	int answer = PerformChat(user,usee,incoming,ip,output);
	ReleaseFakeTopics();
	return answer;
}

int PerformChat(char* user, char* usee, char* incoming,char* ip,char* output) // returns volleycount or 0 if command done or -1 PENDING_RESTART
{ //   primary entrypoint for chatbot -- null incoming treated as conversation start.
	pendingUserReset = false;
	volleyStartTime = ElapsedMilliseconds(); // time limit control
	timerCheckInstance = 0;
	modifiedTraceVal = 0;
	modifiedTrace = false;

	if (!documentMode) tokenCount = 0;
#ifndef DISCARDJSON
	InitJSONNames(); // reset indices for this volley
#endif
	ClearVolleyWordMaps();
	mainInputBuffer = incoming;
	mainOutputBuffer = output;
	size_t len = strlen(incoming);
	if (len >= MAX_BUFFER_SIZE) incoming[MAX_BUFFER_SIZE-1] = 0; // chop to legal safe limit
	// now validate that token size MAX_WORD_SIZE is not invalidated
	char* at = mainInputBuffer;
	bool quote = false;
	char* start = mainInputBuffer;
	while (*++at)
	{
		if (*at == '"' && *(at-1) != '\\') quote = !quote;
		if (*at == ' ' && !quote) // proper token separator
		{
			if ((at-start) > (MAX_WORD_SIZE-1)) break; // trouble
			start = at + 1;
		}
	}
	if ((at-start) > (MAX_WORD_SIZE-1)) start[MAX_WORD_SIZE-5] = 0; // trouble - cut him off at the knees

	char* first = SkipWhitespace(incoming);
#ifndef DISCARDTESTING
	if (!server && !(*first == ':' && IsAlphaUTF8(first[1]))) strcpy(revertBuffer,first); // for a possible revert
#endif
    output[0] = 0;
	output[1] = 0;
	*currentFilename = 0;
	bufferIndex = baseBufferIndex; // return to default basic buffers used so far, in case of crash and recovery
	inputNest = 0; // all normal user input to start with

    //   case insensitive logins
    static char caller[MAX_WORD_SIZE];
	static char callee[MAX_WORD_SIZE];
	callee[0] = 0;
    caller[0] = 0;
	MakeLowerCopy(callee, usee);
    if (user) 
	{
		strcpy(caller,user);
		//   allowed to name callee as part of caller name
		char* separator = strchr(caller,':');
		if (separator) *separator = 0;
		if (separator && !*usee) MakeLowerCopy(callee,separator+1); // override the bot
		strcpy(loginName,caller); // original name as he typed it

		MakeLowerCopy(caller,caller);
	}
	bool hadIncoming = *incoming != 0 || documentMode;
	while (incoming && *incoming == ' ') ++incoming;	// skip opening blanks

	if (incoming[0] && incoming[1] == '#' && incoming[2] == '!') // naming bot to talk to- and whether to initiate or not - e.g. #!Rosette#my message
	{
		char* next = strchr(incoming+3,'#');
		if (next)
		{
			*next = 0;
			MakeLowerCopy(callee,incoming+3); // override the bot name (including future defaults if such)
			strcpy(incoming+1,next+1);	// the actual message.
			if (!*incoming) incoming = 0;	// login message
		}
	}

    if (trace & TRACE_MATCH) Log(STDTRACELOG,(char*)"Incoming data- %s | %s | %s\r\n",caller, (*callee) ? callee : (char*)" ", (incoming) ? incoming : (char*)"");
 
	bool fakeContinue = false;
	if (callee[0] == '&') // allow to hook onto existing conversation w/o new start
	{
		*callee = 0;
		fakeContinue = true;
	}
    Login(caller,callee,ip); //   get the participants names
	if (!*computerID && *incoming != ':') ReportBug("No computer id before user load?")

	if (systemReset) // drop old user
	{
		if (systemReset == 2) 
		{
			KillShare();
			ReadNewUser(); 
		}
		else
		{
			ReadUserData();		//   now bring in user state
		}
		systemReset = 0;
	}
	else if (!documentMode) 
	{
		// preserve file status reference across this read use of ReadALine
		int BOMvalue = -1; // get prior value
		char oldc;
		int oldCurrentLine;	
		BOMAccess(BOMvalue, oldc,oldCurrentLine); // copy out prior file access and reinit user file access

		ReadUserData();		//   now bring in user state
		BOMAccess(BOMvalue, oldc,oldCurrentLine); // restore old BOM values
	}
	// else documentMode
	if (fakeContinue) return volleyCount;
	if (!*computerID && *incoming != ':')  // a  command will be allowed to execute independent of bot- ":build" works to create a bot
	{
		strcpy(output,(char*)"No such bot.\r\n");
		char* fact = "no default fact";
		WORDP D = FindWord((char*)"defaultbot");
		if (!D) fact = "defaultbot word not found";
		else
		{
			FACT* F = GetObjectHead(D);
			if (!F)  fact = "defaultbot fact not found";
			else if (F->flags & FACTDEAD) fact = "dead default fact";
			else fact = Meaning2Word(F->subject)->word;
		}

		ReportBug((char*) "No such bot  %s - %s - %s status: %s", user, usee, incoming,fact);
		ReadComputerID(); // presume default bot log file
		CopyUserTopicFile("nosuchbot");
		if (nosuchbotrestart) pendingRestart = true;
		return 0;	// no such bot
	}

	if (!ip) ip = "";
	
	// for retry INPUT
	inputRetryRejoinderTopic = inputRejoinderTopic;
	inputRetryRejoinderRuleID = inputRejoinderRuleID;
	lastInputSubstitution[0] = 0;

	int ok = 1;
    if (!*incoming && !hadIncoming)  //   begin a conversation - bot goes first
	{
		*readBuffer = 0;
		nextInput = output;
		userFirstLine = volleyCount+1;
		*currentInput = 0;
		responseIndex = 0;
	}
	else if (*incoming == '\r' || *incoming == '\n' || !*incoming) // incoming is blank, make it so
	{
		*incoming = ' ';
		incoming[1] = 0;
	}

	// change out special or illegal characters
	static char copy[INPUT_BUFFER_SIZE];
	char* p = incoming;
	while ((p = strchr(p,ENDUNIT))) *p = '\''; // remove special character used by system in various ways. Dont allow it.
	p = incoming;
	while ((p = strchr(p,'\n'))) *p = ' '; // remove special character used by system in various ways. Dont allow it.
	p = incoming;
	while ((p = strchr(p,'\r'))) *p = ' '; // remove special character used by system in various ways. Dont allow it.
	p = incoming;
	while ((p = strchr(p,(char)-30))) // handle curved quote
	{
		if (p[1] == (char)-128 && p[2] == (char)-103)
		{
			*p = '\'';
			memmove(p+1,p+3,strlen(p+2));
		}
		++p;
	}
	strcpy(copy,incoming); // so input trace not contaminated by input revisions -- mainInputBuffer is "incoming"

	ok = ProcessInput(copy);

	if (ok <= 0) return ok; // command processed
	
	if (!server) // refresh prompts from a loaded bot since mainloop happens before user is loaded
	{
		WORDP dBotPrefix = FindWord((char*)"$botprompt");
		strcpy(botPrefix,(dBotPrefix && dBotPrefix->w.userValue) ? dBotPrefix->w.userValue : (char*)"");
		WORDP dUserPrefix = FindWord((char*)"$userprompt");
		strcpy(userPrefix,(dUserPrefix && dUserPrefix->w.userValue) ? dUserPrefix->w.userValue : (char*)"");
	}

	// compute response and hide additional information after it about why
	FinishVolley(mainInputBuffer,output,NULL); // use original input main buffer, so :user and :bot can cancel for a restart of concerasation
	char* after = output + strlen(output) + 1;
	*after++ = (char)0xfe; // positive termination
	*after++ = (char)0xff; // positive termination for servers
	ComputeWhy(after,-1);
	after += strlen(after) + 1;
	strcpy(after,activeTopic); // currently the most interesting topic
#ifndef DISCARDJAVASCRIPT
	DeleteTransientJavaScript(); // unload context if there
#endif
	return volleyCount;
}

FunctionResult Reply() 
{
	callback =  (wordCount > 1 && *wordStarts[1] == OOB_START && (!stricmp(wordStarts[2],(char*)"callback") || !stricmp(wordStarts[2],(char*)"alarm") || !stricmp(wordStarts[2],(char*)"loopback"))); // dont write temp save
	stringInverseFree = stringInverseStart;
	withinLoop = 0;
	choiceCount = 0;
	callIndex = 0;
	ResetOutput();
	ResetTopicReply();
	ResetReuseSafety();
	if (trace & TRACE_OUTPUT) 
	{
		Log(STDTRACELOG,(char*)"\r\n\r\nReply input: ");
		for (int i = 1; i <= wordCount; ++i) Log(STDTRACELOG,(char*)"%s ",wordStarts[i]);
		Log(STDTRACELOG,(char*)"\r\n  Pending topics: %s\r\n",ShowPendingTopics());
	}
	FunctionResult result = NOPROBLEM_BIT;
	int pushed = PushTopic(FindTopicIDByName(GetUserVariable((char*)"$cs_control_main")));
	if (pushed < 0) return FAILRULE_BIT;
	AllocateOutputBuffer();
	result = PerformTopic(0,currentOutputBase); //   allow control to vary
	FreeOutputBuffer();
	if (pushed) PopTopic();
	if (globalDepth) ReportBug((char*)"Main code global depth not 0");
	return result;
}

void Restart()
{
	char us[MAX_WORD_SIZE];
	trace = 0;
	ClearUserVariables();
	PartiallyCloseSystem();
	CreateSystem();
	InitStandalone();
	ProcessArguments(argc,argv);
	strcpy(us,loginID);

#ifndef DISCARDPOSTGRES
	if (postgresparams)  PGUserFilesCode();
#endif
#ifndef DISCARDMONGO
	if (mongodbparams)  MongoSystemInit(mongodbparams);
#endif
#ifdef PRIVATE_CODE
	PrivateInit(privateParams); 
#endif
	EncryptInit(encryptParams);
	DecryptInit(decryptParams);

	if (!server)
	{
		echo = false;
		char initialInput[MAX_WORD_SIZE];
		*initialInput = 0;
		int turn = PerformChat(us,computerID,initialInput,callerIP,mainOutputBuffer);
		// ignore any PENDING_RESTART == turn response here.
	}
	else 
	{
		Log(STDTRACELOG,(char*)"System restarted %s\r\n",GetTimeInfo(true)); // shows user requesting restart.
	}
	pendingRestart = false;
}

int ProcessInput(char* input)
{
	lastRestoredIndex = 0;
	sentencePreparationIndex = 0;	// set id for save/restore sentence optimization
	startTimeInfo =  ElapsedMilliseconds();
	// aim to be able to reset some global data of user
	unsigned int oldInputSentenceCount = inputSentenceCount;
	//   precautionary adjustments
	strcpy(inputCopy,input);
	char* buffer = inputCopy;
	size_t len = strlen(input);
	if (len >= MAX_BUFFER_SIZE) buffer[MAX_BUFFER_SIZE-1] = 0; 

#ifndef DISCARDTESTING
	char* at = SkipWhitespace(buffer);
	if (*at == '[') // oob is leading
	{
		int n = 1;
		while (n && *++at)
		{
			if (*at == '\\') ++at; // ignore \[ stuff
			else if (*at == '[') ++n;
			else if (*at == ']') --n;
		}
		at = SkipWhitespace(++at);
	}
	
	if (*at == ':' && IsAlphaUTF8(at[1]) && IsAlphaUTF8(at[2]) && !documentMode && !readingDocument) // avoid reacting to :P and other texting idioms
	{
		bool reset = false;
		if (!strnicmp(at,":reset",6)) 
		{
			reset = true;
			char* intercept = GetUserVariable("$cs_beforereset");
			if (intercept) Callback(FindWord(intercept),"()",false); // call script function first
		}
		TestMode commanded = DoCommand(at,mainOutputBuffer);
		// reset rejoinders to ignore this interruption
		outputRejoinderRuleID = inputRejoinderRuleID; 
 		outputRejoinderTopic = inputRejoinderTopic;
		if (!strnicmp(at,(char*)":retry",6) || !strnicmp(at,(char*)":redo",5))
		{
			strcpy(input,mainInputBuffer);
			strcpy(inputCopy,mainInputBuffer);
			buffer = inputCopy;
		}
		else if (commanded == RESTART)
		{
			pendingRestart = true;
			return PENDING_RESTART;	// nothing more can be done here.
		}
		else if (commanded == BEGINANEW)  
		{ 
			int BOMvalue = -1; // get prior value
			char oldc;
			int oldCurrentLine;	
			BOMAccess(BOMvalue, oldc,oldCurrentLine); // copy out prior file access and reinit user file access
			ResetToPreUser();	// back to empty state before user
			FreeAllUserCaches();
			ReadNewUser();		//   now bring in user state again (in case we changed user)
			BOMAccess(BOMvalue, oldc,oldCurrentLine); // restore old BOM values
			*buffer = *mainInputBuffer = 0; // kill off any input
			userFirstLine = volleyCount+1;
			*readBuffer = 0;
			nextInput = buffer;
			if (reset) 
			{
				char* intercept = GetUserVariable("$cs_afterreset");
				if (intercept) Callback(FindWord(intercept),"()",false); // call script function after
			}
			ProcessInput(buffer);
			return 2; 
		}
		else if (commanded == COMMANDED ) 
		{
			ResetToPreUser(); // back to empty state before any user
			return false; 
		}
		else if (commanded == OUTPUTASGIVEN) return true; 
		else if (commanded == TRACECMD) 
		{
			WriteUserData(time(0)); // writes out data in case of tracing variables
			ResetToPreUser(); // back to empty state before any user
			return false; 
		}
		// otherwise FAILCOMMAND
	}
#endif
	
	if (!documentMode) 
	{
		responseIndex = 0;	// clear out data (having left time for :why to work)
		OnceCode((char*)"$cs_control_pre");
		if (responseIndex != 0) ReportBug((char*)"Not expecting PRE to generate a response")
		randIndex =  oldRandIndex;
	}
	if (*buffer) ++volleyCount;
	int oldVolleyCount = volleyCount;
	bool startConversation = !*buffer;
loopback:
	inputNest = 0; // all normal user input to start with
	lastInputSubstitution[0] = 0;
	if (trace &  TRACE_OUTPUT) Log(STDTRACELOG,(char*)"\r\n\r\nInput: %d to %s: %s \r\n",volleyCount,computerID,input);
	strcpy(currentInput,input);	//   this is what we respond to, literally.

	if (!strncmp(buffer,(char*)". ",2)) buffer += 2;	//   maybe separator after ? or !

	//   process input now
	char prepassTopic[MAX_WORD_SIZE];
	strcpy(prepassTopic,GetUserVariable((char*)"$cs_prepass"));
	nextInput = buffer;
	if (!documentMode)  AddHumanUsed(buffer);
 	int loopcount = 0;
	while (((nextInput && *nextInput) || startConversation) && loopcount < SENTENCE_LIMIT) // loop on user input sentences
	{
		nextInput = SkipWhitespace(nextInput);
		if (nextInput[0] == INPUTMARKER) // submitted by ^input `` is start,  ` is end
		{
			if (nextInput[1] == INPUTMARKER) ++inputNest;
			else --inputNest;
			nextInput += 2;
			continue;
		}
		topicIndex = currentTopicID = 0; // precaution
		FunctionResult result = DoSentence(prepassTopic,loopcount == (SENTENCE_LIMIT - 1)); // sets nextInput to next piece
		if (result == RESTART_BIT)
		{
			pendingRestart = true;
			if (pendingUserReset) // erase user file?
			{
				ResetToPreUser(); // back to empty state before any user
				ReadNewUser();
				WriteUserData(time(0)); 
				ResetToPreUser(); // back to empty state before any user
			}
			return PENDING_RESTART;	// nothing more can be done here.
		}
		else if (result == FAILSENTENCE_BIT) // usually done by substituting a new input
		{
			inputRejoinderTopic  = inputRetryRejoinderTopic; 
			inputRejoinderRuleID = inputRetryRejoinderRuleID; 
		}
		else if (result == RETRYINPUT_BIT)
		{
			// for retry INPUT
			inputRejoinderTopic  = inputRetryRejoinderTopic; 
			inputRejoinderRuleID = inputRetryRejoinderRuleID; 
			// CANNOT DO ResetTopicSystem(); // this destroys our saved state!!!
			RecoverUser(); // must be after resettopic system
			// need to recover current topic and pending list
			inputSentenceCount = oldInputSentenceCount;
			volleyCount = oldVolleyCount;
			strcpy(inputCopy,input);
			buffer = inputCopy;
			goto loopback;
		}
		char* at = SkipWhitespace(buffer);
		if (*at != '[') ++inputSentenceCount; // dont increment if OOB message
		if (sourceFile && wordCount)
		{
			sourceTokens += wordCount;
			++sourceLines;
		}
		startConversation = false;
		++loopcount;
	}
	return true;
}

bool PrepassSentence(char* prepassTopic)
{
	if (prepassTopic && *prepassTopic)
	{
		int topic = FindTopicIDByName(prepassTopic);
		if (topic && !(GetTopicFlags(topic) & TOPIC_BLOCKED))  
		{
			int pushed =  PushTopic(topic); 
			if (pushed < 0) return false;
			ChangeDepth(1,(char*)"PrepassSentence");
			AllocateOutputBuffer();
			ResetReuseSafety();
			uint64 oldflags = tokenFlags;
			FunctionResult result = PerformTopic(0,currentOutputBase); 
			FreeOutputBuffer();
			ChangeDepth(-1,(char*)"PrepassSentence");
			if (pushed) PopTopic();
			//   subtopic ending is not a failure.
			if (result & (RESTART_BIT|ENDSENTENCE_BIT | FAILSENTENCE_BIT| ENDINPUT_BIT | FAILINPUT_BIT )) 
			{
				if (result & ENDINPUT_BIT) nextInput = "";
				--inputSentenceCount; // abort this input
				return true; 
			}
			if (prepareMode == PREPARE_MODE || trace & (TRACE_PREPARE|TRACE_POS) || prepareMode == POS_MODE || (prepareMode == PENN_MODE && trace & TRACE_POS)) 
			{
				if (tokenFlags != oldflags) DumpTokenFlags((char*)"After prepass"); // show revised from prepass
			}
		}
	}
	return false;
}

FunctionResult DoSentence(char* prepassTopic, bool atlimit)
{
	char input[INPUT_BUFFER_SIZE];  // complete input we received
	int len = strlen(nextInput);
	if (len >= (INPUT_BUFFER_SIZE)-100) nextInput[INPUT_BUFFER_SIZE-100] = 0;
	strcpy(input,nextInput);
	ambiguousWords = 0;

	if (all) Log(STDTRACELOG,(char*)"\r\n\r\nInput: %s\r\n",input);
	bool oldecho = echo;
	bool changedEcho = true;
	if (prepareMode == PREPARE_MODE)  changedEcho = echo = true;

    //    generate reply by lookup first
	unsigned int retried = 0;
	sentenceRetryRejoinderTopic = inputRejoinderTopic; 
	sentenceRetryRejoinderRuleID =	inputRejoinderRuleID; 
	bool sentenceRetry = false;
	oobPossible = true;

retry:  
	char* start = nextInput; // where we read from
	if (trace & TRACE_INPUT) Log(STDTRACELOG,(char*)"\r\n\r\nInput: %s\r\n",input);
 	if (trace && sentenceRetry) DumpUserVariables(); 
	PrepareSentence(nextInput,true,true,false,true); // user input.. sets nextinput up to continue
	nextInput = SkipWhitespace(nextInput);

	// set %more and %morequestion
	moreToCome = moreToComeQuestion = false;	
	if (!atlimit)
	{
		char* at = nextInput-1;
		while (*++at)
		{
			if (*at == ' ' || *at == INPUTMARKER || *at == '\n' || *at == '\r') continue;	// ignore this junk
			moreToCome = true;	// there is more input coming
			break;
		}
		moreToComeQuestion = (strchr(nextInput,'?') != 0);
	}
	else
	{
		int xx = 0;
	}

	char nextWord[MAX_WORD_SIZE];
	ReadCompiledWord(nextInput,nextWord);
	WORDP next = FindWord(nextWord);
	if (next && next->properties & QWORD) moreToComeQuestion = true; // assume it will be a question (misses later ones in same input)
	if (changedEcho) echo = oldecho;
	
	if (PrepassSentence(prepassTopic)) return NOPROBLEM_BIT; // user input revise and resubmit?  -- COULD generate output and set rejoinders
	if (prepareMode == PREPARE_MODE || prepareMode == POS_MODE || tmpPrepareMode == POS_MODE) return NOPROBLEM_BIT; // just do prep work, no actual reply
	tokenCount += wordCount;
	sentenceRetry = false;
    if (!wordCount && responseIndex != 0) return NOPROBLEM_BIT; // nothing here and have an answer already. ignore this

	if (showTopics)
	{
		changedEcho = echo = true;
		impliedSet = 0;
		KeywordTopicsCode(NULL);
		for (unsigned int i = 1; i <=  FACTSET_COUNT(0); ++i)
		{
			FACT* F = factSet[0][i];
			WORDP D = Meaning2Word(F->subject);
			WORDP N = Meaning2Word(F->object);
			int topic = FindTopicIDByName(D->word);
			char* name = GetTopicName(topic);
			Log(STDTRACELOG,(char*)"%s (%s) : (char*)",name,N->word);
			//   look at references for this topic
			int start = -1;
			int startPosition = 0;
			int endPosition = 0;
			while (GetIthSpot(D,++start, startPosition, endPosition)) // find matches in sentence
			{
				// value of match of this topic in this sentence
				for (int k = startPosition; k <= endPosition; ++k) 
				{
					if (k != startPosition) Log(STDTRACELOG,(char*)"_");
					Log(STDTRACELOG,(char*)"%s",wordStarts[k]);
				}
				Log(STDTRACELOG,(char*)" ");
			}
			Log(STDTRACELOG,(char*)"\r\n");
		}
		impliedSet = ALREADY_HANDLED;
		if (changedEcho) echo = oldecho;
	}

	if (noReact) return NOPROBLEM_BIT;
	FunctionResult result =  Reply();
	if (result & RETRYSENTENCE_BIT && retried < MAX_RETRIES) 
	{
		inputRejoinderTopic  = sentenceRetryRejoinderTopic; 
		inputRejoinderRuleID = sentenceRetryRejoinderRuleID; 

		++retried;	 // allow  retry -- issues with this
		--inputSentenceCount;
		char* buf = AllocateBuffer();
		strcpy(buf,nextInput);	// protect future input
		strcpy(start,oldInputBuffer); // copy back current input
		strcat(start,(char*)" "); 
		strcat(start,buf); // add back future input
		nextInput = start; // retry old input
		sentenceRetry = true;
		FreeBuffer();
		goto retry; // try input again -- maybe we changed token controls...
	}
	if (result & FAILSENTENCE_BIT)  --inputSentenceCount;
	if (result == ENDINPUT_BIT) nextInput = ""; // end future input
	return result;
}

void OnceCode(const char* var,char* function) //   run before doing any of his input
{
	stringInverseFree = stringInverseStart; // drop any possible inverse used
	withinLoop = 0;
	callIndex = 0;
	topicIndex = currentTopicID = 0; 
	char* name = (!function || !*function) ? GetUserVariable(var) : function;
	int topic = FindTopicIDByName(name);
	if (!topic) return;
	ResetReuseSafety();

	int pushed = PushTopic(topic);
	if (pushed < 0) return;
	
	if (trace & (TRACE_MATCH|TRACE_PREPARE) && CheckTopicTrace()) 
	{
		if (!stricmp(var,(char*)"$cs_control_pre")) 
		{
			Log(STDTRACELOG,(char*)"\r\nPrePass\r\n");
		}
		if (!stricmp(var,(char*)"$cs_externaltag")) 
		{
			Log(STDTRACELOG,(char*)"\r\nPosTagging\r\n");
		}
		else 
		{
			Log(STDTRACELOG,(char*)"\r\n\r\nPostPass\r\n");
			Log(STDTRACELOG,(char*)"Pending topics: %s\r\n",ShowPendingTopics());
		}
	}
	
	// prove it has gambits
	topicBlock* block = TI(topic);
	if (BlockedBotAccess(topic) || GAMBIT_MAX(block->topicMaxRule) == 0)
	{
		char word[MAX_WORD_SIZE];
		sprintf(word,"There are no gambits in topic %s for %s.",GetTopicName(topic),var);
		AddResponse(word,0);
		return;
	}
	ruleErased = false;	
	AllocateOutputBuffer();
	PerformTopic(GAMBIT,currentOutputBase);
	FreeOutputBuffer();

	if (pushed) PopTopic();
	if (topicIndex) ReportBug((char*)"topics still stacked")
	if (globalDepth) ReportBug((char*)"Once code %s global depth not 0",name);
	topicIndex = currentTopicID = 0; // precaution
}
		
void AddHumanUsed(const char* reply)
{
	if (!*reply) return;
	if (humanSaidIndex >= MAX_USED) humanSaidIndex = 0; // chop back //  overflow is unlikely but if he inputs >10 sentences at once, could
    unsigned int i = humanSaidIndex++;
    *humanSaid[i] = ' ';
	reply = SkipOOB((char*)reply);
	size_t len = strlen(reply);
	if (len >= SAID_LIMIT) // too long to save in user file
	{
		strncpy(humanSaid[i]+1,reply,SAID_LIMIT); 
		humanSaid[i][SAID_LIMIT] = 0; 
	}
	else strcpy(humanSaid[i]+1,reply); 
}

void AddBotUsed(const char* reply,unsigned int len)
{
	if (chatbotSaidIndex >= MAX_USED) chatbotSaidIndex = 0; // overflow is unlikely but if he could  input >10 sentences at once
	unsigned int i = chatbotSaidIndex++;
    *chatbotSaid[i] = ' ';
	if (len >= SAID_LIMIT) // too long to save in user file
	{
		strncpy(chatbotSaid[i]+1,reply,SAID_LIMIT); 
		chatbotSaid[i][SAID_LIMIT] = 0; 
	}
	else strcpy(chatbotSaid[i]+1,reply); 
}

bool HasAlreadySaid(char* msg)
{
    if (!*msg) return true; 
    if (!currentRule || Repeatable(currentRule) || GetTopicFlags(currentTopicID) & TOPIC_REPEAT) return false;
	msg = TrimSpaces(msg);
	size_t actual = strlen(msg);
    for (int i = 0; i < chatbotSaidIndex; ++i) // said in previous recent  volleys
    {
		size_t len = strlen(chatbotSaid[i]+1);
		if (actual > (SAID_LIMIT-3)) actual = len;
        if (!strnicmp(msg,chatbotSaid[i]+1,actual+1)) return true; // actual sentence is indented one (flag for end reads in column 0)
    }
	for (int i = 0; i  < responseIndex; ++i) // already said this turn?
	{
		char* says = SkipOOB(responseData[i].response);
		size_t len = strlen(says);
		if (actual > (SAID_LIMIT-3)) actual = len;
        if (!strnicmp(msg,says,actual+1)) return true; 
	}
    return false;
}

static void SaveResponse(char* msg)
{
    strcpy(responseData[responseIndex].response,msg); // the response
    responseData[responseIndex].responseInputIndex = inputSentenceCount; // which sentence of the input was this in response to
	responseData[responseIndex].topic = currentTopicID; // what topic wrote this
	sprintf(responseData[responseIndex].id,(char*)".%d.%d",TOPLEVELID(currentRuleID),REJOINDERID(currentRuleID)); // what rule wrote this
	if (currentReuseID != -1) // if rule was referral reuse (local) , add in who did that
	{
		sprintf(responseData[responseIndex].id + strlen(responseData[responseIndex].id),(char*)".%d.%d.%d",currentReuseTopic,TOPLEVELID(currentReuseID),REJOINDERID(currentReuseID));
	}
	responseOrder[responseIndex] = (unsigned char)responseIndex;
	responseIndex++;
	if (responseIndex == MAX_RESPONSE_SENTENCES) --responseIndex;

	// now mark rule as used up if we can since it generated text
	SetErase(true); // top level rules can erase whenever they say something
	
	if (showWhy) Log(ECHOSTDTRACELOG,(char*)"\n  => %s %s %d.%d  %s\r\n",(!UsableRule(currentTopicID,currentRuleID)) ? (char*)"-" : (char*)"", GetTopicName(currentTopicID,false),TOPLEVELID(currentRuleID),REJOINDERID(currentRuleID),ShowRule(currentRule));
}

char* SkipOOB(char* buffer)
{
		// skip oob data 
	char* noOob = SkipWhitespace(buffer);
	if (*noOob == '[')
	{
		int count = 1;
		bool quote = false;
		while (*++noOob)
		{
			if (*noOob == '"' && *(noOob-1) != '\\') quote = !quote; // ignore within quotes
			if (quote) continue;
			if (*noOob == '[' && *(noOob-1) != '\\') ++count;
			if (*noOob == ']' && *(noOob-1) != '\\') 
			{
				--count;
				if (count == 0) 
				{	
					noOob = SkipWhitespace(noOob+1);	// after the oob end
					break;
				}
			}
		}
	}
	return noOob;
}

bool AddResponse(char* msg, unsigned int responseControl)
{
	if (!msg || !*msg) return true;
	char* buffer = AllocateBuffer();
    size_t len = strlen(msg);
 	if (len > OUTPUT_BUFFER_SIZE)
	{
		char word[MAX_WORD_SIZE];
		strncpy(word,msg,100);
		word[100] = 0;
		char* at = word;
		while ((at = strchr(at,'\n'))) *at = ' ';
		at = word;
		while ((at = strchr(at,'\r'))) *at = ' ';
		ReportBug((char*)"response too big %s...",word)
		strcpy(msg+OUTPUT_BUFFER_SIZE-5,(char*)" ... "); //   prevent trouble
		len = strlen(msg);
	}

	// Do not change any oob data or test for repeat
    strcpy(buffer,msg);
	char* at = SkipOOB(buffer);
	if (responseControl & RESPONSE_REMOVETILDE) RemoveTilde(at);
	if (responseControl & RESPONSE_ALTERUNDERSCORES)
	{
		Convert2Underscores(at,false); // leave new lines alone
		Convert2Blanks(at);	// dont keep underscores in output regardless
	}
	if (responseControl & RESPONSE_UPPERSTART) 	*at = GetUppercaseData(*at); 

	//   remove spaces before commas (geofacts often have them in city_,_state)
	if (responseControl & RESPONSE_REMOVESPACEBEFORECOMMA)
	{
		char* ptr = at;
		while (ptr && *ptr)
		{
			char* comma = strchr(ptr,',');
			if (comma && comma != buffer )
			{
				if (*--comma == ' ') memmove(comma,comma+1,strlen(comma));
				ptr = comma+2;
			}
			else if (comma) ptr = comma+1;
			else ptr = 0;
		}
	}

	if (!*at){} // we only have oob?
    else if (all || HasAlreadySaid(at) ) // dont really do this, either because it is a repeat or because we want to see all possible answers
    {
		if (all) Log(ECHOSTDTRACELOG,(char*)"Choice %d: %s  why:%s %d.%d %s\r\n\r\n",++choiceCount,at,GetTopicName(currentTopicID,false),TOPLEVELID(currentRuleID),REJOINDERID(currentRuleID),ShowRule(currentRule));
        else if (trace & TRACE_OUTPUT) Log(STDTRACELOG,(char*)"Rejected: %s already said\r\n",buffer);
		else if (showReject) Log(ECHOSTDTRACELOG,(char*)"Rejected: %s already said\r\n",buffer);
        memset(msg,0,len+1); //   kill partial output
		FreeBuffer();
        return false;
    }
    if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACETABLOG,(char*)"Message: %s\r\n",buffer);

    SaveResponse(buffer);
    if (!timerLimit || timerCheckInstance != TIMEOUT_INSTANCE) memset(msg,0,len+1); // erase all of original message, +  1 extra as leading space
	FreeBuffer();
    return true;
}

void PrepareSentence(char* input,bool mark,bool user, bool analyze,bool oobstart,bool atlimit) // set currentInput and nextInput
{
	lastRestoredIndex = ++sentencePreparationIndex; // an id marker
	char* original[MAX_SENTENCE_LENGTH];
	unsigned int mytrace = trace;
	clock_t start_time = ElapsedMilliseconds();
	if (prepareMode == PREPARE_MODE) mytrace = 0;
	ResetSentence();
	ResetTokenSystem();

	char* ptr = input;
	tokenFlags |= (user) ? USERINPUT : 0; // remove any question mark
    ptr = Tokenize(ptr,wordCount,wordStarts,false,false,oobstart); 
 	upperCount = 0;
	lowerCount = 0;
	for (int i = 1; i <= wordCount; ++i)   // see about SHOUTing
	{
		char* at = wordStarts[i] - 1;
		while (*++at)
		{
			if (IsAlphaUTF8(*at))
			{
				if (IsUpperCase(*at)) ++upperCount;
				else ++lowerCount;
			}
		}	
	}

	// set derivation data on original words of user before we do substitution
	for (int i = 1; i <= wordCount; ++i) derivationIndex[i] = (unsigned short)((i << 8) | i); // track where substitutions come from
	memcpy(derivationSentence+1,wordStarts+1,wordCount * sizeof(char*));
	derivationLength = wordCount;
	derivationSentence[wordCount+1] = NULL;

	if (oobPossible && *wordStarts[1] == '[' && !wordStarts[1][1] && *wordStarts[wordCount] == ']'  && !wordStarts[wordCount][1]) 
	{
		oobPossible = false; // no more for now
		oobExists = true;
	}
	else oobExists = false;

 	if (tokenControl & ONLY_LOWERCASE && !oobExists) // force lower case
	{
		for (int i = FindOOBEnd(1); i <= wordCount; ++i) 
		{
			if (wordStarts[i][0] != 'I' || wordStarts[i][1]) MakeLowerCase(wordStarts[i]);
		}
	}
	
	// this is the input we currently are processing.
	*oldInputBuffer = 0;
	char* at = oldInputBuffer;
	for (int i = 1; i <= wordCount; ++i)
	{
		strcpy(at,wordStarts[i]);
		at += strlen(at);
		*at++ = ' ';
	}
	*at = 0;

	// force Lower case on plural start word which has singular meaning (but for substitutes
	if (wordCount  && !oobExists)
	{
		char word[MAX_WORD_SIZE];
		MakeLowerCopy(word,wordStarts[1]);
		size_t len = strlen(word);
		if (strcmp(word,wordStarts[1]) && word[1] && word[len-1] == 's') // is a different case and seemingly plural
		{
			WORDP O = FindWord(word,len,UPPERCASE_LOOKUP);
			WORDP D = FindWord(word,len,LOWERCASE_LOOKUP);
			if (D && D->properties & PRONOUN_BITS) {;} // dont consider hers and his as plurals of some noun
			else if (O && O->properties & NOUN) {;}// we know this noun (like name James)
			else
			{
				char* singular = GetSingularNoun(word,true,false);
				D = FindWord(singular);
				if (D && stricmp(singular,word)) // singular exists different from plural, use lower case form
				{
					D = StoreWord(word); // lower case plural form
					if (D->internalBits & UPPERCASE_HASH) AddProperty(D,NOUN_PROPER_PLURAL|NOUN);
					else AddProperty(D,NOUN_PLURAL|NOUN);
					wordStarts[1] = reuseAllocation(wordStarts[1],D->word);
				}
			}
		}
	}
 	if (mytrace & TRACE_PREPARE|| prepareMode == PREPARE_MODE)
	{
		Log(STDTRACELOG,(char*)"TokenControl: ");
		DumpTokenControls(tokenControl);
		Log(STDTRACELOG,(char*)"\r\n\r\n");
		if (tokenFlags & USERINPUT) Log(STDTRACELOG,(char*)"\r\nOriginal User Input: %s\r\n",input);
		else Log(STDTRACELOG,(char*)"\r\nOriginal Chatbot Output: %s\r\n",input);
		Log(STDTRACELOG,(char*)"Tokenized into: ");
		for (int i = 1; i <= wordCount; ++i) Log(STDTRACELOG,(char*)"%s  ",wordStarts[i]);
		Log(STDTRACELOG,(char*)"\r\n");
	}
	int originalCount = wordCount;
	if (mytrace & TRACE_PREPARE || prepareMode) memcpy(original+1,wordStarts+1,wordCount * sizeof(char*));	// replicate for test

	if (tokenControl & (DO_SUBSTITUTE_SYSTEM|DO_PRIVATE)  && !oobExists)  
	{
		// test for punctuation not done by substitutes (eg "?\")
	char c = (wordCount) ? *wordStarts[wordCount] : 0;
		if ((c == '?' || c == '!') && wordStarts[wordCount])  
		{
			char* tokens[3];
			tokens[1] = AllocateString(wordStarts[wordCount],1,1);
			ReplaceWords(wordCount,1,1,tokens);
		}  

		// test for punctuation badly done at end (eg "?\")
		ProcessSubstitutes();
 		if (mytrace & TRACE_PREPARE || prepareMode == PREPARE_MODE)
		{
			int changed = 0;
			if (wordCount != originalCount) changed = true;
			for (int i = 1; i <= wordCount; ++i) 
			{
				if (original[i] != wordStarts[i]) 
				{
					changed = i;
					break;
				}
			}
			if (changed)
			{
				Log(STDTRACELOG,(char*)"Substituted (");
				if (tokenFlags & DO_ESSENTIALS) Log(STDTRACELOG, "essentials ");
				if (tokenFlags & DO_SUBSTITUTES) Log(STDTRACELOG, "substitutes ");
				if (tokenFlags & DO_CONTRACTIONS) Log(STDTRACELOG, "contractions ");
				if (tokenFlags & DO_INTERJECTIONS) Log(STDTRACELOG, "interjections ");
				if (tokenFlags & DO_BRITISH) Log(STDTRACELOG, "british ");
				if (tokenFlags & DO_SPELLING) Log(STDTRACELOG, "spelling ");
				if (tokenFlags & DO_TEXTING) Log(STDTRACELOG, "texting ");
				if (tokenFlags & DO_NOISE) Log(STDTRACELOG, "noise ");
				if (tokenFlags & DO_PRIVATE) Log(STDTRACELOG, "private ");
				Log(STDTRACELOG,(char*)") into: ");
				for (int i = 1; i <= wordCount; ++i) Log(STDTRACELOG,(char*)"%s  ",wordStarts[i]);
				Log(STDTRACELOG,(char*)"\r\n");
				memcpy(original+1,wordStarts+1,wordCount * sizeof(char*));	// replicate for test
			}
			originalCount = wordCount;
		}
	}
	
	// if 1st token is an interjection DO NOT allow this to be a question
	if (wordCount && wordStarts[1] && *wordStarts[1] == '~' && !(tokenControl & NO_INFER_QUESTION)) 
		tokenFlags &= -1 ^ QUESTIONMARK;

	// special lowercasing of 1st word if it COULD be AUXVERB and is followed by pronoun - avoid DO and Will and other confusions
	if (wordCount > 1 && IsUpperCase(*wordStarts[1])  && !oobExists)
	{
		WORDP X = FindWord(wordStarts[1],0,LOWERCASE_LOOKUP);
		if (X && X->properties & AUX_VERB)
		{
			WORDP Y = FindWord(wordStarts[2]);
			if (Y && Y->properties & PRONOUN_BITS) wordStarts[1] = X->word;
		}
	}

	int i;
 	for (i = 1; i <= wordCount; ++i)  originalCapState[i] = IsUpperCase(*wordStarts[i]); // note cap state
 	if (mytrace & TRACE_PREPARE || prepareMode == PREPARE_MODE) 
	{
		int changed = 0;
		if (wordCount != originalCount) changed = true;
		for (int j = 1; j <= wordCount; ++j) if (original[j] != wordStarts[j]) changed = j;
		if (changed)
		{
			if (tokenFlags & DO_PROPERNAME_MERGE) Log(STDTRACELOG,(char*)"Name-");
			if (tokenFlags & DO_NUMBER_MERGE) Log(STDTRACELOG,(char*)"Number-");
			if (tokenFlags & DO_DATE_MERGE) Log(STDTRACELOG,(char*)"Date-");
			Log(STDTRACELOG,(char*)"merged: ");
			for (int i = 1; i <= wordCount; ++i) Log(STDTRACELOG,(char*)"%s  ",wordStarts[i]);
			Log(STDTRACELOG,(char*)"\r\n");
			memcpy(original+1,wordStarts+1,wordCount * sizeof(char*));	// replicate for test
			originalCount = wordCount;
		}
	}

	// spell check unless 1st word is already a known interjection. Will become standalone sentence
	if (tokenControl & DO_SPELLCHECK && wordCount && *wordStarts[1] != '~'  && !oobExists)  
	{
		if (SpellCheckSentence())
		{
			tokenFlags |= DO_SPELLCHECK;
			if (tokenControl & (DO_SUBSTITUTE_SYSTEM|DO_PRIVATE))  ProcessSubstitutes();
		}
		if (mytrace & TRACE_PREPARE || prepareMode == PREPARE_MODE)
		{
 			int changed = 0;
			if (wordCount != originalCount) changed = true;
			for (int i = 1; i <= wordCount; ++i) if (original[i] != wordStarts[i]) changed = i;
			if (changed)
			{
				Log(STDTRACELOG,(char*)"Spelling changed into: ");
				for (int i = 1; i <= wordCount; ++i) Log(STDTRACELOG,(char*)"%s  ",wordStarts[i]);
				Log(STDTRACELOG,(char*)"\r\n");
			}
		}
	}
	if (tokenControl & DO_PROPERNAME_MERGE && wordCount  && !oobExists)  ProperNameMerge();   
	if (tokenControl & DO_DATE_MERGE && wordCount  && !oobExists)  ProcessCompositeDate();   
 	if (tokenControl & DO_NUMBER_MERGE && wordCount && !oobExists)  ProcessCompositeNumber(); //   numbers AFTER titles, so they dont change a title
	if (tokenControl & DO_SPLIT_UNDERSCORE &&  !oobExists)  ProcessSplitUnderscores(); 
	
	if (!analyze) nextInput = ptr;	//   allow system to overwrite input here

	if (tokenControl & DO_INTERJECTION_SPLITTING && wordCount > 1 && *wordStarts[1] == '~') // interjection. handle as own sentence
	{
		// formulate an input insertion
		char buffer[BIG_WORD_SIZE];
		*buffer = 0;
		int more = (derivationIndex[1] & 0x000f) + 1; // at end
		if (more <= derivationLength && *derivationSentence[more] == ',') // swallow comma into original
		{
			++more;	// dont add comma onto input
			derivationIndex[1]++;	// extend end onto comma
		}
		for (int i = more; i <= derivationLength; ++i) // rest of data after input.
		{
			strcat(buffer,derivationSentence[i]);
			strcat(buffer,(char*)" ");
		}

		// what the rest of the input had as punctuation OR insure it does have it
		char* end = buffer + strlen(buffer);
		char* tail = end - 1;
		while (tail > buffer && *tail == ' ') --tail; 
		if (tokenFlags & QUESTIONMARK && *tail != '?') strcat(tail,(char*)"? (char*)");
		else if (tokenFlags & EXCLAMATIONMARK && *tail != '!' ) strcat(tail,(char*)"! ");
		else if (*tail != '.') strcat(tail,(char*)". ");

		if (!analyze) 
		{
			strcpy(end,nextInput); // a copy of rest of input
			strcpy(nextInput,buffer); // unprocessed user input is here
			ptr = nextInput;
		}
		wordCount = 1;
		tokenFlags |= DO_INTERJECTION_SPLITTING;
	}
	
	// copy the raw sentence original input that this sentence used
	*rawSentenceCopy = 0;
	char* atword = rawSentenceCopy;
	int reach = 0;
	for (int i = 1; i <= wordCount; ++i)
	{
		int start = derivationIndex[i] >> 8;
		int end = derivationIndex[i] & 0x00ff;
		if (start > reach)
		{
			reach = end;
			for (int j = start; j <= reach; ++j)
			{
				strcpy(atword, derivationSentence[j]);
				atword += strlen(atword);
				if (j < derivationLength) *atword++ = ' ';
				*atword = 0;
			}
		}
	}
	
	if (mytrace & TRACE_PREPARE || prepareMode == PREPARE_MODE)
	{
		Log(STDTRACELOG,(char*)"Actual used input: ");
		for (int i = 1; i <= wordCount; ++i) 
		{
			Log(STDTRACELOG,(char*)"%s",wordStarts[i]);
			int start = derivationIndex[i] >> 8;
			int end = derivationIndex[i] & 0x00ff;
			if (start == end && wordStarts[i] == derivationSentence[start]) {;} // unchanged from original
			else // it came from somewhere else
			{
				int start = derivationIndex[i] >> 8;
				int end = derivationIndex[i] & 0x00Ff;
				Log(STDTRACELOG,(char*)"(");
				for (int j = start; j <= end; ++j)
				{
					if (j != start) Log(STDTRACELOG,(char*)" ");
					Log(STDTRACELOG,(char*)"%s",derivationSentence[j]);
				}
				Log(STDTRACELOG,(char*)")");
			}
			Log(STDTRACELOG,(char*)" ");
		}
		Log(STDTRACELOG,(char*)"\r\n\r\n");
	}

	if (echoSource == SOURCE_ECHO_LOG) 
	{
		Log(ECHOSTDTRACELOG,(char*)"  => ");
		for (int i = 1; i <= wordCount; ++i) Log(STDTRACELOG,(char*)"%s  ",wordStarts[i]);
		Log(ECHOSTDTRACELOG,(char*)"\r\n");
	}

	wordStarts[wordCount+1] = reuseAllocation(wordStarts[wordCount+1],(char*)""); // visible end of data in debug display
	wordStarts[wordCount+2] = 0;
    if (mark && wordCount) MarkAllImpliedWords();

	if (prepareMode == PREPARE_MODE || trace & TRACE_POS || prepareMode == POS_MODE || (prepareMode == PENN_MODE && trace & TRACE_POS)) DumpTokenFlags((char*)"After parse");
	if (timing & TIME_PREPARE) {
		int diff = ElapsedMilliseconds() - start_time;
		if (timing & TIME_ALWAYS || diff > 0) Log(STDTIMELOG, (char*)"Prepare %s time: %d ms\r\n", input, diff);
	}
}

#ifdef WIN32
    #include <direct.h>
    #define GetCurrentDir _getcwd
#else
    #include <unistd.h>
    #define GetCurrentDir getcwd
#endif

#ifndef NOMAIN
int main(int argc, char * argv[]) 
{
	for (int i = 1; i < argc; ++i)
	{
		if (!strnicmp(argv[i],"root=",5)) 
		{
#ifdef WIN32
			SetCurrentDirectory((char*)argv[i]+5);
#else
			chdir((char*)argv[i]+5);
#endif
		}
	}

	FILE* in = FopenStaticReadOnly((char*)"SRC/dictionarySystem.h"); // SRC/dictionarySystem.h
	if (!in) // if we are not at top level, try going up a level
	{
#ifdef WIN32
		if (!SetCurrentDirectory((char*)"..")) // move from BINARIES to top level
			myexit((char*)"unable to change up\r\n");
#else
		chdir((char*)"..");
#endif
	}
	else FClose(in); 
	if (InitSystem(argc,argv)) myexit((char*)"failed to load memory\r\n");
    if (!server) 
	{
		quitting = false; // allow local bots to continue regardless
		MainLoop();
	}
	else if (quitting) {;} // boot load requests quit
#ifndef DISCARDSERVER
    else
    {
#ifdef EVSERVER
        if (evsrv_run() == -1) Log(SERVERLOG, "evsrv_run() returned -1");
#else
        InternetServer();
#endif
    }
#endif
	CloseSystem();
}
#endif
