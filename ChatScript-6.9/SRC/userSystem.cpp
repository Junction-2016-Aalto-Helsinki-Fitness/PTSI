#include "common.h"


#ifndef USERFACTS 
#define USERFACTS 100
#endif
unsigned int userFactCount = USERFACTS;			// how many facts user may save in topic file
bool serverRetryOK = false;
bool stopUserWrite = false;
static bool verifyUserFacts = true;
static char* backupMessages = NULL;

#define MAX_USER_MESSAGES MAX_USED

//   replies we have tried already
char chatbotSaid[MAX_USED+1][SAID_LIMIT+3];  //   tracks last n messages sent to user
char humanSaid[MAX_USED+1][SAID_LIMIT+3]; //   tracks last n messages sent by user in parallel with userSaid
int humanSaidIndex;
int chatbotSaidIndex;

static char* saveVersion = "jul2116";	// format of save file

int userFirstLine = 0;	// start volley of current conversation
uint64 setControl = 0;	// which sets should be saved with user

char ipAddress[50];
char computerID[ID_SIZE];
char computerIDwSpace[ID_SIZE];
char loginID[ID_SIZE];    //   user FILE name (lower case)
char loginName[ID_SIZE];    //   user typed name
char callerIP[ID_SIZE];

char timeturn15[100];
char timeturn0[20];
char timePrior[20];

void PartialLogin(char* caller,char* ip)
{
    //   make user name safe for file system
	char*  id = loginID;
	char* at = caller-1;
	char c;
	while ((c = *++at)) 
	{
		if (IsAlphaUTF8OrDigit(c) || c == '-' || c == '_'  || c == '+') *id++ = c;
		else if (c == ' ') *id++ = '_';
	}
	*id = 0;

	sprintf(logFilename,(char*)"%s/%slog-%s.txt",users,GetUserPath(loginID),loginID); // user log goes here

	if (ip) strcpy(callerIP,ip);
	else *callerIP = 0;
}

void Login(char* caller,char* usee,char* ip) //   select the participants
{
	strcpy(ipAddress,(ip) ? ip : (char*)"");
	if (!stricmp(usee,(char*)"trace")) // enable tracing during login
	{
		trace = (unsigned int) -1;
		echo = true;
		*usee = 0;
	}
	if (!stricmp(usee, (char*)"time")) // enable timing during login
	{
		timing = (unsigned int)-1 ^ TIME_ALWAYS;
		*usee = 0;
	}
	if (*usee) MakeLowerCopy(computerID,usee);
	else ReadComputerID(); //   we are defaulting the chatee
	if (!*computerID) ReportBug((char*)"No default bot?\r\n")

	//   for topic access validation
	*computerIDwSpace = ' ';
	MakeLowerCopy(computerIDwSpace+1,computerID);
	strcat(computerIDwSpace,(char*)" ");

	if (ip && *ip) // maybe use ip in generating unique login
	{
		if (!stricmp(caller,(char*)"guest")) sprintf(caller,(char*)"guest%s",ip);
		else if (*caller == '.') sprintf(caller,(char*)"%s",ip);
	}
	char* ptr = caller-1;
	while (*++ptr) 
	{
		if (!IsAlphaUTF8OrDigit(*ptr) && *ptr != '-' && *ptr != '+') *ptr = '_'; // require simple file names
	}

    //   prepare for chat
    PartialLogin(caller,ip);
 }

void ReadComputerID()
{
	strcpy(computerID,(char*)"anonymous");
	WORDP D = FindWord((char*)"defaultbot",0); // do we have a FACT with the default bot in it as verb
	if (D)
	{
		FACT* F = GetVerbNondeadHead(D);
		if (F) MakeLowerCopy(computerID,Meaning2Word(F->subject)->word);
	}
}

void ResetUserChat()
{
 	chatbotSaidIndex = humanSaidIndex = 0;
	setControl = 0;
	for (unsigned int i = 0; i <= MAX_FIND_SETS; ++i) SET_FACTSET_COUNT(i,0);
}

