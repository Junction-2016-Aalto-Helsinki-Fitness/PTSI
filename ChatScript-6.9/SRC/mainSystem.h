#ifndef MAINSYSTEMH
#define MAINSYSTEMH
#ifdef INFORMATION
Copyright (C) 2011-2016 by Bruce Wilcox

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#endif

#define ID_SIZE 500
#define OUTPUT_BUFFER_SIZE 20000

typedef struct RESPONSE
{
    unsigned int responseInputIndex;                        // which input sentence does this stem from?
	int topic;										// topic of rule
	char id[24];											// .100.30
 	char relayid[24];										// 324.100.30 topic and rule doing relay
	char response[OUTPUT_BUFFER_SIZE];						// answer sentences, 1 or more per input line
} RESPONSE;

#define SENTENCE_LIMIT 50 // how many sentence from user do we accept
#define MAX_RESPONSE_SENTENCES 20
#define MAX_SENTENCE_LENGTH 256 // room for +4 after content (keep a power of 4 for savesentence code)
#define REAL_SENTENCE_LIMIT 252 // stay under char size boundary and leave excess room
#define TIMEOUT_INSTANCE 1000000

#define PENDING_RESTART -1	// perform chat returns this flag on turn

#define START_BIT 0x8000000000000000ULL	// used looping thru bit masks
#define INPUTMARKER '`'	// used to start and end ^input data

// values of prepareMode
 enum PrepareMode { // how to treat input
	NO_MODE = 0,				// std processing of user inputs
	POS_MODE = 1,				// just pos tag the inputs
	PREPARE_MODE = 2,			// just prepare the inputs
	POSVERIFY_MODE = 4,
	POSTIME_MODE = 8,
	PENN_MODE = 16,
	REGRESS_MODE = 32,			// inputs are from a regression test
};

 enum RegressionMode { 
	NO_REGRESSION = 0,
	NORMAL_REGRESSION = 1,
	REGRESS_REGRESSION = 2,
};

// values of echoSource
 enum EchoSource {
	NO_SOURCE_ECHO = 0,
	SOURCE_ECHO_USER = 1,
	SOURCE_ECHO_LOG = 2,
};
#define MAX_TRACED_FUNCTIONS 50
 
extern unsigned short int derivationIndex[256];
extern int derivationLength;
extern char* derivationSentence[MAX_SENTENCE_LENGTH];
extern bool docstats;
extern unsigned int docSentenceCount;
extern clock_t startTimeInfo;
extern unsigned int outputLength;
extern bool readingDocument;
extern bool callback;
extern char inputCopy[MAX_BUFFER_SIZE]; 
extern unsigned char responseOrder[MAX_RESPONSE_SENTENCES+1];
extern RESPONSE responseData[MAX_RESPONSE_SENTENCES+1];
extern int responseIndex;
extern bool documentMode;
extern bool assignedLogin;
extern unsigned int volleyCount;
extern FILE* sourceFile;
extern bool oobExists;
extern char hostname[100];
extern int argc;
extern char** argv;
extern  bool pendingRestart;
extern  bool pendingUserReset;
extern unsigned long sourceStart;
extern unsigned int sourceTokens;
extern unsigned int sourceLines;
extern char* version;
extern int sentencePreparationIndex;
extern int lastRestoredIndex;
extern unsigned int tokenCount;
extern unsigned int choiceCount;
extern bool redo;
extern bool commandLineCompile;
extern int inputCounter,totalCounter;
extern int inputSentenceCount;  
extern char* extraTopicData;
extern char postProcessing;
extern char rawSentenceCopy[MAX_BUFFER_SIZE];
extern char* authorizations;
extern uint64 tokenControl;
extern unsigned int responseControl;
extern bool moreToCome,moreToComeQuestion;
extern unsigned int trace;
extern unsigned int timing;
extern int regression;
extern EchoSource echoSource;
extern bool all;
extern PrepareMode prepareMode;
extern PrepareMode tmpPrepareMode;
extern clock_t startSystem;
extern char oktest[MAX_WORD_SIZE];
extern int timerLimit;
extern int timerCheckRate;
extern int timerCheckInstance;
extern clock_t volleyStartTime;
extern bool autonumber;
extern bool showWhy;
extern bool showTopic;
extern bool showInput;
extern bool showReject;
extern bool showTopics;
extern bool shortPos;
extern char users[100];
extern char logs[100];

// pending control
extern int systemReset;
extern bool quitting;
extern bool unusedRejoinder;
extern bool overrideAuthorization;
extern bool noReact;

// server
extern unsigned int port;
extern bool server;
#ifndef DISCARDSERVER
extern std::string interfaceKind;
#endif

// buffers
extern char ourMainInputBuffer[MAX_BUFFER_SIZE];
extern char* mainInputBuffer;
extern char ourMainOutputBuffer[MAX_BUFFER_SIZE];
extern char* mainOutputBuffer;
extern char currentInput[MAX_BUFFER_SIZE];
extern char revertBuffer[MAX_BUFFER_SIZE];
extern char* readBuffer;
extern char* nextInput;

extern char activeTopic[200];

extern int always; 
extern unsigned long callBackTime;
extern unsigned long callBackDelay;
extern unsigned long loopBackTime;
extern unsigned long loopBackDelay;
extern unsigned long alarmTime;
extern unsigned long alarmDelay;
void Restart();
void ProcessInputFile();
bool ProcessInputDelays(char* buffer,bool hitkey);
char* SkipOOB(char* buffer);

// startup
#ifdef DLL
extern "C" __declspec(dllexport) unsigned int InitSystem(int argc, char * argv[],char* unchangedPath = NULL,char* readonlyPath = NULL, char* writablePath = NULL, USERFILESYSTEM* userfiles = NULL);
#else
unsigned int InitSystem(int argc, char * argv[],char* unchangedPath = NULL,char* readonlyPath = NULL, char* writablePath = NULL, USERFILESYSTEM* userfiles = NULL);
#endif
int FindOOBEnd(int start);
void InitStandalone();
void CreateSystem();
void ReloadSystem();
void CloseSystem();
void PartiallyCloseSystem();
int main(int argc, char * argv[]);
void ProcessOOB(char* buffer);
void ComputeWhy(char* buffer, int n);

// Input processing
void MainLoop();
void FinishVolley(char* input,char* output,char* summary);
int ProcessInput(char* input);
FunctionResult DoSentence(char* prepassTopic,bool atlimit);

#ifdef DLL
extern "C" __declspec(dllexport) int PerformChat(char* user, char* usee, char* incoming,char* ip,char* output);
extern "C" __declspec(dllexport) int PerformChatGivenTopic(char* user, char* usee, char* incoming,char* ip,char* output,char* topic);
#else
int PerformChat(char* user, char* usee, char* incoming,char* ip,char* output);
int PerformChatGivenTopic(char* user, char* usee, char* incoming,char* ip,char* output,char* topic);
#endif

void ResetSentence();
void ResetToPreUser();
void PrepareSentence(char* input,bool mark = true,bool user=true, bool analyze = false, bool oobstart = false,bool atlimit = false);
bool PrepassSentence(char* presspassTopic);
FunctionResult Reply();
void OnceCode(const char* var,char* topic = NULL);
void AddBotUsed(const char* reply,unsigned int len);
void AddHumanUsed(const char* reply);
bool HasAlreadySaid(char* msg);
bool AddResponse(char* msg, unsigned int controls);

#endif