static char* WriteUserFacts(char* ptr,bool sharefile,int limit)
{
	if (!ptr) return NULL;
	
    //   write out fact sets first, before destroying any transient facts
	sprintf(ptr,(char*)"%x #set flags\r\n",(unsigned int) setControl);
	ptr += strlen(ptr);
	unsigned int i;
    unsigned int count;
	if (!shared || sharefile)  for (i = 0; i <= MAX_FIND_SETS; ++i) 
    {
		if (!(setControl & (uint64) (1 << i))) continue; // purely transient stuff

		//   remove dead references
		FACT** set = factSet[i];
        count = FACTSET_COUNT(i);
		unsigned int j;
        for (j = 1; j <= count; ++j)
		{
			FACT* F = set[j];
			if (F && F->flags & FACTDEAD)
			{
				memmove(&set[j],&set[j+1],sizeof(FACT*) * (count - j));
				--count;
				--j;
			}
		}
        if (!count) continue;

		// save this set
		sprintf(ptr,(char*)"#set %d\r\n",i); 
		ptr += strlen(ptr);
        for (j = 1; j <= count; ++j)
		{
			FACT* F = factSet[i][j];
			if (!F) strcpy(ptr,(char*)"0\r\n");
			else
			{
				WriteFact(F,false,ptr,false,true);
				if (F > factLocked) F->flags |= MARKED_FACT;	 // since we wrote this out here, DONT write out in general writeouts..
			}
			ptr += strlen(ptr);
			if ((unsigned int)(ptr - userDataBase) >= (userCacheSize - OVERFLOW_SAFETY_MARGIN)) return NULL;
		}
		sprintf(ptr,(char*)"%s",(char*)"#end set\n"); 
		ptr += strlen(ptr);
     }
	strcpy(ptr,(char*)"#`end fact sets\r\n");
	ptr += strlen(ptr);

	// most recent facts, in order, but not those written out already as part of fact set (in case FACTDUPLICATE is on, dont want to recreate and not build2 layer facts)
	FACT* F = factFree+1; // point to 1st unused fact
	while (--F > factLocked && limit) // backwards down to base system facts
	{
		WORDP D = Meaning2Word(F->subject);
		if (shared && !sharefile)  continue;
		if (!(F->flags & (FACTDEAD|FACTTRANSIENT|MARKED_FACT|FACTBUILD2))) --limit; // we will write this
	}
	// ends on factlocked, which is not to be written out
	int counter = 0;
 	while (++F <= factFree)  // factfree is a valid fact
	{
		WORDP D = Meaning2Word(F->subject);
		if (shared && !sharefile)  continue;
		if (!(F->flags & (FACTDEAD|FACTTRANSIENT|MARKED_FACT|FACTBUILD2))) 
		{
			++counter;
			WriteFact(F,true,ptr,false,true); // facts are escaped safe for JSON
			ptr += strlen(ptr);
			if ((unsigned int)(ptr - userDataBase) >= (userCacheSize - OVERFLOW_SAFETY_MARGIN)) return NULL;
		}
	}
	//ClearUserFacts();
	sprintf(ptr,(char*)"#`end user facts %d\r\n",counter);
	ptr += strlen(ptr);

	return ptr;
}

static void CheckUserFacts()
{
}


static bool ReadUserFacts()
{	
    //   read in fact sets
    char word[MAX_WORD_SIZE];
    *word = 0;
    ReadALine(readBuffer, 0); //   setControl
	ReadHex(readBuffer,setControl);
    while (ReadALine(readBuffer, 0)>= 0) 
    {
		if (*readBuffer == '#' && readBuffer[1] == ENDUNIT) break; // end of sets to read
		char* ptr = ReadCompiledWord(readBuffer,word);
        int setid;
        ptr = ReadInt(ptr,setid); 
		SET_FACTSET_COUNT(setid,0);
		if (trace & TRACE_USER) Log(STDTRACELOG,(char*)"Facts[%d]\r\n",setid);
	    while (ReadALine(readBuffer, 0)>= 0) 
		{
			if (*readBuffer == '#') break;
			char* lclptr = readBuffer;
			FACT* F = ReadFact(lclptr,0);
			AddFact(setid,F);
			if (trace & TRACE_USER) TraceFact(F);
        }
		if (*readBuffer == '#' && readBuffer[1] == ENDUNIT) break; // otherwise has #end set as the line
	}
	if (strcmp(readBuffer,(char*)"#`end fact sets")) 
	{
		ReportBug((char*)"Bad fact sets alignment\r\n")
		return false;
	}

	// read long-term user facts
	while (ReadALine(readBuffer, 0)>= 0) 
	{
		if (*readBuffer == '#' && readBuffer[1] == ENDUNIT) break;
		char* data = readBuffer;
		if (*data == '(' && strchr(data,')')) 
		{
			if (!ReadFact(data,0)) return false;
		}
		else 
		{
			ReportBug((char*)"Bad user fact %s\r\n",readBuffer)
			return false;
		}
		
	}	
    if (strncmp(readBuffer,(char*)"#`end user facts",16)) 
	{
		ReportBug((char*)"Bad user facts alignment\r\n")
		return false;
	}

	return true;
}

static char* SafeLine(char* line) // be JSON text safe
{
	if (!line) return line; // null variable (traced)
	char* word = AllocateBuffer();
	AddEscapes(word,line,false);
	FreeBuffer();
	return word; // buffer will be used immediately
}

static char* WriteRecentMessages(char* ptr,bool sharefile,int messageCount)
{
	if (!ptr) return NULL; // buffer ran out long ago

    //   recent human inputs
	int start = humanSaidIndex - messageCount; 
	if (start < 0) start = 0;
	int i;
    if (!shared || sharefile) for (i = start; i < humanSaidIndex; ++i)  
	{
		size_t len = strlen(humanSaid[i]);
		if (len == 0) continue;
		if (len > 200) humanSaid[i][200] = 0;
		sprintf(ptr,(char*)"%s\r\n",SafeLine(humanSaid[i]));
		ptr += strlen(ptr);
		if ((unsigned int)(ptr - userDataBase) >= (userCacheSize - OVERFLOW_SAFETY_MARGIN)) return NULL;
	}
	strcpy(ptr,(char*)"#`end user\r\n");
	ptr += strlen(ptr);
	
	// recent chatbot outputs
 	start = chatbotSaidIndex - messageCount;
	if (start < 0) start = 0;
    if (!shared || sharefile) for (i = start; i < chatbotSaidIndex; ++i) 
	{
		size_t len = strlen(chatbotSaid[i]);
		if (len == 0) continue;
		if (len > 200) chatbotSaid[i][200] = 0;
		sprintf(ptr,(char*)"%s\r\n",SafeLine(chatbotSaid[i]));
		ptr += strlen(ptr);
		if ((unsigned int)(ptr - userDataBase) >= (userCacheSize - OVERFLOW_SAFETY_MARGIN)) return NULL;
	}
	strcpy(ptr,(char*)"#`end chatbot\r\n");
	ptr += strlen(ptr);

	return ptr;
}

static bool ReadRecentMessages()
{
	char* buffer = AllocateBuffer();
	char* recover = AllocateBuffer();
	char* original = buffer;
	*buffer = 0;
	buffer[1] = 0;
    for (humanSaidIndex = 0; humanSaidIndex <= MAX_USED; ++humanSaidIndex) 
    {
        ReadALine(recover, 0);
		strcpy(humanSaid[humanSaidIndex],RemoveEscapesWeAdded( recover));
		if (*humanSaid[humanSaidIndex] == '#' && humanSaid[humanSaidIndex][1] == ENDUNIT) break; // #end
		strcpy(buffer,humanSaid[humanSaidIndex]);
		buffer += strlen(buffer) + 1;
    }
	if (humanSaidIndex > MAX_USED || strcmp(humanSaid[humanSaidIndex],(char*)"#`end user"))  // failure to end right
	{
		humanSaidIndex = 0;
		chatbotSaidIndex = 0;
		ReportBug((char*)"bad humansaid")
		return false;
	}
	else *humanSaid[humanSaidIndex] = 0;
	*buffer++ = 0;

	for (chatbotSaidIndex = 0; chatbotSaidIndex <= MAX_USED; ++chatbotSaidIndex) 
    {
        ReadALine(recover, 0);
		strcpy(chatbotSaid[chatbotSaidIndex],RemoveEscapesWeAdded( recover));
		if (*chatbotSaid[chatbotSaidIndex] == '#' && chatbotSaid[chatbotSaidIndex][1] == ENDUNIT) break; // #end
		strcpy(buffer,chatbotSaid[chatbotSaidIndex]);
		buffer += strlen(buffer) + 1;
    }
	if (chatbotSaidIndex > MAX_USED || strcmp(chatbotSaid[chatbotSaidIndex],(char*)"#`end chatbot")) // failure to end right
	{
		chatbotSaidIndex = 0;
		ReportBug((char*)"Bad message alignment\r\n")
		return false;
	}
	else *chatbotSaid[chatbotSaidIndex] = 0;
	*buffer++ = 0;
	backupMessages = AllocateString(original,buffer-original); // create a backup copy
	FreeBuffer();
	FreeBuffer();
	return true;
}

void RecoverUser() // regain stuff we loaded from user
{
	chatbotSaidIndex = humanSaidIndex = 0;
	char* at = backupMessages;
	while (at && *at) // read human said messages
	{
		strcpy(humanSaid[humanSaidIndex++],at);
		at += strlen(at)+1;
	}
	*humanSaid[humanSaidIndex] = 0;

	if (at) ++at; // skip over null separator

	while (at && *at) // read human said messages
	{
		strcpy(chatbotSaid[chatbotSaidIndex++],at);
		at += strlen(at)+1;
	}
	*chatbotSaid[chatbotSaidIndex] = 0;

	randIndex =  oldRandIndex;

	// recover loaded topic info
	memcpy(pendingTopicList,originalPendingTopicList,sizeof(unsigned int) * (originalPendingTopicIndex + 1));
	pendingTopicIndex = originalPendingTopicIndex;
}

char* WriteUserVariables(char* ptr,bool sharefile, bool compiled)
{
	if (!ptr) return NULL;
	unsigned int index = userVariableIndex;
	if (index == 0 && !compiled) 
	{
		ReportBug("No user variables in file write of %s",loginID);
		return NULL;
	}
	bool traceseen = false;
	
	if (modifiedTrace) trace = modifiedTraceVal; // script set the value
    while (index)
    {
        WORDP D = userVariableList[--index];
        if (!(D->internalBits & VAR_CHANGED) ) continue; 
		if (*D->word != USERVAR_PREFIX) ReportBug((char*)"Bad user variable to save %s\r\n",D->word)
		else if (shared && !sharefile && !strnicmp(D->word,(char*)"$share_",7)) continue;
  		else if (shared && sharefile && strnicmp(D->word,(char*)"$share_",7)) continue;
		else if ( (D->word[1] != LOCALVAR_PREFIX && D->word[1] !=  TRANSIENTVAR_PREFIX) && (D->w.userValue || (D->internalBits & MACRO_TRACE))) // transients not dumped, nor are NULL values
		{
			char word[100];
			char* val = D->w.userValue;
			if (!stricmp(D->word,"$cs_trace")) 
			{
				traceseen = true;
				sprintf(word,(char*)"%d",trace);
				val = word;
			}
			if (!val) val = ""; // for null variables being marked as traced
			if (D->internalBits & MACRO_TRACE) 
			{
				RemoveInternalFlag(D,MACRO_TRACE);
				sprintf(ptr,(char*)"%s=`%s\r\n",D->word,SafeLine(val));
			}
			else sprintf(ptr,(char*)"%s=%s\r\n",D->word,SafeLine(val));
			ptr += strlen(ptr);
			if (!compiled)
			{
				if ((unsigned int)(ptr - userDataBase) >= (userCacheSize - OVERFLOW_SAFETY_MARGIN)) return NULL;
			}
		}
        D->w.userValue = NULL;
		RemoveInternalFlag(D,VAR_CHANGED);
    }
	if (!traceseen)
	{
		sprintf(ptr,(char*)"$cs_trace=%d\r\n",trace);
		ptr += strlen(ptr);
	}

	// now put out the function tracing bits
	index = tracedFunctionsIndex;
	while (index)
    {
        WORDP D = tracedFunctionsList[--index];
		if (D->inferMark && D->internalBits & (FN_TRACE_BITS | NOTRACE_TOPIC | FN_TIME_BITS))
		{
			sprintf(ptr,(char*)"%s=%d %d\r\n",D->word,D->internalBits & (FN_TRACE_BITS | NOTRACE_TOPIC| FN_TIME_BITS),D->inferMark);
			ptr += strlen(ptr);
			D->inferMark = 0;	// turn off tracing now
		}
		D->internalBits &= -1 ^ (FN_TRACE_BITS | NOTRACE_TOPIC | FN_TIME_BITS);
	 }

	strcpy(ptr,(char*)"#`end variables\r\n");
	ptr += strlen(ptr);
	return ptr;
}

static bool ReadUserVariables()
{
	tracedFunctionsIndex = 0;
	while (ReadALine(readBuffer, 0)>= 0) //   user variables
	{
		if (*readBuffer != USERVAR_PREFIX && *readBuffer != FUNCTIONVAR_PREFIX) break; // end of variables
        char* ptr = strchr(readBuffer,'=');
        *ptr = 0; // isolate user var name from rest of buffer
		bool traceit = ptr[1] == '`';
		if (traceit)  ++ptr;
		if (*readBuffer == '$') SetUserVariable(readBuffer,RemoveEscapesWeAdded(ptr+1));
		else // tracing bits on a function
		{
			WORDP F = FindWord(readBuffer,LOWERCASE_LOOKUP);
			if (F) 
			{
				char value[MAX_WORD_SIZE];
				ptr = ReadCompiledWord(ptr+1,value);
				int bits = atoi(value); // control flags
				ptr = ReadCompiledWord(ptr,value);
				int val = atoi(value); // which tracings
				if (val || bits)
				{
					F->internalBits |= bits; // covers trace and time bits
					F->inferMark = val;		// set its trace details bits
					tracedFunctionsList[tracedFunctionsIndex++] = F;
				}
			}
		}
		if (traceit) // we are tracing this variable, turn on his flag
		{ 
			WORDP D = StoreWord(readBuffer);
			PrepareVariableChange(D,"",false); // keep it alive as long as it is traced
			D->internalBits |= MACRO_TRACE;
		}
		if (trace & TRACE_VARIABLE) Log(STDTRACELOG,(char*)"uservar: %s=%s\r\n",readBuffer,ptr+1);
    }

	if (strcmp(readBuffer,(char*)"#`end variables")) 
	{
		ReportBug((char*)"Bad variable alignment\r\n")
		return false;
	}
	return true;
}

static char* GatherUserData(char* ptr,time_t curr,bool sharefile)
{
	// need to get fact limit variable for WriteUserFacts BEFORE writing user variables, which clears them
	int count = userFactCount;
	char* value = GetUserVariable("$cs_userfactlimit");
	if (*value) count = atoi(value);

	int messageCount = MAX_USER_MESSAGES;
	value = GetUserVariable("$cs_userhistorylimit");
	if (*value) messageCount = atoi(value);

	// each line MUST end with cr/lf  so it can be made potentially safe for Mongo w/o adjusting size of data (inefficient)
	char* start = ptr;
	if (!timeturn15[1] && volleyCount >= 15 && responseIndex) sprintf(timeturn15,(char*)"%lu-%d%s",(unsigned long)curr,responseData[0].topic,responseData[0].id); // delimit time of turn 15 and location...
	sprintf(ptr,(char*)"%s %s %s %s |\r\n",saveVersion,timeturn0,timePrior,timeturn15); 
	ptr += strlen(ptr);
	*ptr = 0;
	ptr = WriteUserTopics(ptr,sharefile);
	if (!ptr)
	{
		ReportBug("User file topic data too big %s",loginID)
		return NULL;
	}
	ptr = WriteUserVariables(ptr,sharefile,false);  // json safe
	if (!ptr)
	{
		ReportBug("User file variable data too big %s",loginID)
		return NULL;
	}
	trace = 0;
	echo = false;
	if (verifyUserFacts) CheckUserFacts();	// verify they are good for now
	ptr = WriteUserFacts(ptr,sharefile,count);  // json safe
	if (!ptr)
	{
		ReportBug("User file fact data too big %s",loginID)
		return NULL;
	}
	ptr = WriteUserContext(ptr,sharefile); 
	if (!ptr)
	{
		ReportBug("User file context data too big %s",loginID)
		return NULL;
	}
	ptr = WriteRecentMessages(ptr,sharefile, messageCount); // json safe
	if (!ptr)
	{
		ReportBug("User file message data too big %s", loginID)
		return NULL;
	}
	return ptr;
}

void WriteUserData(time_t curr)
{ 
	if (!numberOfTopics)  return; //   no topics ever loaded or we are not responding
	if (!userCacheCount) return;	// never save users - no history
	
	clock_t start_time = ElapsedMilliseconds();

	if (globalDepth == 0) currentRule = NULL;
	ChangeDepth(1,(char*)"WriteUserData");

	char name[MAX_WORD_SIZE];
	char filename[SMALL_WORD_SIZE];
	// NOTE mongo does not allow . in a filename
	sprintf(name,(char*)"%s/%stopic_%s_%s.txt",users,GetUserPath(loginID),loginID,computerID);
	strcpy(filename,name);
	strcat(filename,"\r\n");
	userDataBase = FindUserCache(name); // have a buffer dedicated to him? (cant be safe with what was read in, because share involves 2 files)
	if (!userDataBase)
	{
		userDataBase = GetCacheBuffer(-1); 
		if (!userDataBase) return;		// not saving anything
		strcpy(userDataBase,filename);
	}

#ifndef DISCARDTESTING
	if (filesystemOverride == NORMALFILES && (!server || serverRetryOK) && !documentMode && !callback)  
	{
		char fname[MAX_WORD_SIZE];
		sprintf(fname,(char*)"TMP/backup-%s_%s.bin",loginID,computerID);
		CopyFile2File(fname,userDataBase,false);	// backup for debugging BUT NOT if callback of some kind...
		if (redo) // multilevel backup enabled
		{
			sprintf(fname,(char*)"TMP/backup%d-%s_%s.bin",volleyCount,loginID,computerID);
			CopyFile2File(fname,userDataBase,false);	// backup for debugging BUT NOT if callback of some kind...
		}
	}
#endif

	char* ptr = GatherUserData(userDataBase+strlen(filename),curr,false);
	if (ptr) Cache(userDataBase,ptr-userDataBase);
	if (ptr && shared)
	{
		sprintf(name,(char*)"%s/%stopic_%s_%s.txt",users,GetUserPath(loginID),loginID,(char*)"share");
		strcpy(filename,name);
		strcat(filename,"\r\n");
		userDataBase = FindUserCache(name); // have a buffer dedicated to him? (cant be safe with what was read in, because share involves 2 files)
		if (!userDataBase)
		{
			userDataBase = GetCacheBuffer(-1); // cannot fail if we got to here
			strcpy(userDataBase,filename);
		}

#ifndef DISCARDTESTING
		if (filesystemOverride == NORMALFILES &&  (!server || serverRetryOK)  && !documentMode  && !callback)  
		{
			char name[MAX_WORD_SIZE];
			sprintf(name,(char*)"TMP/backup-share-%s_%s.bin",loginID,computerID);
			CopyFile2File(name,userDataBase,false);	// backup for debugging
			if (redo)
			{
				sprintf(name,(char*)"TMP/backup%d-share-%s_%s.bin",volleyCount,loginID,computerID);
				CopyFile2File(name,userDataBase,false);	// backup for debugging BUT NOT if callback of some kind...
			}
		}
#endif
		ptr = GatherUserData(userDataBase+strlen(filename),curr,true);
		if (ptr) Cache(userDataBase,ptr-userDataBase);
	}
	userVariableIndex = 0; // flush all modified variables
	tracedFunctionsIndex = 0;
	ChangeDepth(-1,(char*)"WriteUserData");

	if (timing & TIME_USER) {
		int diff = ElapsedMilliseconds() - start_time;
		if (timing & TIME_ALWAYS || diff > 0) Log(STDTIMELOG, (char*)"Write user data time: %d ms\r\n", diff);
	}
}

static  bool ReadFileData(char* bot) // passed  buffer with file content (where feasible)
{	
	char* buffer = GetFileRead(loginID,bot);
	size_t len = 0;
	char junk[MAX_WORD_SIZE];
	*junk = 0;
	strcpy(timePrior,(char*)"0");
	strcpy(timeturn15,(char*)"0");
	strcpy(timeturn0,(char*)"0");

	// set bom
	if (buffer && *buffer != 0) // readable data
	{ 
		maxFileLine = currentFileLine = 0;
		BOM = BOMSET;
		char* at = strchr(buffer,'\r');
		if (at) // newer format
		{
			userRecordSourceBuffer = at + 2; // skip \r\n
			ReadALine(readBuffer,0);

			char* x = ReadCompiledWord(readBuffer,junk);
			x = ReadCompiledWord(x,timeturn0); // the start stamp id if there
			x = ReadCompiledWord(x,timePrior); // the prior stamp id if there
			ReadCompiledWord(x,timeturn15); // the timeturn id if there
			if (stricmp(junk,saveVersion)) *buffer = 0;// obsolete format
		}
		else // older format
		{
			userRecordSourceBuffer = buffer + strlen(buffer) + 1; // filename
			ReadALine(readBuffer,0);

			char* x = ReadCompiledWord(readBuffer,junk);
			x = ReadCompiledWord(x,timeturn0); // the start stamp id if there
			x = ReadCompiledWord(x,timePrior); // the prior stamp id if there
			ReadCompiledWord(x,timeturn15); // the timeturn id if there
		}
	}
    if (!buffer || !*buffer) 
	{
		// if shared file exists, we dont have to kill it. If one does exist, we merely want to use it to add to existing bots
		ReadNewUser();
		strcpy(timeturn0,GetMyTime(time(0))); // startup time
	}
	else
	{
		if (trace & TRACE_USER) Log(STDTRACELOG,(char*)"loading user\r\n");
		if (!ReadUserTopics()) 
		{
			ReportBug((char*)"User data file TOPICS inconsistent\r\n");
			return false;
		}
		if (!ReadUserVariables()) 
		{
			ReportBug((char*)"User data file VARIABLES inconsistent\r\n");
			return false;
		}
		if (!ReadUserFacts()) 
		{
			ReportBug((char*)"User data file FACTS inconsistent\r\n");
			return false;
		}
		if (!ReadUserContext()) 
		{
			ReportBug((char*)"User data file CONTEXT inconsistent\r\n");
			return false;
		}
		if (!ReadRecentMessages()) 
		{
			ReportBug((char*)"User data file MESSAGES inconsistent\r\n");
			return false;
		}
		if (trace & TRACE_USER) Log(STDTRACELOG,(char*)"user load completed normally\r\n");
		oldRandIndex = randIndex = atoi(GetUserVariable((char*)"$cs_randindex")) + (volleyCount % MAXRAND);	// rand base assigned to user
	}
	userRecordSourceBuffer = NULL;
	return true;
}

void ReadUserData() // passed  buffer with file content (where feasible)
{	
	if (globalDepth == 0) currentRule = NULL;
	ChangeDepth(1,(char*)"ReadUserData");

	clock_t start_time = ElapsedMilliseconds();

	// std defaults
	tokenControl = (DO_SUBSTITUTE_SYSTEM | DO_INTERJECTION_SPLITTING | DO_PROPERNAME_MERGE | DO_NUMBER_MERGE | DO_SPELLCHECK | DO_PARSE );
	responseControl = ALL_RESPONSES;
	*wildcardSeparator = ' ';

	ResetUserChat();
	if (!ReadFileData(computerID))// read user file, if any, or get it from cache
	{
		printf((char*)"%s",(char*)"User data file inconsistent\r\n");
		ReportBug((char*)"User data file inconsistent\r\n");
	}
	if (shared) ReadFileData((char*)"share");  // read shared file, if any, or get it from cache
	ChangeDepth(-1,(char*)"ReadUserData");

	if (timing & TIME_USER) {
		int diff = ElapsedMilliseconds() - start_time;
		if (timing & TIME_ALWAYS || diff > 0) Log(STDTIMELOG, (char*)"Read user data time: %d ms\r\n", diff);
	}
}

void KillShare()
{
	if (shared) 
	{
		char buffer[MAX_WORD_SIZE];
		sprintf(buffer,(char*)"%s/%stopic_%s_%s.txt",users,GetUserPath(loginID),loginID,(char*)"share");
		unlink(buffer); // remove all shared data of this user
	}
}

void ReadNewUser()
{
	if (trace & TRACE_USER) Log(STDTRACELOG,(char*)"New User\r\n");
	ResetUserChat();
	ClearUserVariables();
	ClearUserFacts();
	ResetTopicSystem(true);

	userFirstLine = 1;
	volleyCount = 0;
	// std defaults
	tokenControl = (DO_SUBSTITUTE_SYSTEM | DO_INTERJECTION_SPLITTING | DO_PROPERNAME_MERGE | DO_NUMBER_MERGE | DO_SPELLCHECK | DO_PARSE );
	responseControl = ALL_RESPONSES;
	*wildcardSeparator = ' ';

	//   set his random seed
	bool hasUpperCharacters;
	bool hasUTF8Characters;
	unsigned int rand = (unsigned int) Hashit((unsigned char *) loginID,strlen(loginID),hasUpperCharacters,hasUTF8Characters);
	char word[MAX_WORD_SIZE];
	oldRandIndex = randIndex = rand & 4095;
    sprintf(word,(char*)"%d",randIndex);
	SetUserVariable((char*)"$cs_randindex",word ); 
	strcpy(word,computerID);
	*word = GetUppercaseData(*word);
	SetUserVariable((char*)"$cs_bot",word ); 
	SetUserVariable((char*)"$cs_login",loginName);

	sprintf(readBuffer,(char*)"^%s",computerID);
	WORDP D = FindWord(readBuffer,0,LOWERCASE_LOOKUP);
	if (!D) // no such bot
	{
		*computerID = 0;
		return;
	}

	char* buffer = AllocateBuffer();
	*buffer = 0;
	PushOutputBuffers();
	currentRuleOutputBase = currentOutputBase = buffer;
	ChangeDepth(1,(char*)"ReadNewUser");
	FunctionResult result;
	char arg[15]; // dofunction call needs to be alterable if tracing
	strcpy(arg,(char*)"()");
	DoFunction(D->word,arg,buffer,result);
	PopOutputBuffers();
	ChangeDepth(-1,(char*)"ReadNewUser");
	FreeBuffer();

	inputRejoinderTopic = inputRejoinderRuleID = NO_REJOINDER; 
}
