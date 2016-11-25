#include "common.h"


#ifdef INFORMATION

Function calls all run through DoCommand().
	
A function call can either be to a system routine or a user routine. 
	
User routines are like C macros, executed in the context of the caller, so the argument 
are never evaluated prior to the call. If you evaluated an argument during the mustering,
you could get bad answers. Consider:
	One has a function: ^foo(^arg1 ^arg2)  ^arg2 ^arg1
	And one has a call ^foo(($val = 1 ) $val )
This SHOULD look like inline code:  $val  $val = 1 
But evaluation at argument time would alter the value of $val and pass THAT as ^arg2. Wrong.

The calling Arguments to a user function are in an array, whose base starts at callArgumentBase and runs
up to (non-inclusive) callArgumentIndex.

System routines are proper functions, whose callArgumentList may or may not be evaluated. 
The callArgumentList are in an array, whose base starts at index CallingArgumentBase and runs
up to (non-inclusive) CallingArgumentIndex. The description of a system routine tells
how many callArgumentList it expects and in what way. Routines that set variables always pass
that designator as the first (unevaluated) argument and all the rest are evaluated callArgumentList.

The following argument passing is supported
	1. Evaluated - each argument is evaluated and stored (except for a storage argument). 
		If the routine takes optional callArgumentList these are already also evaluated and stored, 
		and the argument after the last actual argument is a null string.
	2. STREAM_ARG - the entire argument stream is passed unevaled as a single argument,
		allowing the routine to handle processing them itself.

All calls have a context of "executingBase" which is the start of the rule causing this 
evaluation. All calls are passed a "buffer" which is spot in the currentOutputBase it
should write any answers.

Anytime a single argument is expected, one can pass a whole slew of them by making
them into a stream, encasing them with ().  The parens will be stripped and the
entire mess passed unevaluated. This makes it analogous to STREAM_ARG, but the latter
requires no excess parens to delimit it.

In general, the system does not test result codes on argument evaluations. So
issuing a FAILRULE or such has no effect there.

#endif

#define SIZELIM 200
#define MAX_TOPIC_KEYS 5000
#define PLANMARK -1
#define RULEMARK -2

#define MAX_LOG_NAMES 4

static char* functionAnswerBase = NULL;
static char* functionAnswerName = NULL;

char lognames[MAX_LOG_NAMES][200];	
FILE* logfiles[4];

bool planning = false;

#define MAX_REUSE_SAFETY 100
static int reuseIndex = 0;
static char* reuseSafety[MAX_REUSE_SAFETY+1];
static int reuseSafetyCount[MAX_REUSE_SAFETY+1];

static char* months[] = { (char*)"January",(char*)"February",(char*)"March",(char*)"April",(char*)"May",(char*)"June",(char*)"July",(char*)"August",(char*)"September",(char*)"October",(char*)"November",(char*)"December"};
static char* days[] = { (char*)"Sunday",(char*)"Monday",(char*)"Tuesday",(char*)"Wednesday",(char*)"Thursday",(char*)"Friday",(char*)"Saturday"};
long http_response = 0;
int globalDepth = 0;
char* stringPlanBase = 0;
char* backtrackPoint = 0;		// plan code backtrace data
unsigned int currentIterator = 0;		// next value of iterator

//   spot callArgumentList are stored for  function calls
char* callArgumentList[MAX_ARGUMENT_COUNT+1];    // arguments to functions
unsigned int callArgumentBases[MAX_CALL_DEPTH];    // arguments to functions
WORDP callStack[MAX_CALL_DEPTH];
unsigned int callIndex = 0;
unsigned int callArgumentIndex;
unsigned int callArgumentBase;
unsigned int fnVarBase;
bool backtrackable = false;

char lastInputSubstitution[INPUT_BUFFER_SIZE];
TestMode wasCommand; // special result passed back from some commands to control chatscript

static char oldunmarked[MAX_SENTENCE_LENGTH];
static unsigned int spellSet;			// place to store word-facts on words spelled per a pattern
char* currentPlanBuffer;

static int rhymeSet;
//////////////////////////////////////////////////////////
/// BASIC FUNCTION CODE
//////////////////////////////////////////////////////////

void InitFunctionSystem() // register all functions
{
	unsigned int k = 0;
	SystemFunctionInfo *fn;
	while ((fn = &systemFunctionSet[++k]) && fn->word)
	{
		if (*fn->word == '^' ) // not a header
		{
			WORDP D = StoreWord((char*) fn->word,0);
			AddInternalFlag(D,FUNCTION_NAME);
			D->x.codeIndex = (unsigned short)k;
		}
	}

	for (unsigned int i = 0; i < MAX_LOG_NAMES; ++i) 
	{
		lognames[i][0] = 0;
		logfiles[i] = NULL;
	}

	oldunmarked[0] = 0;	// global unmarking has nothing
}

char* InitDisplay(char* list)
{
	char word[MAX_WORD_SIZE];
	list += 2;	// skip ( and space
	while (1)
	{
		list = ReadCompiledWord((char*)list,word); 
		if (*word == ')') break;  // end of display is signaled by )
		if (*word == USERVAR_PREFIX && !AllocateInverseSlot(word)) return 0; // leaves display hanging badly BUG
	}
	return list;
}

void RestoreDisplay(char* base, char* list)
{
	char word[MAX_WORD_SIZE];
	list += 2;	// skip ( and space
	char* slot = base; // display table starts here
	while (1)
	{
		list = ReadCompiledWord(list,word); 
		if (*word == ')') break;
		if (*word == USERVAR_PREFIX) slot = RestoreInverseSlot(word,slot);
	}
}

void ResetFunctionSystem()
{
	//   reset function call data
	fnVarBase = callArgumentBase = callArgumentIndex = 0;
}

#ifdef WIN32
#define MAKEWORDX(a, b)      ((unsigned short)(((BYTE)(((DWORD_PTR)(a)) & 0xff)) | ((unsigned short)((BYTE)(((DWORD_PTR)(b)) & 0xff))) << 8))
FunctionResult InitWinsock()
{
	static bool first = true;
	if (first) // prevent DB close from closing WSAStartup to improve performance
	{
		first = false;
		WSADATA wsaData;
		unsigned short wVersionRequested = MAKEWORDX(2, 0);              //   Request WinSock v2.0
		if (WSAStartup(wVersionRequested, &wsaData) != 0) 
		{
			if (trace & TRACE_SQL && CheckTopicTrace()) Log(STDTRACELOG, "WSAStartup failed\r\n");
			return FAILRULE_BIT;
		}
	}
	return NOPROBLEM_BIT;
}
#endif

static char* GetPossibleFunctionArgument(char* arg, char* word)
{
	char* ptr = ReadCompiledWord(arg,word);
	if (*word == '^' && IsDigit(word[1])) 
	{
		char* value = callArgumentList[atoi(word+1)+fnVarBase];
		if (*value == LCLVARDATA_PREFIX && value[1] == LCLVARDATA_PREFIX) 
			value += 2; // already evaled data  -- but bug remains
		strcpy(word,value);
	}
	// this gets us what was passed by name, $xxx.hi will be a json ref.
	return ptr;
}

FunctionResult JavascriptArgEval(unsigned int index, char* buffer)
{
	FunctionResult result;
	char* arg = ARGUMENT(index);
	ReadCommandArg(arg,buffer,result,OUTPUT_NOTREALBUFFER|OUTPUT_EVALCODE|OUTPUT_UNTOUCHEDSTRING,maxBufferSize);
	return result;
}

char* SaveBacktrack(int id)
{
	// save: id, oldbacktrack point, currentfact, current dict,   
	char* mark = AllocateString(NULL,4,sizeof(int),false); 
	if (!mark) return NULL;
	int* i = (int*) mark;
	i[0] = id;										// 1st int is a backtrack label - plan (-1) or rule (other)
	i[1] = (int)(stringPlanBase - backtrackPoint);	// 2nd is old backtrack point value
	i[2] = Fact2Index(factFree);					// 4th is fact base 
	i[3] = Word2Index(dictionaryFree);				// 5th is word base (this entry is NOT used)
	return backtrackPoint = mark;
}

static char* FlushMark() // throw away this backtrack point, maybe reclaim its string space
{
	if (!backtrackPoint) return NULL;
	// we are keeping facts and variable changes, so we cannot reassign the string free space back because it may be in use.
	if (backtrackPoint == stringFree) stringFree = backtrackPoint + (4 * sizeof(int));
	int* i = (int*) backtrackPoint;
	return backtrackPoint = stringPlanBase - i[1];
}

static void RestoreMark()
{	// undo all changes
	if (!backtrackPoint) return;
	int* i = ((int*) backtrackPoint); // skip id

	// revert facts
	FACT* oldF = Index2Fact(i[2]);
	while (factFree > oldF) FreeFact(factFree--); // undo facts to start
	// revert dict entries
	WORDP oldD = Index2Word(i[3]);
	
	// trim dead facts at ends of sets
	for (unsigned int store = 0; store <= MAX_FIND_SETS; ++store)
	{
		unsigned int count = FACTSET_COUNT(store) + 1;
		while (--count >= 1)
		{
			if (!(factSet[store][count]->flags & FACTDEAD)) break; // stop having found a live fact
		}
		if (count) SET_FACTSET_COUNT(store,count); // new end
	}
	DictionaryRelease(oldD,backtrackPoint);
	backtrackPoint = stringPlanBase - i[1];
}

void RefreshMark()
{	// undo all changes but leave rule mark in place
	if (!backtrackPoint) return;
	int* i = (int*) backtrackPoint; // point past id, backtrack 
	
	// revert facts
	FACT* oldF = Index2Fact(i[2]);
	while (factFree > oldF) FreeFact(factFree--); // undo facts to start
	// revert dict entries
	WORDP oldD = Index2Word(i[3]);

	// trim dead facts at ends of sets
	for (unsigned int store = 0; store <= MAX_FIND_SETS; ++store)
	{
		unsigned int count = FACTSET_COUNT(store) + 1;
		while (--count >= 1)
		{
			if (!(factSet[store][count]->flags & FACTDEAD)) break; // stop having found a live fact
		}
		if (count) SET_FACTSET_COUNT(store,count); // new end
	}

	DictionaryRelease(oldD,backtrackPoint);
}

static void UpdatePlanBuffer()
{
	size_t len = strlen(currentPlanBuffer);
	if (len) // we have output, prep next output
	{
		currentPlanBuffer += len;	// cumulative output into buffer
		*++currentPlanBuffer = ' '; // add a space
		currentPlanBuffer[1] = 0;
	}
}

static int WildStartPosition(char* arg)
{
	int x = GetWildcardID(arg);
	if (x == ILLEGAL_MATCHVARIABLE) return ILLEGAL_MATCHVARIABLE;
	int n = WILDCARD_START(wildcardPosition[x]);
	if (n == 0 || n > wordCount) n = atoi(wildcardCanonicalText[x]);
	if (n == 0 || n > wordCount) n = 1;
	return n;
}

static int WildEndPosition(char* arg)
{
	int x = GetWildcardID(arg);
	if (x == ILLEGAL_MATCHVARIABLE) return ILLEGAL_MATCHVARIABLE;
	int n = WILDCARD_END(wildcardPosition[x]);
	if (n == 0 || n > wordCount) n = atoi(wildcardCanonicalText[x]);
	if (n == 0 || n > wordCount) n = 1;
	return n;
}

static FunctionResult PlanCode(WORDP plan, char* buffer)
{  // failing to find a responder is not failure.
#ifdef INFORMATION

	A plan sets a recover point for backtracking and clears it one way or another when it exits.
	A rule sets a backpoint only if it finds some place to backtrack. The rule will clear that point one way or another when it finishes.

	Undoable changes to variables are handled by creating special facts. 
#endif

	if (trace & (TRACE_MATCH|TRACE_PATTERN) && CheckTopicTrace()) Log(STDTRACELOG,(char*)"\r\n\r\nPlan: %s ",plan->word);
	WORDP originalPlan = plan;
	bool oldplan = planning;
	bool oldbacktrackable = backtrackable;
	char* oldbacktrackPoint = backtrackPoint;
	char* oldStringPlanBase = stringPlanBase;
	stringPlanBase = stringFree;
	backtrackPoint = stringFree;
	backtrackable = false;
	unsigned int oldWithinLoop = withinLoop;
	withinLoop = 0;
	planning = true;
	int holdd = globalDepth;
	char* oldCurrentPlanBuffer = currentPlanBuffer;

	unsigned int tindex = topicIndex;
    FunctionResult result = NOPROBLEM_BIT;

	SAVEOLDCONTEXT()

	ChangeDepth(1,originalPlan->word); // plancode

	// where future plans will increment naming
	char name[MAX_WORD_SIZE];
	strcpy(name,plan->word);
	char* end = name + plan->length;
	*end = '.';
	*++end = 0;

	unsigned int n = 0;
	while (result == NOPROBLEM_BIT) // loop on plans to use
	{
		*buffer = 0;
		currentPlanBuffer = buffer;	  // where we are in buffer writing across rules of a plan
		int topic = plan->x.topicIndex;
		if (!topic)  
		{
			result = FAILRULE_BIT; 
			break;
		}
		int pushed =  PushTopic(topic);  // sets currentTopicID
		if (pushed < 0) 
		{
			result = FAILRULE_BIT; 
			break;
		}
		char* xxplanMark = SaveBacktrack(PLANMARK); // base of changes the plan has made
		char* base = GetTopicData(topic); 
		int ruleID = 0;
		currentRuleTopic = currentTopicID;
		currentRule = base;
		currentRuleID = ruleID;
		char* ruleMark = NULL;

		bool fail = false;
		
		ChangeDepth(1,GetTopicName(currentTopicID)); // plancode
		unsigned int oldTopicDisplay = currentTopicDisplay;
		currentTopicDisplay = currentTopicID;
		char* locals = GetTopicLocals(currentTopicID);
		if (locals && currentTopicDisplay != oldTopicDisplay && !InitDisplay(locals)) fail = true;

		while (!fail && base && *base ) //   loop on rules of topic
		{
			currentRule = base;
			ruleMark = SaveBacktrack(RULEMARK); // allows rule to be completely undone if it fails
			backtrackable = false;
			result = TestRule(ruleID,base,currentPlanBuffer); // do rule at base
			if (!result || (result & ENDTOPIC_BIT)) // rule didnt fail
			{
				UpdatePlanBuffer();	// keep any results
				if (result & ENDTOPIC_BIT) break; // no more rules are needed
			}
			else if (backtrackable)  // rule failed 
			{
				while (backtrackable)
				{
					if (trace & (TRACE_MATCH|TRACE_PATTERN) && CheckTopicTrace()) Log(STDTRACETABLOG,(char*)"Backtrack \r\n");
					*currentPlanBuffer = 0;
					RefreshMark(); // undo all of rule, but leave undo marker in place
					backtrackable = false;
					result = DoOutput(currentPlanBuffer,currentRule,currentRuleID); // redo the rule per normal
					if (!result || result & ENDTOPIC_BIT) break; // rule didnt fail
				}
				if (result & ENDTOPIC_BIT) break; // rule succeeded eventually
			}
			FlushMark();  // cannot revert changes after this
			base = FindNextRule(NEXTTOPLEVEL,base,ruleID);
		}

		if (locals && currentTopicDisplay != oldTopicDisplay) RestoreDisplay(inverseStringDepth[globalDepth],locals);
		currentTopicDisplay = oldTopicDisplay;
		ChangeDepth(-1,GetTopicName(currentTopicDisplay)); // plan

		if (backtrackPoint == ruleMark) FlushMark(); // discard rule undo
		if (trace & (TRACE_MATCH|TRACE_PATTERN) && CheckTopicTrace()) 
		{
			char* xname = GetTopicName(currentTopicID);
			if (*xname == '^') Log(STDTRACETABLOG,(char*)"Result: %s Plan: %s \r\n",ResultCode(result),name);
			else Log(STDTRACETABLOG,(char*)"Result: %s Topic: %s \r\n",ResultCode(result),xname);
		}
		if (pushed) PopTopic();
		if (result & ENDTOPIC_BIT) 
		{
			FlushMark(); // drop our access to this space, we are as done as we can get on this rule
			break;	// we SUCCEEDED, the plan is done
		}
		//   flush any deeper stack back to spot we started
		if (result & FAILCODES) topicIndex = tindex; 
		//   or remove topics we matched on so we become the new master path
		RestoreMark(); // undo failed plan
		sprintf(end,(char*)"%d",++n);
		plan = FindWord(name);
		result =  (!plan) ? FAILRULE_BIT : NOPROBLEM_BIT;
		if (!result && trace & (TRACE_MATCH|TRACE_PATTERN) && CheckTopicTrace()) Log(STDTRACETABLOG,(char*)"NextPlan %s\r\n",name);
	}
	RESTOREOLDCONTEXT()

	if (globalDepth != holdd) ReportBug((char*)"PlanCode didn't balance");
	ChangeDepth(-1,originalPlan->word); // plancode
	
	if (*currentPlanBuffer == ' ') *currentPlanBuffer = 0; // remove trailing space

	// revert to callers environment
	planning = oldplan;
	currentPlanBuffer = oldCurrentPlanBuffer;
	withinLoop = oldWithinLoop;
	backtrackable = oldbacktrackable;
	stringPlanBase = oldStringPlanBase;
	backtrackPoint = oldbacktrackPoint;
	result = (FunctionResult)(result & (-1 ^ (ENDTOPIC_BIT|ENDRULE_BIT)));
	return result; // these are swallowed
}

char* DoFunction(char* name,char* ptr,char* buffer,FunctionResult &result) // DoCall(
{
	WORDP D = FindWord(name,0,LOWERCASE_LOOKUP);
	 if (!D || !(D->internalBits & FUNCTION_NAME))
    {
		result = UNDEFINED_FUNCTION;
		return ptr; 
	}
	int oldimpliedIf = impliedIf;
	*buffer = 0;
	if (stricmp(name,"^substitute")) impliedIf = ALREADY_HANDLED;	// we all allow immediate if context to pass thru here safely
	result = NOPROBLEM_BIT;
	ptr = SkipWhitespace(ptr);
	if (*ptr != '(') // should have been
	{
		result = FAILRULE_BIT;
		return ptr;
	}
	if (timerLimit) // check for violating time restriction
	{
		if (timerCheckInstance == TIMEOUT_INSTANCE) 
		{
			result = FAILINPUT_BIT;
			return ptr;	// force it to fail all the time
		}
		if (++timerCheckInstance >= timerCheckRate)
		{
			timerCheckInstance = 0;
			if ((ElapsedMilliseconds() - volleyStartTime) >= timerLimit) 
			{
				result = FAILINPUT_BIT; // time out NOW
				timerCheckInstance = TIMEOUT_INSTANCE;	// force it to fail all the time
				return ptr;
			}
		}
	}
	char* paren = ptr;
	ptr = SkipWhitespace(ptr+1); // aim to next major thing after ( 
	SystemFunctionInfo* info = NULL;
	unsigned int oldArgumentBase = callArgumentBase;
	unsigned int oldArgumentIndex = callArgumentIndex;
	unsigned char* definition = NULL;
	unsigned int j = 0;
	char* oldFunctionAnswerBase = functionAnswerBase;
	char* oldFunctionAnswerName = functionAnswerName;
	unsigned int oldFnVarBase = fnVarBase;
	clock_t start_time = ElapsedMilliseconds();
	unsigned int firstArgument = callArgumentIndex;
	ChangeDepth(1,D->word); // HandleSystemCall
	if (D->x.codeIndex && !(D->internalBits & (IS_PLAN_MACRO|IS_TABLE_MACRO))) // system function --  macroFlags are also on codeindex, but IS_TABLE_MACRO distinguishes  but PLAN also has a topicindex which is a codeindex
	{
		callArgumentBase = callArgumentIndex - 1;
		info = &systemFunctionSet[D->x.codeIndex];
		char* start = ptr;
		int flags = 0x00000100;	// do we leave this unevaled?
		while (ptr && *ptr != ')' && *ptr != ENDUNIT) // read arguments
		{ 
			if (info->argumentCount != STREAM_ARG) // break them up
			{
				char* buffer = AllocateBuffer();
				if (info->argumentCount == UNEVALED) ptr = ReadCompiledWordOrCall(ptr,buffer);
				// unevaled counted arg
				else if (info->argumentCount != VARIABLE_ARG_COUNT && info->argumentCount & (flags << j)) 
					ptr = ReadCompiledWord(ptr,buffer);
				else // VARIABLE ARG OR COUNTED ARG
				{
					ptr = ReadCommandArg(ptr,buffer,result,OUTPUT_NOTREALBUFFER|OUTPUT_EVALCODE|OUTPUT_UNTOUCHEDSTRING,maxBufferSize);
				}
				callArgumentList[callArgumentIndex] = AllocateInverseString(buffer);
				ptr = SkipWhitespace(ptr);
				FreeBuffer();
				if (callArgumentList[callArgumentIndex] && callArgumentList[callArgumentIndex][0] == USERVAR_PREFIX &&
					strstr(callArgumentList[callArgumentIndex],"$_")) // NOT by reference but by value somewhere in the chain
				{
					callArgumentList[callArgumentIndex] = AllocateInverseString(GetUserVariable(callArgumentList[callArgumentIndex])); // pass by unmarked value - no one will try to store thru it
				}
				++j;
			}
			else // swallow unevaled arg stream
			{
				ptr = BalanceParen(paren,false,false);  // start after (, point after closing ) if one can, to next token - it may point 2 after )  or it may point 1 after )
				while (*--ptr != ')'){;} // back up to closing
				size_t len = ptr++ - start; // length of argument bytes not including paren, and end up after paren
				while (start[len-1] == ' ') --len; // dont want trailing blanks
				callArgumentList[callArgumentIndex] = AllocateInverseString(start,len);
			}
			if (!callArgumentList[callArgumentIndex])
			{
				ReportBug("Inverse String space exhausted %s",D->word);
				result = FAILRULE_BIT;
				goto TERMINATE;
			}

			if (++callArgumentIndex >= MAX_ARG_LIST) 
			{
				ReportBug("Globally too many arguments %s",D->word);
				result = FAILRULE_BIT;
				goto TERMINATE;
			}
			if (info->argumentCount == STREAM_ARG) break; // end of arguments
			ptr = SkipWhitespace(ptr);
		}
		callArgumentList[callArgumentIndex] = (char*) ""; 
		callArgumentList[callArgumentIndex+1] = (char*) ""; // optional arguments excess
		callArgumentList[callArgumentIndex+2] = (char*) ""; // optional arguments excess
		callArgumentList[callArgumentIndex+3] = (char*) ""; 
		callArgumentList[callArgumentIndex+4] = (char*) ""; // optional arguments excess
		callArgumentList[callArgumentIndex+5] = (char*) ""; // optional arguments excess
		if ((trace & (TRACE_OUTPUT|TRACE_USERFN) || D->internalBits & MACRO_TRACE) && !(D->internalBits & NOTRACE_FN) && CheckTopicTrace()) 
		{
			--globalDepth; // patch depth because call data should be outside of depth
			Log(STDTRACETABLOG,(char*) "System call %s(",D->word);
			for (unsigned int i = firstArgument; i < callArgumentIndex; ++i)
			{	
				char c = callArgumentList[i][40];
				callArgumentList[i][40] = 0;
				Log(STDTRACELOG, (char*) "%s",callArgumentList[i]);
				if (i < (callArgumentIndex - 1)) Log(STDTRACELOG, (char*)",");
				callArgumentList[i][40] = c;
			}
			Log(STDTRACELOG, ")\r\n");
			++globalDepth;
		}

		if (result & ENDCODES); // failed during argument processing
		else result = (*info->fn)(buffer);
	} 
	else //   user function (plan macro, inputmacro, outputmacro, tablemacro)) , eg  ^call (_10 ^2 it ^call2 (3 ) )  spot each token has 1 space separator 
	{
		functionAnswerBase = buffer; // let ^return be transparent by only tracking user routines for this data
		functionAnswerName = D->word;
		callArgumentBases[callIndex] = callArgumentIndex - 1; // call stack
		callStack[callIndex++] = D;

		unsigned int args = 0;
		if ((D->internalBits & FUNCTION_BITS) == IS_PLAN_MACRO) 
		{
			definition = NULL; 
			args = D->w.planArgCount;
		}
		else if (!D->w.fndefinition)
		{
			ReportBug((char*)"Missing function definition for %s\r\n",D->word);
			result = FAILRULE_BIT;
		}
		else 
		{
			definition = D->w.fndefinition + 1;
			args = MACRO_ARGUMENT_COUNT(D); // expected args
		}

		// now process arguments
		unsigned int argflags =  D->x.macroFlags;
		char* startRawArg = ptr;
        while (definition && ptr && *ptr && *ptr != ')') //   ptr is after opening (and before an arg but may have white space
        {
			char* arg = AllocateBuffer();
				
			if (currentRule == NULL) //   this is a table function- DONT EVAL ITS ARGUMENTS AND... keep quoted item intact
			{
				ptr = ReadCompiledWord(ptr,arg); // return dq args as is
#ifndef DISCARDSCRIPTCOMPILER
				if (compiling && ptr == NULL) BADSCRIPT((char*)"TABLE-11 Arguments to %s ran out",name)
#endif
			}
			else 
			{
				bool stripQuotes =  (argflags & ( 1 << j)) ? 1 : 0; // want to use quotes 
				// arguments to user functions are not evaluated, they will be used, in place, in the function.
				// EXCEPT evaluation of ^arguments must be immediate to maintain current context- both ^arg and ^"xxx" stuff
				ptr = ReadArgument(ptr,arg); //   ptr returns on next significant char
				if (*arg == '"' && stripQuotes)
				{
					size_t len = strlen(arg);
					if (arg[len-1] == '"') 
					{
						arg[len-1] = 0;
						memmove(arg,arg+1,strlen(arg));
					}
					// and purify internal \" to simple quote
					char* x = arg;
					while ((x = strchr(x,'\\')))
					{
						if (x[1] == '"') memmove(x,x+1,strlen(x)); // remove 
					}
				}
			}
			if (*arg == 0) strcpy(arg,(char*)"null");// no argument found - caller had null data in argument

			//   within a function, seeing function argument as an argument 
			//   switch to incoming arg now, later callArgumentBase will be wrong
			if (*arg == '^' && IsDigit(arg[1]) ) strcpy(arg,callArgumentList[atoi(arg+1) + fnVarBase]); 
			if (*arg == '"' && arg[1] == ENDUNIT) // internal special quoted item, remove markers.
			{
				size_t len = strlen(arg);
				if (arg[len-2] == ENDUNIT)
				{
					arg[len-2] = 0;
					memmove(arg,arg+2,len-1);
				}
			}
			if (*arg == FUNCTIONSTRING && (arg[1] == '"' || arg[1] == '\''))
			{
				AllocateOutputBuffer();
				ReformatString(arg[1],arg+2,currentOutputBase,result,0);
				strcpy(arg,currentOutputBase);
				FreeOutputBuffer();
			}
			if (arg[0] == USERVAR_PREFIX && strstr(arg,"$_"))  strcpy(arg,GetUserVariable(arg)-2); // NOT by reference but by marked value
				
			if (!stricmp(arg,(char*)"null")) *arg = 0;	 // pass NOTHING as the value

			callArgumentList[callArgumentIndex++] = AllocateInverseString(arg);
			FreeBuffer();
			if (!callArgumentList[callArgumentIndex-1])
			{
				if (callIndex) --callIndex;
				result = FAILRULE_BIT;
				FreeBuffer();
				goto TERMINATE;
			}
			if (callArgumentIndex >= MAX_ARGUMENT_COUNT) --callArgumentIndex; // force lock to fail but swallow all args to update ptr
			++j;
			ptr = SkipWhitespace(ptr); // yevs pattern doesnt space right
		} // end of argument processing

		// handle any display variables
		char* basedisplay = 0;
		char* baseinvert = 0;
		if ((D->internalBits & FUNCTION_BITS) != IS_PLAN_MACRO && definition && definition[0] == '(')
		{
			basedisplay = (char*)(definition);
			baseinvert = stringInverseFree; // here is where we allocate values
			definition = (unsigned char*) InitDisplay((char*)definition); // will return 0 if runs out of string space
			if (!definition) result = FAILRULE_BIT;
		}
	
		// now do defaulted null arguments
		while (definition && (callArgumentIndex - oldArgumentIndex) < args) // fill in defaulted args to null
		{
			callArgumentList[callArgumentIndex++] = AllocateInverseString("",0);
		}

		// now if args are local vars instead of ^, set them up (we have protected them by now)
		char var[MAX_WORD_SIZE];
		char* list = (basedisplay) ? (basedisplay + 2) : NULL;	// skip ( and space xxxx
		if (list) for (unsigned int i = oldArgumentIndex; i < callArgumentIndex; ++i)
		{
			list = ReadCompiledWord(list,var); 
			if (*var == USERVAR_PREFIX) 
			{
				WORDP arg = FindWord(var);
				if (!arg) continue;	// should never happen
				char* val = callArgumentList[i]; // constants wont have `` in front of them
				if (*val == USERVAR_PREFIX) val = GetUserVariable(val);
				else if (val[0] == ENDUNIT && val[1] == ENDUNIT) val += 2; // skip over noeval marker
				else if (val[0] == '\'' && val[1] == '_' && IsDigit(val[2]))
				{
					int id = GetWildcardID(val+1);
					if (id >= 0) val = AllocateInverseString(wildcardCanonicalText[id],0,true);
					else val = AllocateInverseString("");
				}
				else if (val[0] == '_' && IsDigit(val[1]))
				{
					int id = GetWildcardID(val);
					if (id >= 0) val = AllocateInverseString(wildcardOriginalText[id],0,true);
					else val = AllocateInverseString("");
				}
				if (*val && *(val-2) != '`' && *(val-1) != '`') 
					val = AllocateInverseString(val,0,true);
				arg->w.userValue = val;
			}
		}

		if ((trace & (TRACE_OUTPUT|TRACE_USERFN) || D->internalBits & MACRO_TRACE) && !(D->internalBits & NOTRACE_FN) && CheckTopicTrace()) 
		{
			--globalDepth; // patch depth because call data should be outside of depth
			char a = *ptr;
			*ptr = 0;
			Log(STDTRACETABLOG,(char*) "User call %s(%s):(",D->word,startRawArg);
			char* buf = AllocateBuffer();
			for (unsigned int i = firstArgument; i < callArgumentIndex; ++i)
			{	
				char* x = callArgumentList[i];
				if (*x == USERVAR_PREFIX) 
				{
					strncpy(buf,GetUserVariable(x,false,true),40);
					x = buf;
				}
				else if (*x == '_' && IsDigit(x[1])) 
				{
					int id = GetWildcardID(x);
					if (id >= 0) 
					{
						strncpy(buf,wildcardOriginalText[id],40);
						x = buf;
					}
				}
				else if (*x == '\'' && x[1] == '_' && IsDigit(x[2])) 
				{
					int id = GetWildcardID(x+1);
					if (id >= 0) 
					{
						strncpy(buf,wildcardCanonicalText[id],40);
						x = buf;
					}
				}
				else if (*x == LCLVARDATA_PREFIX) x += 2; // skip ``

				// limited to 40, provide ... and restore
				char d[4];
				d[0] = x[40];
				d[1] = x[41];
				d[2] = x[42];
				d[3] = x[43];
				strcpy(x+40,"...");
				Log(STDTRACELOG, (char*) "%s",x);
				x[40] = d[0];
				x[41] = d[1];
				x[42] = d[2];
				x[43] = d[3];
				if (i < (callArgumentIndex - 1)) Log(STDTRACELOG, (char*)", ");
			}
			FreeBuffer();
			Log(STDTRACELOG, ")\r\n");
			Log(STDTRACETABLOG,(char*)"");
			*ptr = a;
			++globalDepth;
		}

		fnVarBase = callArgumentBase = oldArgumentIndex; 
	
		//   run the definition
		unsigned int oldtrace = trace;
		if (D->internalBits & MACRO_TRACE && !(D->internalBits & NOTRACE_FN)) 
		{
			trace = (unsigned int) D->inferMark;
			if (oldtrace && !(oldtrace & TRACE_ECHO)) trace ^= TRACE_ECHO;
		}
		else if (D->internalBits & NOTRACE_FN) trace = 0;
		if (result & ENDCODES){;}
		else if (callArgumentIndex >= (MAX_ARGUMENT_COUNT-1)) 	// pinned max (though we could legally arrive by accident on this last one)
		{
			// undo any display variables
			if (definition && basedisplay) RestoreDisplay(baseinvert,basedisplay);
			ReportBug((char*)"User function nesting too deep %d",MAX_ARGUMENT_COUNT);
			result = FAILRULE_BIT;
		}
		else if (definition && (D->internalBits & FUNCTION_BITS) == IS_PLAN_MACRO) 
			result = PlanCode(D,buffer); // run a plan
#ifndef DISCARDJAVASCRIPT
		else if (definition && *definition == '*' && !strncmp((char*)definition,"*JavaScript",11))
		{
			result = RunJavaScript((char*)definition+11,buffer,args); // point at space after label
		}
#endif
		else if (definition)
		{
			unsigned int flags = OUTPUT_FNDEFINITION;
			if (!(D->internalBits & IS_OUTPUT_MACRO)) flags|= OUTPUT_NOTREALBUFFER;// if we are outputmacro, we are merely extending an existing buffer
			Output((char*)definition,buffer,result,flags);
		}

		// undo any display variables
		if (definition && basedisplay) RestoreDisplay(baseinvert,basedisplay);

		trace = (modifiedTrace) ? modifiedTraceVal : oldtrace;
		if (callIndex) --callIndex; // safe decrement
		if (result & ENDCALL_BIT) result = (FunctionResult) (result ^ ENDCALL_BIT); // terminated user call 
	} // end user function

TERMINATE:
	fnVarBase = oldFnVarBase;
	functionAnswerBase = oldFunctionAnswerBase;
	functionAnswerName = oldFunctionAnswerName;
	ChangeDepth(-1,D->word);

	if ((trace & TRACE_OUTPUT || D->internalBits & MACRO_TRACE || (trace & TRACE_USERFN && definition)) && CheckTopicTrace()) 
	{
		// make a short description for which call this is, if we can
		char word[MAX_WORD_SIZE];
		*word = 0;
		if (callArgumentIndex > firstArgument) 
		{
			strncpy(word,callArgumentList[firstArgument],40);
			if (*word == '$') strncpy(word,GetUserVariable(word),40);
			word[40] = 0;	
		}
		if (trace == TRACE_USERFN)  Log(STDTRACETABLOG,(char*)"%s %s(%s) => %s\r\n",ResultCode(result),name,word,buffer);
		else if (info && info->properties & SAMELINE) Log(STDTRACETABLOG,(char*)"%s %s(%s) => %s\r\n",ResultCode(result),name,word,buffer);	// stay on same line to save visual space in log
		else Log(STDTRACETABLOG,(char*)"%s %s(%s) => %s\r\n",ResultCode(result),name,word,buffer);
	}
	// currently only user functions- NOTRACE printouts would be boring
	if (definition && (timing & TIME_USERFN || D->internalBits & MACRO_TIME) && !(D->internalBits & NOTIME_FN) && CheckTopicTime()) {
		int diff = ElapsedMilliseconds() - start_time;
		if (diff > 0 || timing & TIME_ALWAYS ) 
		{
			// make a short description for which call this is, if we can
			char word[MAX_WORD_SIZE];
			*word = 0;
			if (callArgumentIndex > firstArgument) 
			{
				strncpy(word,callArgumentList[firstArgument],40);
				if (*word == '$') strncpy(word,GetUserVariable(word),40);
				word[40] = 0;	
			}
			
			Log(STDTIMETABLOG, (char*)"%s(%s) time: %d ms\r\n", name,word,diff);
		}
	}
	//   pop argument list
	callArgumentIndex = oldArgumentIndex;	 
	callArgumentBase = oldArgumentBase;

	impliedIf = oldimpliedIf;
	if (ptr && *ptr == ')') // skip ) and space if there is one...
	{
		if (ptr[1] == ' ') return ptr+2; // if this is a pattern comparison, this will NOT be a space, but will be a comparison op instead missing it
		return ptr+1;	// ptr to the end unit
	}
	else return ptr;
}

void DumpFunctions()
{
	unsigned int k = 0;
	SystemFunctionInfo *fn;
	while ( (fn = &systemFunctionSet[++k])  && fn->word )
	{
		if (*fn->word != '^') Log(STDTRACELOG,(char*)"%s\r\n",fn->word);
		else Log(STDTRACELOG,(char*)"%s - %s\r\n",fn->word,fn->comment);
	}
}

//////////////////////////////////////////////////////////
/// FUNCTION UTILITIES
//////////////////////////////////////////////////////////

char* ResultCode(FunctionResult result)
{
	char* ans = "OK";
	if (result & ENDCALL_BIT) ans = "ENDCALL";
	else if (result & ENDRULE_BIT) ans = "ENDRULE";
	else if (result & FAILRULE_BIT) ans = "FAILRULE";
	else if (result & RETRYRULE_BIT) ans = "RETRYRULE";
	else if (result & RETRYTOPRULE_BIT) ans = "RETRYTOPRULE";

	else if (result & ENDTOPIC_BIT) ans = "ENDTOPIC";
	else if (result & FAILTOPIC_BIT) ans = "FAILTOPIC";
	else if (result & RETRYTOPIC_BIT) ans = "RETRYTOPIC";

	else if (result & ENDSENTENCE_BIT) ans = "ENDSENTENCE";
	else if (result & FAILSENTENCE_BIT) ans = "FAILSENTENCE";
	else if (result & RETRYSENTENCE_BIT) ans = "RETRYSENTENCE";

	else if (result & RETRYINPUT_BIT) ans = "RETRYINPUT";
	else if (result & ENDINPUT_BIT) ans = "ENDINPUT";
	else if (result & FAILINPUT_BIT) ans = "FAILINPUT";
	else if (result & FAILMATCH_BIT) ans = "FAILMATCH";
	else if (result == NOPROBLEM_BIT) ans = "NOPROBLEM";
	else if (result == FAILLOOP_BIT) ans = "FAILLOOP";
	else if (result == ENDLOOP_BIT) ans = "ENDLOOP";
	else if (result & UNDEFINED_FUNCTION) ans = "UNDEFINED_FUNCTION";
	return ans;
};

 static void AddInput(char* buffer)
{
	char* copy = AllocateBuffer();
	strcpy(copy,nextInput);
	strcpy(nextInput,(char*)" `` "); // system separator marks start of internal input
	char* ptr = nextInput + 4;
	unsigned int n = BurstWord(buffer);
	for (unsigned int i = 0; i < n; ++i)
	{
        strcpy(ptr,GetBurstWord(i));
		ptr += strlen(ptr);
		strcpy(ptr++,(char*)" ");
	}
	strcpy(ptr,(char*)" ` "); // mark end of internal input
	ptr += 3;
	strcpy(ptr,copy);
	FreeBuffer();
	if (strlen(nextInput) > 1000) nextInput[1000] = 0;	// overflow
}

static unsigned int ComputeSyllables(char* word)
{
	char copy[MAX_WORD_SIZE];
	MakeLowerCopy(copy,word);
	size_t len = strlen(copy);
	if (len <= 3) return 1;

	char* ptr = copy-1;
	unsigned int vowels = 0;
	int series = 0;
	while (*++ptr)
	{
		if (!IsVowel(*ptr)) 
		{
			if (series >= 4) --vowels; 
			series = 0;
		}
		else 
		{
			++vowels;
			++series;
		}
	}
	// silent e
	if (copy[len-1] == 'e' && !IsVowel(copy[len-2]) && IsVowel(copy[len-3])) --vowels;	// silent e
	
	// silent es or ed
	if ((copy[len-1] == 'd' || copy[len-1] == 's') && copy[len-2] == 'e' && !IsVowel(copy[len-3]) && IsVowel(copy[len-4])) --vowels;	// silent e

	return vowels;
}

static FunctionResult RandomMember(char* buffer,char* answer) 
{
#ifdef INFORMATION
returns a random member of a set or class

returns FAILRULE if a bad set is given.

The value is recursive. If the random member chosen is a set or class, the
link is followed and a random member from the next level is chosen, and so on. 
If the value is a wordnet reference it goes lower until it cant go any lower.

#endif
	MEANING members[3000];
loop:
	WORDP D = FindWord(answer);
	if (!D ) return FAILRULE_BIT;

    unsigned int count = 0;
    FACT* F = GetObjectNondeadHead(D);
    while (F && count < 2999)
    {
        if (F->verb == Mmember) members[count++] = F->subject;
        F = GetObjectNondeadNext(F);
    }
    if (!count) return FAILRULE_BIT; //   none found

	//   pick one at random
	while (ALWAYS)
	{
		MEANING M =  members[random(count)];
		M = GetMaster(M);
		D = Meaning2Word(M);
		unsigned int index = Meaning2Index(M);
		answer = D->word;
		if (*answer == '~') goto loop; //   member is a subset or class, get from it instead

		else if (index) // go down hierarchy until you cant and use that
		{
			F = GetObjectNondeadHead(D); // he is the general, get a specific
			count = 0;
			while (F && count < 2999)
			{
				if (F->verb == Mis && Meaning2Index(F->object) == index) members[count++] = F->subject;
				F = GetObjectNondeadNext(F);
			}
			if (count) continue; // select from there
			// we are a bottom meaning
			strcpy(buffer,D->word);
			return NOPROBLEM_BIT;
		}
		else break;
	}

    if (*answer == '<') ++answer; //   interjections have < in front
	strcpy(buffer,answer);
    return NOPROBLEM_BIT;
}

FunctionResult FLR(char* buffer,char* which)
{  
	int store;
	*buffer = 0;
	char word[MAX_BUFFER_SIZE];
	bool keep = false;
	char* ptr = GetPossibleFunctionArgument(ARGUMENT(1),word);
	if (!strnicmp(ptr,(char*)"KEEP",4)) keep = true;

	store = GetSetID(word);
	if (store == ILLEGAL_FACTSET) return FAILRULE_BIT;
	unsigned int count = FACTSET_COUNT(store);
	if (!count) 
	{
		if (impliedWild != ALREADY_HANDLED)
		{
			SetWildCardIndexStart(impliedWild);
			SetWildCard((char*)"",(char*)"",0,0); // subject
			SetWildCard((char*)"",(char*)"",0,0);	// verb
			SetWildCard((char*)"",(char*)"",0,0);	// object
			SetWildCard((char*)"",(char*)"",0,0);	// flags
		}
		impliedWild = ALREADY_HANDLED;
		return ENDRULE_BIT; //   terminates but does not cancel output
	}
	
	if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACELOG,(char*)"[%d] => ",count);

	if (!withinLoop && planning && (*which != 'n') && *GetTopicName(currentTopicID) == '^' && !backtrackable) backtrackable = true;
	
	// pick fact
	unsigned int item;
	if (*which == 'l') item = count; //   last
	else if (*which == 'f') item = 1; //   first
	else if (*which == 'n') // next
	{
		item = ++factSetNext[store];
		if (count < item) return FAILRULE_BIT; 
	}
	else if (*which == 'r') item = random(count) + 1;    // random
	else // specific index
	{
		keep = true;
		item = atoi(which);
		if (count < item || item == 0) return FAILRULE_BIT;
	}
	FACT* F = factSet[store][item];

	// remove fact from set, but next does not alter set
	if (*which != 'n' && !keep)
	{
		SET_FACTSET_COUNT(store,(count-1));
		memmove(&factSet[store][item],&factSet[store][item+1],sizeof(FACT*) * (count - item)); 
	}		

	char type = *GetSetType(word);

	// transfer fact pieces appropriately
	MEANING Mfirst = 0;
	MEANING Mlast = 0;
	uint64 factSubject = 0;
	uint64 factObject = 0;
	if (trace & TRACE_INFER && CheckTopicTrace()) TraceFact(F);
	if (type == 'f') // want entire fact as index
	{
		if (impliedSet == ALREADY_HANDLED) sprintf(buffer,(char*)"%d",Fact2Index(F)); 
		else AddFact(impliedSet,F);
	}
	else if (type == 's') // want subject
	{
		if (!F) strcpy(buffer,(char*)"null");
		else
		{
			MEANING M = F->subject;
			if (F->flags & FACTSUBJECT) sprintf(buffer,(char*)"%d",M);
			else sprintf(buffer,(char*)"%s",Meaning2Word(M)->word);
		}
	}
	else if (type == 'v') // want verb
	{
		if (!F) strcpy(buffer,(char*)"null");
		else
		{
			MEANING M = F->verb;
			if (F->flags & FACTVERB) sprintf(buffer,(char*)"%d",M);
			else sprintf(buffer,(char*)"%s",Meaning2Word(M)->word);
		}
	}
	else if (type == 'o') // want object
	{
		if (!F) strcpy(buffer,(char*)"null");
		else
		{
			MEANING M = F->object;
			if (F->flags & FACTOBJECT) sprintf(buffer,(char*)"%d",M);
			else sprintf(buffer,(char*)"%s",Meaning2Word(M)->word);
		}
	}
	else if ( type == 'a' || type == '+'  || type == ' ' || !type || type == 'r') // want all, subject first
	{
		if (!F) Mfirst = Mlast = 0xffffffff;
		else
		{
			Mfirst = F->subject;
			factSubject = F->flags & FACTSUBJECT;
			Mlast = F->object;
			factObject = F->flags & FACTOBJECT;
		}
	}
	else // want all, object first
	{
		if (!F) Mfirst = Mlast = 0xffffffff;
		else
		{
			Mlast = F->subject;
			factObject = F->flags & FACTSUBJECT;
			Mfirst = F->object;
			factSubject= F->flags & FACTOBJECT;
		}
	}
	if (Mfirst) // spread
	{
		char factID[100];
		char* piece;
		if ( Mfirst == 0xffffffff) piece = "null";
		else if (factSubject) 
		{
			sprintf(factID,(char*)"%d",Mfirst);
			piece = factID;
		}
		else if (type == 'r') piece = WriteMeaning(Mfirst,false);
		else piece = Meaning2Word(Mfirst)->word;

		// _wildcard can take all, otherwise you get just a field
		// for variables. not legal for sets
		if (!F) strcpy(buffer,(char*)"null");
		else if (impliedWild == ALREADY_HANDLED) strcpy(buffer,piece);
		else 
		{
			SetWildCardIndexStart(impliedWild);
			if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" _%d=%s ",impliedWild,piece);
			SetWildCard(piece,piece,0,0); 

			 //   verb 
			if ( Mfirst == 0xffffffff) piece = "null";
			else
			{
				MEANING M = F->verb;
				if (F->flags & FACTVERB) 
				{
					sprintf(factID,(char*)"%d",M);
					piece = factID;
				}
				else if (type == 'r') piece = WriteMeaning(M,false);
				else piece = Meaning2Word(M)->word;
			}
			if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" _%d=%s ",impliedWild+1,piece);
			SetWildCard(piece,piece,0,0);

			//   object
			if ( Mfirst == 0xffffffff) piece = "null";
			else if (factObject) 
			{
				sprintf(factID,(char*)"%d",Mlast);
				piece = factID;
			}
			else if (type == 'r') piece = WriteMeaning(Mlast,false);
			else piece = Meaning2Word(Mlast)->word;
			if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" _%d=%s ",impliedWild+2,piece);
			SetWildCard(piece,piece,0,0); 

			if ( type == 'a' && F) // all include flags on fact
			{
				sprintf(tmpWord,(char*)"0x%08x",F->flags);
				SetWildCard(tmpWord,tmpWord,0,0);
			}
		}
		impliedSet = impliedWild = ALREADY_HANDLED; // we spread the values out
	}
	if (trace & TRACE_OUTPUT && *buffer && CheckTopicTrace()) Log(STDTRACELOG,(char*)" %s  ",buffer);
	return NOPROBLEM_BIT;
}

bool RuleTest(char* data) // see if pattern matches
{
	char pattern[MAX_WORD_SIZE];
	GetPattern(data,NULL,pattern);
	wildcardIndex = 0;
	int junk;
	bool uppercasem = false;
	int matched = 0;
	bool answer =  Match(pattern+2,0,0,(char*)"(",1,0,junk,junk,uppercasem,matched,0,0); // start past the opening paren
	if (clearUnmarks) // remove transient global disables.
	{
		clearUnmarks = false;
		for (int i = 1; i <= wordCount; ++i) unmarked[i] = 1;
	}
	ShowMatchResult(answer ? NOPROBLEM_BIT : FAILRULE_BIT, pattern+2,NULL);
	return answer;
}

unsigned int Callback(WORDP D,char* arguments, bool boot) 
{
	if (! D || !(D->internalBits & FUNCTION_NAME)) return FAILRULE_BIT;
	unsigned int oldtrace = trace;
	if (!boot) trace = 0;
	char args[MAX_WORD_SIZE];
	strcpy(args,arguments);
	FunctionResult result;
	AllocateOutputBuffer();
	DoFunction(D->word,args,currentOutputBase,result);
	if (*currentOutputBase && boot)
	{
		printf("Boot response: %s\r\n",currentOutputBase);
		if (server) Log(SERVERLOG, "Boot response: %s\r\n",currentOutputBase);
	}
	FreeOutputBuffer();
	trace = (modifiedTrace) ? modifiedTraceVal : oldtrace;
	return result;
}

void ResetUser(char* input)
{
	if (globalDepth ) // in midst of execution, being safe
	{
		inputCounter = 0;
		totalCounter = 0;
		itAssigned = theyAssigned = 0;
		inputSentenceCount = 0;
		ReadNewUser();
		userFirstLine = 1;
		return;
	}

	unsigned int oldtopicid = currentTopicID;
	char* oldrule = currentRule;
	int oldruleid = currentRuleID;
	int oldruletopic = currentRuleTopic;
	ResetToPreUser();	// back to empty state before user
	KillShare();
	ReadNewUser(); 
	userFirstLine = 1;
	responseIndex = 0;
	wasCommand = BEGINANEW;
	*input = 0;
	currentTopicID = oldtopicid;
	currentRule = oldrule;
	currentRuleID = oldruleid;
	currentRuleTopic = oldruletopic;
}

//////////////////////////////////////////////////////////
/// TOPIC FUNCTIONS
//////////////////////////////////////////////////////////

static FunctionResult AddTopicCode(char* buffer) 
{     
	AddPendingTopic(FindTopicIDByName(ARGUMENT(1))); // does not fail, just may not become pending
	return NOPROBLEM_BIT;
}

static FunctionResult ClearTopicsCode(char* buffer)
{
	ClearPendingTopics();
	return NOPROBLEM_BIT;
}

static FunctionResult CountTopicCode(char* buffer) 
{     
	int topic = FindTopicIDByName(ARGUMENT(1));
	if (BlockedBotAccess(topic)) return FAILRULE_BIT;
	topicBlock* block = TI(topic);

	char* name = ARGUMENT(2);
	if (!strnicmp(name,(char*)"gambit",6)) sprintf(buffer,(char*)"%d", GAMBIT_MAX(block->topicMaxRule)); 
	else if (!strnicmp(name,(char*)"rule",4)) sprintf(buffer,(char*)"%d", RULE_MAX(block->topicMaxRule)); 
	else if (!stricmp(name,(char*)"used")) sprintf(buffer,(char*)"%d",TopicUsedCount(topic));
	else if (!stricmp(name,(char*)"available"))
	{
		unsigned int count = 0;
		unsigned int* map = block->gambitTag;	
		unsigned int gambitID = *map;
		while (gambitID != NOMORERULES)
		{
			if (UsableRule(topic,gambitID)) ++count;
			gambitID = *++map;
		}
		sprintf(buffer,(char*)"%d",count); 
	}
	else return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

static FunctionResult GambitCode(char* buffer) 
{ 
	char arguments[MAX_ARG_LIMIT+1][200];
	if (planning) return FAILRULE_BIT;	// cannot call from planner
	if (all) return FAILRULE_BIT; // dont generate gambits when doing all
	bool fail = false;
	unsigned int i;
	for (i = 1; i < MAX_ARG_LIMIT; ++i)
	{
		char* a = ARGUMENT(i);
		if (!*a) break;
		size_t len = strlen(a);
		if (len > SIZELIM)
		{
			ReportBug((char*)"Respond code size limit exceeded to %d for %s\r\n",len,a);
			return FAILRULE_BIT;
		}
		strcpy(arguments[i],a);
		if (!stricmp(a,(char*)"FAIL")  && !*ARGUMENT(i+1)) 
		{
			fail = true;
			*arguments[i] = 0;
		}
	}
	*arguments[i] = 0;
	
	int oldIndex = responseIndex;
	FunctionResult result = NOPROBLEM_BIT;
	int oldreuseid = currentReuseID;
	int oldreusetopic = currentReuseTopic;
	for (i = 1; i < MAX_ARG_LIMIT; ++i)
	{
		// gambit(PENDING) means from interesting stack  
		// gambit(~name) means use named topic 
		// gambit(~) means current topic we are within now
		// gambit (word) means topic with that keyword
		char* word = arguments[i];
		if ((oldIndex < responseIndex && result == NOPROBLEM_BIT) || result != NOPROBLEM_BIT || *word == 0) break; // generated an answer or failed or ran out
		oldIndex = responseIndex; // in case answer generated but topic claims failure
		if (trace & TRACE_TOPIC) Log(STDTRACELOG,(char*)"Gambit trying %s\r\n",word);
		
		//  if "~", get current topic name to use for gambits
		int topic;
	
		currentReuseID = currentRuleID; // LOCAL reuse
		currentReuseTopic = currentRuleTopic;
		int oldCurrentTopic = currentTopicID;

   		if (!stricmp(word,(char*)"pending")) // pick topic from pending stack
		{
			unsigned int stack[MAX_TOPIC_STACK+1];
			memcpy(stack,pendingTopicList,pendingTopicIndex * sizeof(unsigned int)); // copy stack
			int oldPendingIndex = pendingTopicIndex;
			while (oldPendingIndex) // walk topics, most recent first
			{
				topic = stack[--oldPendingIndex];
				char* xname = GetTopicName(currentTopicID); // just for debugging
				if (TopicInUse(topic) == -1) continue;
				currentTopicID = topic;
				ChangeDepth(1,(char*)"GambitCode");
				FunctionResult myresult = PerformTopic(GAMBIT,buffer);
				ChangeDepth(-1,(char*)"GambitCode");
				if (myresult & RESULTBEYONDTOPIC) 
				{
					result = myresult;
					break;
				}
				if (responseIndex > oldIndex) 
				{
					result = NOPROBLEM_BIT;
					break;
				}
			}
		}

		 // do topic by name
		else if (*word == '~')
		{
			topic = FindTopicIDByName(word);
			if (topic && !(GetTopicFlags(topic) & TOPIC_BLOCKED))
			{
 				int pushed = PushTopic(topic);
				if (pushed < 0) result =  FAILRULE_BIT;
				else 
				{
					ChangeDepth(1,(char*)"GambitCode1");
					result = PerformTopic(GAMBIT,buffer);
					ChangeDepth(-1,(char*)"GambitCode1");

					if (pushed) PopTopic();
				}
			}
		}
	
		// do topic by keyword
		else
		{
			WORDP D = FindWord(word);
			FACT* F = NULL;
			if (!D) result = NOPROBLEM_BIT;
			else  F = GetSubjectNondeadHead(D);
			while (F) // find topics word is a direct member of
			{
				if (F->verb == Mmember)
				{
					WORDP E = Meaning2Word(F->object);
					if (E->internalBits & TOPIC)
					{
						int xtopic = FindTopicIDByName(E->word);
						if (xtopic && !(GetTopicFlags(xtopic) & (TOPIC_BLOCKED|TOPIC_SYSTEM|TOPIC_NOGAMBITS)))
						{
 							int pushed = PushTopic(xtopic);
							if (pushed < 0) 
							{
								result = FAILRULE_BIT;
								break;
							}
							ChangeDepth(1,(char*)"GambitCode2");
							result = PerformTopic(GAMBIT,buffer);
							ChangeDepth(-1,(char*)"GambitCode2");
							if (pushed) PopTopic();
							if (result & RESULTBEYONDTOPIC) break;
							if (responseIndex > oldIndex)  
							{
								result = NOPROBLEM_BIT;
								break;
							}
						}
					}
				}
				F = GetSubjectNondeadNext(F);
			} 
		}
		currentTopicID = oldCurrentTopic; // this is where we were

	}
	if (fail  && responseIndex <= oldIndex)  result = FAILRULE_BIT; // report failure
	currentReuseID = oldreuseid;
	currentReuseTopic = oldreusetopic;
	return result;
}

static FunctionResult GetVerifyCode(char* buffer) 
{
	char* arg1 = ARGUMENT(1);
	int topicid;
	int id;
	char* verify = GetVerify(arg1,topicid,id); //  ~topic.#.#=LABEL<~topic.#.#  is a maximally complete why
	if (verify) strcpy(buffer,verify);
	return NOPROBLEM_BIT;
}

static FunctionResult GetRuleCode(char* buffer) 
{     
	char* arg1 = ARGUMENT(1);
	char* arg2 = ARGUMENT(2);
	char* arg3 = ARGUMENT(3);
	int topic = currentTopicID;
	int id;
	char* rule;
	bool fulllabel = false;
	bool crosstopic = false;
	char* dot = strchr(arg2,'.');
	if (dot && IsDigit(dot[1])) rule = GetRuleTag(topic,id,arg2);
	else rule = GetLabelledRule(topic,arg2,arg3,fulllabel,crosstopic,id,currentTopicID);
	if (!rule) return FAILRULE_BIT;
	if (!stricmp(arg1,(char*)"tag")) sprintf(buffer,(char*)"%s.%d.%d",GetTopicName(topic),TOPLEVELID(id),REJOINDERID(id));
	else if (!stricmp(arg1,(char*)"topic")) strcpy(buffer,GetTopicName(topic));
	else if (!stricmp(arg1,(char*)"label")) GetLabel(rule,buffer);
	else if (!stricmp(arg1,(char*)"type")) sprintf(buffer,(char*)"%c",*rule);
	else if (!stricmp(arg1,(char*)"pattern")) // use pattern notation so it can work with ^match and will not be harmed stored as a variable
	{
		*buffer = '"';
		buffer[1] = 0;
		GetPattern(rule,NULL,buffer+1);
		if (!buffer[1]) *buffer = 0;
		else strcat(buffer,(char*)"\"");
	}
	else if (!stricmp(arg1,(char*)"usable")) strcpy(buffer,(UsableRule(topic,id)) ? (char*) "1" : (char*) "");
	else // output
	{
		 rule = GetPattern(rule,NULL,NULL);
		 char* end = strchr(rule,ENDUNIT);  // will not be a useful output as blanks will become underscores, but can do ^reuse() to execute it
		 *end = 0;
		 strcpy(buffer,rule);
		 *end = ENDUNIT;
	}
	if (trace & TRACE_OUTPUT && CheckTopicTrace())
	{
		char word[MAX_WORD_SIZE];
		strncpy(word,buffer,50);
		word[50] = 0;
		Log(STDTRACELOG,(char*)" %s ",word);
	}
	return NOPROBLEM_BIT;
}
	
static FunctionResult HasGambitCode(char* buffer)
{
	// hasgambit(~topic) means does it have any unused gambits
	// hasgambit(~topic last) means is last gambit unused
	// hasgambit(~topic any) means does it have gambits used or unused
	char* name = ARGUMENT(1);
	int topic = FindTopicIDByName(name);
	if (!topic) return FAILRULE_BIT;
	topicBlock* block = TI(topic);

	unsigned int gambits = GAMBIT_MAX(block->topicMaxRule);   // total gambits of topic
	if (!gambits) return FAILRULE_BIT;	

	char* arg = ARGUMENT(2);
	if (!stricmp(arg,(char*)"last")) return UsableRule(topic,block->gambitTag[gambits-1]) ? NOPROBLEM_BIT : FAILRULE_BIT; // is last gambit unused
	else if (!stricmp(arg,(char*)"any")) return NOPROBLEM_BIT;
	else return (HasGambits(topic) < 1) ? FAILRULE_BIT : NOPROBLEM_BIT;
}

static FunctionResult KeepCode(char* buffer)
{
	if (planning) return FAILRULE_BIT;

	AddKeep(currentRule);
	return NOPROBLEM_BIT;
}

static FunctionResult LastUsedCode(char* buffer)
{
	char* name = ARGUMENT(1);
	char* what = ARGUMENT(2);
	int topic = FindTopicIDByName(name);
	if (!topic)  return FAILRULE_BIT;  
	topicBlock* block = TI(topic);

	if (!stricmp(what,(char*)"gambit")) sprintf(buffer,(char*)"%d",block->topicLastGambitted);
	else if (!stricmp(what,(char*)"responder")) sprintf(buffer,(char*)"%d",block->topicLastRespondered);
	else if (!stricmp(what,(char*)"rejoinder")) sprintf(buffer,(char*)"%d",block->topicLastRejoindered);
	else // any 
	{
		int last = block->topicLastRejoindered;
		if (block->topicLastRespondered > last) last = block->topicLastRespondered;
		if (block->topicLastGambitted > last) last = block->topicLastGambitted;
		sprintf(buffer,(char*)"%d",last);
	}
	return NOPROBLEM_BIT;
}

static FunctionResult PopTopicCode(char* buffer) // reconsider BUG
{     
	char* arg1 = ARGUMENT(1);
	if (*arg1 == '~') RemovePendingTopic(FindTopicIDByName(arg1)); // current topic may continue executing
	else if (!*arg1) // make current topic not interesting AND quit it
	{
		RemovePendingTopic(currentTopicID);
		return ENDTOPIC_BIT; 
	}
	else return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

static FunctionResult DoRefine(char* buffer,char* arg1, bool fail, bool all) 
{
	FunctionResult result = NOPROBLEM_BIT;
	char* rule;
    int id = currentRuleID;
	int topic = currentTopicID;
	int originalTopic = topic;
	char level = *currentRule;

	if (!*arg1) 
	{
		// of course if there were NO rejoinders this would be in error
		rule =  currentRule; // default continue AFTER the current rule
		level = TopLevelRule(currentRule) ?  'a' : (*currentRule+1);
	}
	else // designated
	{
		bool fulllabel = false;
		bool crosstopic = false;
		char* dot = strchr(arg1,'.');
		if (dot && IsDigit(dot[1])) rule = GetRuleTag(topic,id,arg1);
		else rule = GetLabelledRule(topic,arg1,(char*)"",fulllabel,crosstopic,id,currentTopicID);
		level = TopLevelRule(rule) ?  'a' : (*rule+1);
	}

	if (!rule) return FAILRULE_BIT;

	// change context now
	SAVEOLDCONTEXT()
	currentRuleTopic = currentTopicID = topic;
	currentRuleID = id;
	currentRule = FindNextRule(NEXTRULE,rule,id); 

	unsigned int oldTrace = EstablishTopicTrace();

	bool failed = false;

	ChangeDepth(1,(char*)"DoRefineCode");
	unsigned int oldTopicDisplay = currentTopicDisplay;
	currentTopicDisplay = currentTopicID;
	char* locals = GetTopicLocals(currentTopicID);
	if (locals && currentTopicDisplay != oldTopicDisplay && !InitDisplay(locals)) failed = true; 

	while (currentRule && level == *currentRule) // try all choices
    {
		if (trace & TRACE_PATTERN && CheckTopicTrace())
		{
			char label[MAX_WORD_SIZE];
			GetLabel(currentRule,label);
			if (*label) Log(STDTRACETABLOG, "try %s: \\",label); // the \\ will block linefeed on next Log call
			else Log(STDTRACETABLOG, "try  \\");
		}
		result = TestRule(id,currentRule,buffer,true);
	    if (all && result != NOPROBLEM_BIT && result != FAILMATCH_BIT) break; // failure
		else if (!all && result != FAILMATCH_BIT && result != FAILRULE_BIT) break;
		else result = NOPROBLEM_BIT;

		while (currentRule && *currentRule)
		{
			currentRule = FindNextRule(NEXTRULE,currentRule,id); 
			if (currentRule && (*currentRule <= level  || !Rejoinder(currentRule))) break;	// matches our level OR is earlier than it (end of a zone like refine of a: into b: zone)
		}
    }
	if (outputRejoinderRuleID == NO_REJOINDER) outputRejoinderRuleID = BLOCKED_REJOINDER; // refine values exist instead of real rejoinders, dont let calling rule do set rejoinder
	if (result == RETRYTOPIC_BIT) // retry the topic of THIS rejoinder, not of the caller
	{
		if (originalTopic != currentTopicID) // it was not us, so need to gambit or rejoinder on this topic that we are in now
		{
			ARGUMENT(1) = AllocateInverseString(GetTopicName(currentTopicID));
			ARGUMENT(2) = AllocateInverseString((char*)"");
			GambitCode(buffer+strlen(buffer));
		}
	}
	if (locals && currentTopicDisplay != oldTopicDisplay && !failed) RestoreDisplay(inverseStringDepth[globalDepth],locals);
	currentTopicDisplay = oldTopicDisplay;
	ChangeDepth(-1,(char*)"DoRefineCode");

	RESTOREOLDCONTEXT()

	trace = (modifiedTrace) ? modifiedTraceVal : oldTrace;
	// finding none does not fail unless told to fail
	if (fail && (!currentRule || level != *currentRule)) result = FAILRULE_BIT;
	return result; 
}

static FunctionResult RefineCode(char* buffer) 
{
	int i = 1;
	bool fail = false;
	if (!stricmp(ARGUMENT(1),(char*)"FAIL")) 
	{
		fail = true; 
		++i; // promote any 2nd argument
	}
	return DoRefine(buffer,ARGUMENT(i),fail,false);
}

static FunctionResult SequenceCode(char* buffer) 
{
	return DoRefine(buffer,ARGUMENT(1),false,true);
}

static FunctionResult RejoinderCode(char* buffer)
{ 
	if (postProcessing)
	{
		ReportBug((char*)"Not legal to use ^rejoinder in postprocessing");
		return FAILRULE_BIT;
	}
    if (!unusedRejoinder) 
	{
		if (trace & TRACE_TOPIC && CheckTopicTrace()) Log(STDTRACELOG,(char*)" disabled rejoinder\r\n\r\n");
		return NOPROBLEM_BIT; //   an earlier response handled this
	}

	if (*ARGUMENT(1)) // 
	{
		char* tag = ARGUMENT(1);
		int topic = currentTopicID;
		bool fulllabel = false;
		bool crosstopic = false;
		char* rule;
		char* dot = strchr(tag,'.');
		int id;
		if (dot && IsDigit(dot[1])) rule = GetRuleTag(topic,id,tag);
		else rule = GetLabelledRule(topic,tag,(char*)"",fulllabel,crosstopic,id,currentTopicID);
		if (!rule) return FAILRULE_BIT; // unable to find labelled rule 

		char level = TopLevelRule(rule)   ? 'a' :  (*rule+1); // default rejoinder level
		char* ptr = FindNextRule(NEXTRULE,rule,id);
		if (!ptr || *ptr != level) return FAILRULE_BIT;		// not found

		inputRejoinderRuleID = id; 
 		inputRejoinderTopic = topic;
	}

	if (inputRejoinderRuleID == NO_REJOINDER) 
	{
		if (trace & TRACE_PATTERN && CheckTopicTrace()) Log(STDTRACELOG,(char*)"  rejoinder not set\r\n");
		return NOPROBLEM_BIT; // not a failure, just nothing done
	}

    //   we last made a QUESTIONWORD or statement, can his reply be expected for that? 
	FunctionResult result = NOPROBLEM_BIT;
	int pushed = PushTopic(inputRejoinderTopic);
	if (pushed < 0) return FAILRULE_BIT;
	
    char* ptr = GetRule(inputRejoinderTopic,inputRejoinderRuleID);
    if (!ptr)  
	{
		if (trace & TRACE_TOPIC && CheckTopicTrace()) Log(STDTRACELOG,(char*)" no rejoinder data for topic %s %d.%d\r\n\r\n",GetTopicName(currentTopicID),TOPLEVELID(inputRejoinderRuleID),REJOINDERID(inputRejoinderRuleID));
		if (pushed) PopTopic();
		return result;
	}

	unsigned int oldtrace = EstablishTopicTrace();
	if (trace & TRACE_TOPIC && CheckTopicTrace()) 
	{
		char label[MAX_WORD_SIZE];
		*label = 0;
		if (*ptr == 'a') // simple 1st level
		{
			*label = '(';
			char* top = GetRule(inputRejoinderTopic,TOPLEVELID(inputRejoinderRuleID));
			GetLabel(top,label+1);
			if (label[1]) strcat(label,")");
			else *label = 0;
		}
		Log(STDTRACELOG,(char*)"  try rejoinder for: %s %d.%d%s",GetTopicName(currentTopicID),TOPLEVELID(inputRejoinderRuleID),REJOINDERID(inputRejoinderRuleID),label);
	}

	int id = inputRejoinderRuleID;
	
    char level;
    level = *ptr; //   what marks this level
	ChangeDepth(1,(char*)"RejoinderCode");
	unsigned int oldTopicDisplay = currentTopicDisplay;
	currentTopicDisplay = currentTopicID;
	char* locals = GetTopicLocals(currentTopicID);

    while (ptr && *ptr) //   loop will search for a level answer it can use
    {
        if (TopLevelRule(ptr)) break; // failed to find rejoinder
        else if (*ptr < level) break;  // end of local choices
        else if (*ptr == level) // check rejoinder
        {
			result = TestRule(id,ptr,buffer);
			if (result == FAILMATCH_BIT) result = FAILRULE_BIT; // convert 
			if (result == NOPROBLEM_BIT) // we found a match
			{
				unusedRejoinder = false;
				break; 
			}
			if (result & (RESTART_BIT|RETRYTOPIC_BIT|RETRYSENTENCE_BIT|FAILTOPIC_BIT|ENDTOPIC_BIT|FAILSENTENCE_BIT|ENDSENTENCE_BIT|ENDINPUT_BIT|RETRYINPUT_BIT|FAILINPUT_BIT)) break;
			result = NOPROBLEM_BIT;
        }
       ptr = FindNextRule(NEXTRULE,ptr,id); //   wrong or failed responder, swallow this subresponder whole
    }
	if (result == RETRYTOPIC_BIT) // retry the topic of THIS rejoinder, not of the caller who is likely just the control script topic
	{
		if (pushed) // it was not us, so need to gambit or rejoinder on this topic that we are in now
		{
			ARGUMENT(1) = AllocateInverseString(GetTopicName(currentTopicID));
			ARGUMENT(2) = AllocateInverseString((char*)"");
			GambitCode(buffer+strlen(buffer));
		}
	}
	if (pushed) PopTopic(); 
	ChangeDepth(-1,(char*)"RejoinderCode");
	currentTopicDisplay = oldTopicDisplay;

    if (inputSentenceCount) // this is the 2nd sentence that failed, give up
    {   
		if (trace & TRACE_FLOW) Log(STDTRACELOG,(char*)"Clearing input rejoinder on 2nd sentence");
        inputRejoinderRuleID = NO_REJOINDER;
        unusedRejoinder = false;
    }
	trace = (modifiedTrace) ? modifiedTraceVal : oldtrace;
	return  result;
}

static FunctionResult RespondCode(char* buffer)
{  // failing to find a responder is not failure unless ask it to be
	char arguments[MAX_ARG_LIMIT+1][SIZELIM];
	if (planning) return FAILRULE_BIT;	// cannot call from planner
	bool fail = false;
	// if a last argument exists (FAIL) then return failure code if doesnt generate output to user
	unsigned int  i;
	for (i = 1; i < MAX_ARG_LIMIT; ++i)
	{
		char* a = ARGUMENT(i);
		if (!*a) break; // end
		size_t len = strlen(a);
		if (len > SIZELIM)
		{
			ReportBug((char*)"Respond code size limit exceeded to %d for %s\r\n",len,a);
			return FAILRULE_BIT;
		}
		strcpy(arguments[i],a);
		if (!stricmp(a,(char*)"FAIL")  && !*ARGUMENT(i+1)) 
		{
			fail = true;
			*arguments[i] = 0;
		}
	}
	*arguments[i] = 0;
	int oldIndex = responseIndex;
	FunctionResult result = NOPROBLEM_BIT;
	int oldreuseid = currentReuseID;
	int oldreusetopic = currentReuseTopic;
	currentReuseID = currentRuleID; // LOCAL reuse
	currentReuseTopic = currentRuleTopic;
	int oldCurrentTopic = currentTopicID;

	for (i = 1; i < MAX_ARG_LIMIT; ++i)
	{
		// gambit(PENDING) means from interesting stack  
		// gambit(~name) means use named topic 
		// gambit(~) means current topic we are within now
		// gambit (word) means topic with that keyword
		char* name = arguments[i];
		if ((oldIndex < responseIndex && result == NOPROBLEM_BIT) || result != NOPROBLEM_BIT || *name == 0) break; // generated an answer or failed or ran out
		oldIndex = responseIndex; // in case answer generated but topic claims failure
		if (trace & TRACE_TOPIC) Log(STDTRACETABLOG,(char*)"Respond trying %s\r\n",name);
		char* rule = NULL;
		char* dot = strchr(name,'.'); // tagged?
		if (dot) *dot = 0;
		int topic = FindTopicIDByName(name); // handles ~ and fully named topics
		if (dot) *dot = '.';
		int id = 0;
		if (dot) // find tagged rule
		{
			bool fulllabel = false;
			bool crosstopic = false;
			if (IsDigit(dot[1])) rule = GetRuleTag(topic,id,name); // numbered rule
			else rule = GetLabelledRule(topic,name,(char*)"",fulllabel,crosstopic,id,topic); // labelled rule
			if (!rule) return FAILRULE_BIT; // unable to find labelled rule 
		}

		// respond(PENDING) means from interesting stack  
		// respond(~name) means use named topic 
		// respond(~) means current topic we are within now
		// respond(word) means topic with that keyword
		oldIndex = responseIndex;
		result = FAILRULE_BIT;
	
   		if (!stricmp(name,(char*)"pending")) // pick topic from pending stack
		{
			unsigned int stack[MAX_TOPIC_STACK+1];
			memcpy(stack,pendingTopicList,pendingTopicIndex * sizeof(unsigned int)); // copy stack
			int oldPendingIndex = pendingTopicIndex;
			while (oldPendingIndex) // walk topics, most recent first
			{
				int xtopic = stack[--oldPendingIndex];
				char* xname = GetTopicName(currentTopicID); // just for debugging
				if (TopicInUse(topic) == -1) continue;
				currentTopicID = xtopic;
				ChangeDepth(1,(char*)"RespondCode");
				FunctionResult myresult = PerformTopic(0,buffer);
				ChangeDepth(-1,(char*)"RespondCode");
				if (myresult & RESULTBEYONDTOPIC)
				{
					result = myresult;
					break;
				}
				if (responseIndex > oldIndex) 
				{
					result = myresult;
					break; // we got something
				}
			}
		}

		 // do topic by name
		else if (*name == '~')
		{
			topic = FindTopicIDByName(name);
			if (!topic)  return FAILRULE_BIT; 
			if (GetTopicFlags(topic) & TOPIC_BLOCKED)  continue;
			int pushed =  PushTopic(topic); 
			if (pushed < 0) return FAILRULE_BIT;
			ChangeDepth(1,(char*)"RespondCode");
			result = PerformTopic(0,buffer,rule,id);
			ChangeDepth(-1,(char*)"RespondCode");
			if (pushed) PopTopic();

			AddKeep(currentRule);  //   do not allow responders to erase his nest call whether or not he succeeds  BUG ???
			result = (FunctionResult)(result & (-1 ^ (ENDTOPIC_BIT|ENDRULE_BIT))); // these are swallowed
		}
	
		// do topic by keyword
		else
		{
			WORDP D = FindWord(name);
			FACT* F = NULL;
			if (!D) result = NOPROBLEM_BIT;
			else  F = GetSubjectNondeadHead(D);
			while (F) // find topics word is a direct member of
			{
				if (F->verb == Mmember)
				{
					WORDP E = Meaning2Word(F->object);
					if (E->internalBits & TOPIC)
					{
						int xtopic = FindTopicIDByName(E->word);
						if (xtopic && !(GetTopicFlags(xtopic) & (TOPIC_BLOCKED|TOPIC_SYSTEM|TOPIC_NOGAMBITS)))
						{
 							int pushed = PushTopic(xtopic);
							if (pushed < 0) 
							{
								result = FAILRULE_BIT;
								break;
							}
							ChangeDepth(1,(char*)"RespondCode2");
							result = PerformTopic(0,buffer);
							ChangeDepth(-1,(char*)"RespondCode2");
							if (pushed) PopTopic();
							if (result & RESULTBEYONDTOPIC) break;
							if (responseIndex > oldIndex)  
							{
								result = NOPROBLEM_BIT;
								break;
							}
						}
					}
				}
				F = GetSubjectNondeadNext(F);
			} 

			AddKeep(currentRule);  //   do not allow responders to erase his nest call whether or not he succeeds  BUG ???
			result = (FunctionResult)(result & (-1 ^ (ENDTOPIC_BIT|ENDRULE_BIT))); // these are swallowed
		}
		currentTopicID = oldCurrentTopic;
		if (result & RESULTBEYONDTOPIC) break;
	}
	currentReuseID = oldreuseid;
	currentReuseTopic = oldreusetopic;
	if (fail && responseIndex <= oldIndex && result == NOPROBLEM_BIT)  result = FAILRULE_BIT; // report failure
	return result;
}

void ResetReuseSafety()
{
	memset(reuseSafety,0,sizeof(reuseSafety));
	memset(reuseSafetyCount,0,sizeof(reuseSafetyCount));
	reuseIndex = 0;
}

FunctionResult RegularReuse(int topic, int id, char* rule,char* buffer,char* arg3,bool crosstopic)
{
	bool found = false;
	int locator = 0;
	for (locator = 0; locator < reuseIndex; ++locator)
	{
		if (reuseSafety[locator] == rule) 
		{
			found = true;
			if (++reuseSafetyCount[locator] > 10)
			{
				char c = rule[30];
				rule[30] = 0;
				ReportBug((char*)"Recursive reuse %s",rule);
				rule[30] = c;
				return FAILRULE_BIT;
			}
			else break;
		}
	}
	if (!found)
	{
		if (reuseIndex == (MAX_REUSE_SAFETY-1)) reuseIndex = 0;
		locator = reuseIndex++;
		reuseSafetyCount[locator] = 1;
		reuseSafety[locator] = rule;
	}

	int oldreuseid = currentReuseID;
	unsigned int oldreusetopic = currentReuseTopic;

	currentReuseID = currentRuleID; // LOCAL reuse
	currentReuseTopic = currentRuleTopic;
	
	// execute rule 
	SAVEOLDCONTEXT()


	currentRule = rule;
	currentRuleID = id;
	currentRuleTopic = currentTopicID = topic;
	
	unsigned int oldTrace = EstablishTopicTrace();
	char* locals = GetTopicLocals(currentTopicID);

	int holdindex = responseIndex;
	FunctionResult result = FAILRULE_BIT;

	bool failed = false;

	ChangeDepth(1,(char*)"reuse");
	unsigned int oldTopicDisplay = currentTopicDisplay;
	currentTopicDisplay = currentTopicID;
	if (locals && currentTopicDisplay != oldTopicDisplay && !InitDisplay(locals)) failed = true; 

	if (!failed) result = ProcessRuleOutput(currentRule,currentRuleID,buffer); 
	
	if (locals && currentTopicDisplay != oldTopicDisplay && !failed) RestoreDisplay(inverseStringDepth[globalDepth],locals);
	currentTopicDisplay = oldTopicDisplay;
	ChangeDepth(-1,(char*)"reuse");

	if (crosstopic && responseIndex > holdindex) AddPendingTopic(topic); // restore caller topic as interesting
	
	RESTOREOLDCONTEXT()
	currentReuseID = oldreuseid;
	currentReuseTopic = oldreusetopic;

	trace = (modifiedTrace) ? modifiedTraceVal : oldTrace;

	if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACETABLOG,(char*)""); //   restore index from lower level
	if (!result && holdindex == responseIndex && !stricmp(arg3,(char*)"FAIL")) return FAILRULE_BIT; // user wants notification of failure
	if (--reuseSafetyCount[locator] == 0) // decrement use count, no need to track any more if goes to 0
	{
		reuseSafetyCount[locator] = reuseSafetyCount[--reuseIndex];
		reuseSafety[locator] = reuseSafety[reuseIndex];
	}
	return result;
}

static FunctionResult ReuseCode(char* buffer) 
{ 
	int id = 0;
	char* arg1 = ARGUMENT(1); // label of rule or topic.label
	if (!*arg1) return FAILRULE_BIT;

	int topic = currentTopicID;
	bool fulllabel = false;
	bool crosstopic = false;
	char* arg2 = ARGUMENT(2); // optional- if there not allowed to use erased rules
	char* arg3 = ARGUMENT(3); // possible fail value
	if (!stricmp(arg2,(char*)"FAIL")) // make it 3rd argument if it exists
	{
		char* val = ARGUMENT(2);
		ARGUMENT(2) = ARGUMENT(3);
		ARGUMENT(3) = val;
	}

	char* rule;
	char* dot = strchr(arg1,'.');
	if (dot && IsDigit(dot[1])) rule = GetRuleTag(topic,id,arg1);
	else rule = GetLabelledRule(topic,arg1,arg2,fulllabel,crosstopic,id,currentTopicID);
	if (!rule) return FAILRULE_BIT; // unable to find labelled rule that is available 
	
	return RegularReuse(topic,id,rule,buffer,arg3,crosstopic);
}

static FunctionResult AvailableCode(char* buffer) 
{ 
	int id = 0;
	char* arg1 = ARGUMENT(1); // label of rule or topic.label
	if (!*arg1) return FAILRULE_BIT;

	int topic = currentTopicID;
	bool fulllabel = false;
	bool crosstopic = false;
	char* rule;
	char* dot = strchr(arg1,'.');
	if (dot && IsDigit(dot[1])) rule = GetRuleTag(topic,id,arg1);
	else rule = GetLabelledRule(topic,arg1,(char*)"",fulllabel,crosstopic,id,currentTopicID);
	if (!rule) return FAILRULE_BIT; // unable to find labelled rule 
	unsigned int result = UsableRule(topic,id);
	if (!result && !stricmp(ARGUMENT(2),(char*)"FAIL")) return FAILRULE_BIT; // user wants notification of failure
	sprintf(buffer,(char*)"%d",result);
	return NOPROBLEM_BIT;
}

static FunctionResult SetRejoinderCode(char* buffer)
{
	if (planning) return NOPROBLEM_BIT; // canot rejoinder inside a plan
	bool input = false;
	char* tag = ARGUMENT(1); // kind of rejoinder
	char* arg2 = ARGUMENT(2); // to where
	if (!stricmp(arg2,(char*)"null")) arg2 = ""; // clear to null
	bool copy = false;
	if (!*arg2){;}
	else if (!stricmp(tag,(char*)"input")) input = true;
	else if (!stricmp(tag,(char*)"output")) {;}
	else if (!stricmp(tag,(char*)"copy")) copy = true; // keep as is
	else return FAILRULE_BIT;
	if (*arg2) tag = arg2;

	if (!stricmp(tag,(char*)"copy") || !stricmp(tag,(char*)"output")) // disable rejoinder
	{
		outputRejoinderRuleID = NO_REJOINDER;
		return NOPROBLEM_BIT;
	}
	if (!stricmp(tag,(char*)"input")) // disable rejoinder
	{
		inputRejoinderRuleID = NO_REJOINDER;
		return NOPROBLEM_BIT;
	}

	int topic = currentTopicID;
	bool fulllabel = false;
	bool crosstopic = false;
	char* rule;
	char* dot = strchr(tag,'.');
	int id;
	if (dot && IsDigit(dot[1])) rule = GetRuleTag(topic,id,tag);
	else rule = GetLabelledRule(topic,tag,(char*)"",fulllabel,crosstopic,id,currentTopicID);
	if (!rule) return FAILRULE_BIT; // unable to find labelled rule 
	
	char label[MAX_WORD_SIZE];
	*label = '(';
	GetLabel(rule,label+1);
	strcat(label,")");
	if (*label == ')') *label = 0;

	// when you give us a rule, we set it to the thing AFTER it (1st rejoinder)
	// confirm the request was feasible
	if (!copy) // copy already verified it
	{
		char level = TopLevelRule(rule)   ? 'a' :  (*rule+1); // default rejoinder level
		char* ptr = FindNextRule(NEXTRULE,rule,id);
		if (!ptr || *ptr != level) return FAILRULE_BIT;		// not found
	}

	if (input)
	{
		inputRejoinderRuleID = id; 
 		inputRejoinderTopic = topic;
		if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACELOG,(char*)"  **set input rejoinder at %s.%d.%d%s\r\n",GetTopicName(topic),TOPLEVELID(id),REJOINDERID(id),label);
	}
	else
	{
		outputRejoinderRuleID = id; 
 		outputRejoinderTopic = topic;
		if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACELOG,(char*)"  **set output rejoinder at %s.%d.%d%s\r\n",GetTopicName(topic),TOPLEVELID(id),REJOINDERID(id),label);
	}
	return NOPROBLEM_BIT;
}

static FunctionResult TopicFlagsCode(char* buffer)
{
	sprintf(buffer,(char*)"%d",GetTopicFlags(FindTopicIDByName(ARGUMENT(1))));
	return NOPROBLEM_BIT;
}

//////////////////////////////////////////////////////////
/// TOPIC LISTS
//////////////////////////////////////////////////////////

static FunctionResult GetTopicsWithGambitsCode(char* buffer)
{ 
	unsigned int store = (impliedSet == ALREADY_HANDLED) ? 0 : impliedSet;
	SET_FACTSET_COUNT(store,0);
	*buffer = 0;

    for (int topicid = 1; topicid <= numberOfTopics; ++topicid) 
    {
        if (topicid == currentTopicID || HasGambits(topicid) <= 0) continue;
		if (GetTopicFlags(topicid) & TOPIC_NOGAMBITS) continue;	// dont use this
		char* name = GetTopicName(topicid);
		if (!*name) continue; // deactivated by faketopic
		MEANING T = MakeMeaning(StoreWord(name));
		FACT* F = CreateFact(T, MgambitTopics,T,FACTTRANSIENT|FACTDUPLICATE);
		AddFact(store,F);
	}
	if (impliedSet == ALREADY_HANDLED && FACTSET_COUNT(store) == 0) return FAILRULE_BIT;
	impliedSet = impliedWild = ALREADY_HANDLED;	
	currentFact = NULL;
	return NOPROBLEM_BIT;
}

static int OrderTopics(unsigned short topicList[MAX_TOPIC_KEYS],unsigned int matches[MAX_TOPIC_KEYS]) // find other topics that use keywords
{
	bool newpass = topicList[1] != 0;
	unsigned int max = 2;
    unsigned int index = 0;
    int i;
	char currentTopic[MAX_WORD_SIZE];
	GetActiveTopicName(currentTopic); // current topic, not system or nostay.
	int baseid = FindTopicIDByName(currentTopic);

	//  value on each topic
    for (i = 1; i <= numberOfTopics; ++i) // value 0 means we havent computed it yet. Value 1 means it has been erased.
    {
		if (i == baseid || BlockedBotAccess(i)) continue;

        char* name = GetTopicName(i);
		if (!*name) continue; // hidden topic
	    unsigned int val = topicList[i];
        if (!val) //   compute match value
        {
            char word[MAX_WORD_SIZE];
            strcpy(word,name);
			char* dot = strchr(word+1,DUPLICATETOPICSEPARATOR);
			if (dot) *dot = 0;	// use base name of the topic, not topic family name.
            WORDP D = FindWord(word); //   go look up the ~word for it
            if (!D) continue; // topic not found -- shouldnt happen

			// Note- once we have found an initial match for a topic name, we dont want to match that name again...
			// E.g., we have a specific topic for a bot, and later a general one that matches all bots. We dont want that later one processed.
  			if (D->inferMark == inferMark) continue;	// already processed a topic of this name
			D->inferMark = inferMark;

            //   look at references for this topic
            int start = -1;
			int startPosition = 0;
			int endPosition = 0;
            while (GetIthSpot(D,++start, startPosition,endPosition)) // find matches in sentence
            {
                // value of match of this topic in this sentence
                for (int k = startPosition; k <= endPosition; ++k)
                {
					if (trace & TRACE_PATTERN && CheckTopicTrace()) Log(STDTRACELOG,(char*)"%s->%s ",wordStarts[k],word);
                    val += 10 + strlen(wordStarts[k]);   // each hit gets credit 10 and length of word as subcredit
					if (!stricmp(wordStarts[k],word+1) || (wordCanonical[k] && !stricmp(wordCanonical[k],word+1))) val += 20; //  exact hit on topic name
                }
				if (endPosition < startPosition) // phrase subcomponent
				{
					if (trace & TRACE_PATTERN && CheckTopicTrace())  Log(STDTRACELOG,(char*)"%s->%s",wordStarts[startPosition],word);
					val += 10;
  				}
            }

			//   Priority modifiers

			char priority = ' ';
			if (GetTopicFlags(i) & TOPIC_PRIORITY && val)
			{
				priority = '+';
				val  *= 3; //  raise its value
			}
  			else if (GetTopicFlags(i) & TOPIC_LOWPRIORITY && val)
			{
				priority = '-';
				val  /= 3; // lower its value
			}

			topicList[i] = (unsigned short)(val + 1); //  1 means we did compute it, beyond that is value
			if (trace & TRACE_PATTERN && val > 1 && CheckTopicTrace()) Log(STDTRACELOG,(char*)"%c(%d) ",priority,topicList[i]);
		} //   close if

        if (val >= max) //  find all best
        {
            if (val > max) // new high value
            {
                max = val;
                index = 0;
            }
            matches[++index] = i;
        }
    }
	if (trace & TRACE_PATTERN && newpass  && CheckTopicTrace()) Log(STDTRACELOG,(char*)"\r\n");
	matches[0] = max;
    return index;
}

FunctionResult KeywordTopicsCode(char* buffer)
{	//   find  topics best matching his input words - never FAILS but can return 0 items stored
    unsigned short topicList[MAX_TOPIC_KEYS];
    memset(topicList,0,MAX_TOPIC_KEYS * sizeof(short));
	
	int set = (impliedSet == ALREADY_HANDLED) ? 0 : impliedSet;
	SET_FACTSET_COUNT(set,0);
	
	bool onlyGambits =  (!stricmp(ARGUMENT(1),(char*)"gambit")); 

    //   now consider topics in priority order
	unsigned int index;
    unsigned int matches[MAX_TOPIC_KEYS];
	NextInferMark();
	while ((index = OrderTopics(topicList,matches))) //   finds all at this level. 1st call evals topics. other calls just retrieve.
    {
        //   see if equally valued topics found are feasible, if so, return one chosen at random
        while (index) // items are 1-based
        {
            unsigned int which = random(index) + 1; 
            int topic = matches[which];
            topicList[topic] = 1; 
            matches[which] = matches[index--]; // swap equally valued end back to fill in position

			unsigned int flags = GetTopicFlags(topic);
			if (onlyGambits && (flags & TOPIC_SYSTEM || !HasGambits(topic))) continue;
				
			char word[MAX_WORD_SIZE];
			strcpy(word,GetTopicName(topic,true));
			if (impliedSet == ALREADY_HANDLED) // just want one
			{
				strcpy(buffer,word);
				break;
			}

			char value[100];
			sprintf(value,(char*)"%d",matches[0]);
			MEANING M = MakeMeaning(StoreWord(word));
			AddFact(set,CreateFact(M,Mkeywordtopics,MakeMeaning(StoreWord(value)),FACTTRANSIENT|FACTDUPLICATE));
        }   
    }
	if (impliedSet == ALREADY_HANDLED && FACTSET_COUNT(set) == 0) return FAILRULE_BIT;
	impliedSet = ALREADY_HANDLED;
	currentFact = NULL;
    return NOPROBLEM_BIT;
}

static FunctionResult PendingTopicsCode(char* buffer)
{
	int set = GetSetID(ARGUMENT(1));
	if (set == ILLEGAL_FACTSET) set = impliedSet;
	if (set == ILLEGAL_FACTSET) return FAILRULE_BIT;
	PendingTopics(set);
	impliedSet = ILLEGAL_FACTSET;
	return NOPROBLEM_BIT;
}

static FunctionResult QueryTopicsCode(char* buffer)
{
	if (impliedSet == ALREADY_HANDLED) // not in assignment
	{
		QueryTopicsOf(ARGUMENT(1),0,NULL); 
		return (FACTSET_COUNT(0)) ? NOPROBLEM_BIT : FAILRULE_BIT;
	}
	return QueryTopicsOf(ARGUMENT(1),impliedSet,NULL); 
}

//////////////////////////////////////////////////////////
/// MARKINGS
//////////////////////////////////////////////////////////

static FunctionResult MarkCode(char* buffer) 
{  
	// argument1 is a word or ~set or missing entirely
	// mark()  flip off generic unmarks 
	// argument 2 is a location designator or * or missing entirely
	// mark(word) enables mark at location 1 
	// mark(word _xxx) enable word mark at start location of _xxx variable 
	// mark(word  1) enables mark at specified location if within range of input
	// mark(* whatever) turns on any turned off global marking at the range of the given location
	char* ptr = ARGUMENT(1);
	
	FunctionResult result;
	char word[MAX_WORD_SIZE];
	ptr = ReadShortCommandArg(ptr,word,result); // what is being marked
	if (result & ENDCODES) return result;

	if (!*word) // mark() remove all generic unmarks
	{
		if (!oldunmarked[0]) // cache current disables
		{
			memcpy(oldunmarked,unmarked,MAX_SENTENCE_LENGTH);
			oldunmarked[0] = 1;
		}
		memset(unmarked,0,MAX_SENTENCE_LENGTH); // clear all mark suppression
		return NOPROBLEM_BIT;
	}
	
	char word1[MAX_BUFFER_SIZE];
	if (*ptr == '^')
	{
		ptr = GetPossibleFunctionArgument(ptr,word1); // get the request
		char word2[MAX_WORD_SIZE];
		if (!IsDigit(*word1) &&  *word1 != '_') 
		{
			strcpy(word2,word1);
			ReadCommandArg(word2,word1,result,0,MAX_WORD_SIZE);
		}
	}
	if (IsDigit(*ptr) || *ptr == '_') ptr = ReadCompiledWord(ptr,word1);  // the locator, leave it unevaled as number or match var
	else ptr = ReadShortCommandArg(ptr,word1,result); // evaluate the locator as a number presumably
	if (*word1 == USERVAR_PREFIX) strcpy(word1,GetUserVariable(word1));

	int startPosition;
	int endPosition;
	if (!*word1 || *word1 == ')') startPosition = endPosition = 1; // default mark  (ran out or hit end paren of call
	else if (IsDigit(*word1)) 
	{
		int val = atoi(word1);
		startPosition = val & 0x0000ffff;
		endPosition = val >> 16;
		if (endPosition == 0) endPosition = startPosition; // SHARED
	}
	else if (*word1 == '_') //  wildcard position designator
	{
		startPosition = wildcardPosition[GetWildcardID(word1)] & 0x0000ffff; // the match location
		endPosition = wildcardPosition[GetWildcardID(word1)] >> 16; 
	}
	else return FAILRULE_BIT;

	if (startPosition < 1) endPosition = startPosition = 1;
	if (startPosition > wordCount)  endPosition = startPosition = wordCount;
	if (endPosition > wordCount)  endPosition = wordCount;

	if (*word == '*') // enable all - mark (* _0)
	{
		if (trace & TRACE_OUTPUT) Log(STDTRACELOG,(char*)"mark * %d...%d words: (char*)",startPosition,endPosition);
		for (int i = startPosition; i <= endPosition; ++i) 
		{
			if (showMark) Log(ECHOSTDTRACELOG,(char*)"Mark * $d(%s_: \r\n",i,wordStarts[i]);
			if (trace & TRACE_FLOW) Log(STDTRACELOG,(char*)"%s ",wordStarts[i]);
			unmarked[i] = 0;
		}
		if (trace & TRACE_FLOW) Log(STDTRACELOG,(char*)"\r\n");
		return NOPROBLEM_BIT;
	}

	// Mark specific thing 
	WORDP D = StoreWord(word);
	MEANING M = MakeMeaning(D);
	if (*D->word != '~') Add2ConceptTopicList(concepts, D,startPosition,endPosition,true); // add ordinary word to concept list directly as WordHit will not store anything but concepts
	NextInferMark();
	if (showMark || (trace & TRACE_PREPARE)) Log(ECHOSTDTRACELOG,(char*)"Mark %s: \r\n",D->word);
	if (trace & TRACE_OUTPUT) Log(STDTRACELOG,(char*)"mark all @word %d ",D->word);
	MarkFacts(M,startPosition,endPosition);
	if (showMark) Log(ECHOSTDTRACELOG,(char*)"------\r\n");

	return NOPROBLEM_BIT;
}

static FunctionResult MarkedCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	if (*ARGUMENT(1) == USERVAR_PREFIX)  // indirect thru variable
	{
		char* at = GetUserVariable(ARGUMENT(1));
		if (at) arg1 = at;
	}

	WORDP D = FindWord(arg1);
	if (!D) return FAILRULE_BIT;
	int start,end;
	if (!GetNextSpot(D,0,start,end)) return FAILRULE_BIT;
	strcpy(buffer,  (char*) "1" );
	return NOPROBLEM_BIT;
}

static FunctionResult PositionCode(char* buffer)
{
	char* ptr = ARGUMENT(1);
	FunctionResult result;
	char word[MAX_WORD_SIZE];
	ptr = ReadShortCommandArg(ptr,word,result); // start or end
	if (result & ENDCODES) return result;
	char word1[MAX_BUFFER_SIZE];
	ptr = GetPossibleFunctionArgument(ptr,word1);  // the _ var
	if (*word1 == '\'') memmove(word1,word1+1,strlen(word1));
	if (*word1 == '_') //  wildcard position designator
	{
		if (!stricmp(word,(char*)"start")) sprintf(buffer,(char*)"%d",WILDCARD_START(wildcardPosition[GetWildcardID(word1)]));  // the match location
		else if (!stricmp(word,(char*)"end")) sprintf(buffer,(char*)"%d", WILDCARD_END(wildcardPosition[GetWildcardID(word1)]));
		else if (!stricmp(word,(char*)"both")) sprintf(buffer,(char*)"%d", wildcardPosition[GetWildcardID(word1)]);
		else return FAILRULE_BIT;
	}
	else return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

static FunctionResult SetPositionCode(char* buffer)
{
	char* ptr = ARGUMENT(1);
	char word[MAX_WORD_SIZE];

	if (*ptr == '_') // set match var position
	{
		ptr = ReadCompiledWord(ptr,word);
		int n = GetWildcardID(word);
		if (n < 0) return FAILRULE_BIT;

		char startw[MAX_WORD_SIZE];
		FunctionResult result;
		ptr = ReadShortCommandArg(ptr,startw,result); // what is being marked
		if (result != NOPROBLEM_BIT) return result;
		int start = atoi(startw);
		if (start <= 0 || start >= (wordCount+1)) return FAILRULE_BIT;
		char endw[MAX_WORD_SIZE];
		ptr = ReadShortCommandArg(ptr,endw,result); // what is being marked
		if (result != NOPROBLEM_BIT) return result;
		int end = atoi(endw);

		if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACETABLOG, "_%d = %d...%d ",n,start,end);

		if (end <= 0 || end >= (wordCount+1)) return FAILRULE_BIT;
		wildcardPosition[n] = start | (end << 16);
	}
	else return FAILRULE_BIT;// set GLOBAL position -- unused at present
	return NOPROBLEM_BIT;
}

static FunctionResult CapitalizedCode(char* buffer)
{
	if (IsDigit(*ARGUMENT(1)))
	{
		int n = atoi(ARGUMENT(1));
		if (n == 0 || n > wordCount) return FAILRULE_BIT;
		strcpy(buffer,(capState[n]) ? (char*) "1" : (char*) "0");
	}
	else if (IsAlphaUTF8(*ARGUMENT(1))) strcpy(buffer,(IsUpperCase(*ARGUMENT(1))) ? (char*) "1" : (char*) "0");
	else return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

static FunctionResult RoleCode(char* buffer)
{
	int n = 0;
	char* arg = ARGUMENT(1);
	if (*arg == '\'') memmove(arg,arg+1,strlen(arg));
	if (IsDigit(*arg))
	{
		n = atoi(arg);
		if (n == 0 || n > wordCount) return FAILRULE_BIT;
	}
	else if (*arg == '_') n = WildStartPosition(arg);
	else if (*arg == USERVAR_PREFIX) n = atoi(GetUserVariable(arg));
	else if (*arg == '^') 
	{
		char word[MAX_WORD_SIZE];
		ReadArgument(arg,word);
		n = atoi(word);
	}
	else return FAILRULE_BIT;
	sprintf(buffer,(char*)"%u", (unsigned int)roles[n]);
	return NOPROBLEM_BIT;
}

static char* tokenValues[] = {
	(char*)"DO_ESSENTIALS",(char*)"DO_SUBSTITUTES",(char*)"DO_CONTRACTIONS",(char*)"DO_INTERJECTIONS",(char*)"DO_BRITISH", "DO_SPELLING", "DO_TEXTING", "DO_NOISE",
	(char*)"DO_PRIVATE",(char*)"DO_NUMBER_MERGE",(char*)"DO_PROPERNAME_MERGE",(char*)"DO_SPELLCHECK",(char*)"DO_INTERJECTION_SPLITTING",(char*)"DO_POSTAG/PRESENT",(char*)"DO_PARSE/PAST",(char*)"NO_IMPERATIVE/FUTURE",
	(char*)"NO_WITHIN/PRESENT_PERFECT",(char*)"DO_DATE_MERGE/CONTINUOUS",(char*)"NO_SENTENCE_END/PERFECT",(char*)"NO_INFER_QUESTION/PASSIVE",
	(char*)"NO_HYPHEN_END",(char*)"NO_COLON_END",(char*)"NO_SEMICOLON_END",(char*)"STRICT_CASING",
	(char*)"ONLY_LOWERCASE",(char*)"TOKEN_AS_IS",(char*)"SPLIT_QUOTE",(char*)"LEAVE_QUOTE",(char*)"UNTOUCHED_INPUT",(char*)"QUESTIONMARK",(char*)"EXCLAMATIONMARK",(char*)"PERIODMARK",
	(char*)"USERINPUT",(char*)"COMMANDMARK",(char*)"IMPLIED_YOU",(char*)"FOREIGN_TOKENS",(char*)"FAULTY_PARSE",(char*)"QUOTATION",(char*)"NOT_SENTENCE"
};

static FunctionResult DecodeInputTokenCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	int64 n;
	ReadInt64(arg1,n);
	uint64 bit = 1;
	int index = 0;
	while (n) 
	{
		if (bit & n) 
		{
			strcat(buffer,tokenValues[index]);
			strcat(buffer,(char*)", ");
			n ^= bit;
		}
		bit <<= 1;
		++index;
	}
	return NOPROBLEM_BIT;
}

static FunctionResult DecodePosCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	char* arg = ARGUMENT(2);
	int64 n;
	ReadInt64(arg,n);
	if (!stricmp(arg1,(char*)"pos")) DecodeTag(buffer,n, 0,0);
	else strcpy(buffer,GetRole(n));
	return NOPROBLEM_BIT;
}

static FunctionResult PartOfSpeechCode(char* buffer)
{
	int n = 0;
	char* arg = ARGUMENT(1);
	if (*arg == '\'') memmove(arg,arg+1,strlen(arg));
	if (IsDigit(*arg))
	{
		n = atoi(arg);
		if (n == 0 || n > wordCount) return FAILRULE_BIT;
		strcpy(buffer,(capState[n]) ? (char*) "1" : (char*) "0");
	}
	else if (*arg == '_')  n = WildStartPosition(arg);
	else if (*arg == '^') 
	{
		char word[MAX_WORD_SIZE];
		ReadArgument(arg,word);
		n = atoi(word);
	}
	else if (*arg == USERVAR_PREFIX) n = atoi(GetUserVariable(arg));
	else return FAILRULE_BIT;
	uint64 pos = finalPosValues[n];
	if (pos & (AUX_VERB | ADJECTIVE_PARTICIPLE )) pos |= allOriginalWordBits[n] & VERB_BITS; // supllementatal data
	else if (pos &  ADJECTIVE_NORMAL && allOriginalWordBits[n] & ADJECTIVE_PARTICIPLE) pos |= allOriginalWordBits[n] & VERB_BITS; // supllementatal data
	else if (pos & ADJECTIVE_NOUN) pos |= allOriginalWordBits[n] & NORMAL_NOUN_BITS;

#ifdef WIN32
	sprintf(buffer,(char*)"%I64d",pos); 
#else
	sprintf(buffer,(char*)"%lld",pos); 
#endif
	return NOPROBLEM_BIT;
}

static FunctionResult KeepHistoryCode(char* buffer)
{
	int count = atoi(ARGUMENT(2));
	if (count >= (MAX_USED - 1)) count = MAX_USED - 1; 
	if (!stricmp(ARGUMENT(1),(char*)"BOT"))
	{
		if (count == 0) *chatbotSaid[0] = 0;
		if (count < chatbotSaidIndex)  chatbotSaidIndex = count;
	}
	if (!stricmp(ARGUMENT(1),(char*)"USER"))
	{
		if (count == 0)  *humanSaid[0] = 0;
		if (count < humanSaidIndex) humanSaidIndex = count;
	}

	return NOPROBLEM_BIT;
}

static FunctionResult SetTagCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	int n = atoi(arg1);
	if (n < 1 || n > wordCount) return FAILRULE_BIT;
	char* arg2 = ARGUMENT(2);
	if (*arg2 != '~') return FAILRULE_BIT;
	wordTag[n] = StoreWord(arg2);
	return NOPROBLEM_BIT;
}

static FunctionResult SetRoleCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	int n = atoi(arg1);
	if (n < 1 || n > wordCount) return FAILRULE_BIT;
	char* arg2 = ARGUMENT(2);
	if (*arg2 != '~') return FAILRULE_BIT;
	wordRole[n] = StoreWord(arg2);
	return NOPROBLEM_BIT;
}

static FunctionResult SetOriginalCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	int n = atoi(arg1);
	if (n < 1 || n > wordCount) return FAILRULE_BIT;
	char* arg2 = ARGUMENT(2);
	wordStarts[n] = StoreWord(arg2)->word;
	return NOPROBLEM_BIT;
}

static FunctionResult SetCanonCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	int n = atoi(arg1);
	if (n < 1 || n > wordCount) return FAILRULE_BIT;
	char* arg2 = ARGUMENT(2);
	WORDP D = StoreWord(arg2);
	wordCanonical[n] = D->word;
	if (!IsUpperCase(*wordCanonical[n]))
		canonicalLower[n] = D;
    else canonicalUpper[n] = D; 
		
	return NOPROBLEM_BIT;
}

static FunctionResult UnmarkCode(char* buffer)
{
	// unmark() // disable global unmarks
	// unmark(*) // global unmark all words individually
	// unmark(* 4)	 // global unmark this location
	// unmark(* _location) // global unmark range
	// unmark(word 4)
	// unmark(word _location)
	// unmark(word all)

	char* ptr = ARGUMENT(1);
	char word[MAX_WORD_SIZE];
	FunctionResult result;
	ptr = ReadShortCommandArg(ptr,word,result);// set
	if (result & ENDCODES) return result;
	if (matching) clearUnmarks = true;
	
	if (!*word) // unmark() reenables generic unmarking
	{
		if (oldunmarked[0]) // merge state back if have cached
		{
			memcpy(unmarked,oldunmarked,MAX_SENTENCE_LENGTH);
			oldunmarked[0] = 0;
		}
		return NOPROBLEM_BIT;
	}
	
	// get location
	char word1[MAX_WORD_SIZE];
	ptr = ReadCompiledWord(ptr,word1);  // the _data
	int startPosition = wordCount;
	int endPosition = 1;
	if (*word1 == '^') GetPossibleFunctionArgument(ptr,word1); // pass thru or convert
	if (*word1 == USERVAR_PREFIX) strcpy(word1,GetUserVariable(word1));

	if (!*word1) 
	{
		if (*word == '*') // unmark(*)
		{
			startPosition = startSentence;
			endPosition = endSentence;
		}
		else startPosition = endPosition = 1;
	}
	else if (IsDigit(*word1)) 
	{
		int val = atoi(word1);
		startPosition = val & 0x0000ffff;
		endPosition = val >> 16;
	}
	else if (*word1 == '_') 
	{
		startPosition = WILDCARD_START(wildcardPosition[GetWildcardID(word1)]); // the match location
		endPosition = WILDCARD_END(wildcardPosition[GetWildcardID(word1)]);
	}
	else if (!stricmp(word1,(char*)"all")) // remove ALL references anywhere of this
	{
		WORDP D = FindWord(word); //   set or word to unmark
		if (!D) return FAILRULE_BIT;
		ClearWordWhere(D,-1);
		return NOPROBLEM_BIT;
	}
 	else  return FAILRULE_BIT;
	if (!startPosition || startPosition > wordCount) return NOPROBLEM_BIT;	// fail silently
	if (*word == '*') // set unmark EVERYTHING in range 
	{
		if (trace & TRACE_OUTPUT) Log(STDTRACELOG,(char*)"unmark * %d...%d words: ",startPosition,endPosition);
		for (int i = startPosition; i <= endPosition; ++i) 
		{
			if (trace & TRACE_OUTPUT) Log(STDTRACELOG,(char*)"%s ",wordStarts[i]);
			unmarked[i] = 1;
		}
		if (trace & TRACE_OUTPUT) Log(STDTRACELOG,(char*)"\r\n");
	}
	else
	{
		WORDP D = FindWord(word); //   set or word to unmark at specific location
		if (D) 
		{
			RemoveMatchValue(D,startPosition);
			RemoveConceptTopic(concepts,D,startPosition);
			RemoveConceptTopic(topics,D,startPosition);		
		}
	}
	return NOPROBLEM_BIT;
}

//////////////////////////////////////////////////////////
/// INPUT ROUTINES
//////////////////////////////////////////////////////////

static FunctionResult OriginalCode(char* buffer)
{
	char* arg = ARGUMENT(1);
	if (*arg == '\'') ++arg;

	if (*arg == '^') strcpy(arg,callArgumentList[atoi(arg+1)+fnVarBase]);
	else if (*arg == USERVAR_PREFIX) strcpy(arg,GetUserVariable(arg));
	if (*arg != '_') return FAILRULE_BIT;

	int x = GetWildcardID(arg);
	if (x == ILLEGAL_MATCHVARIABLE) return FAILRULE_BIT;
	int start = WILDCARD_START(wildcardPosition[x]);
	int end = WILDCARD_END(wildcardPosition[x]);
	start = derivationIndex[start] >> 8; // from here
	end = derivationIndex[end] & 0x00ff;  // to here but not including here  The end may be beyond wordCount if words have been deducted by now
	if (start == 0) return NOPROBLEM_BIT;	// there is nothing here
	*buffer = 0;
	for (int i = start; i <= end; ++i)
	{
		if (!derivationSentence[i]) return NOPROBLEM_BIT; // in case sentence is empty
		strcat(buffer,derivationSentence[i]);
		if ( i != end) strcat(buffer," ");
	}
	return NOPROBLEM_BIT;
}

static FunctionResult ActualInputRangeCode(char* buffer) 
{ // for range of original input words, get the actual input range
	int start = atoi(ARGUMENT(1));
	int end = atoi(ARGUMENT(2));
	if (start < 0 || start > derivationLength) return FAILRULE_BIT;
	if (end < 0 || end > derivationLength || end < start) return FAILRULE_BIT;

	// set derivation data on original words of user before we do substitution
	int first = 0;
	int last = 0;
	for (int i = 1; i <= wordCount; ++i) // leaf thru actual words to find ranges covered
	{
		// actual word is from this range in original
		int a = derivationIndex[i] >> 8; // from here
		int b  = (derivationIndex[i] & 0x00ff);  // to here including here  The end may be beyond wordCount if words have been deducted by now
		if (a >= start && a <= end && !first) first = i; // starting actual word in range
		if (b >= start && b <= end) last = i; // maximal  ending actual word in range
	}
	sprintf(buffer,"%d",(first << 8) + last);
	return NOPROBLEM_BIT;
}

static FunctionResult OriginalInputRangeCode(char* buffer) 
{ // for range of actual input words, get the original input range
	int start = atoi(ARGUMENT(1));
	int end = atoi(ARGUMENT(2));
	if (start < 0 || start > wordCount) return FAILRULE_BIT;
	if (end < 0 || end > wordCount || end < start) return FAILRULE_BIT;

	// set derivation data on original words of user before we do substitution
	int first = 0;
	int last = 0;
	// actual word is from this range in original
	int a = derivationIndex[start] >> 8; // from here
	int b  = (derivationIndex[end] & 0x00ff);  // to here including here  The end may be beyond wordCount if words have been deducted by now
	sprintf(buffer,"%d",(a << 8) + b);
	return NOPROBLEM_BIT;
}

static FunctionResult InputCode(char* buffer) 
{      // when supplying multiple sentences, must do them in last first order
	if (inputCounter++ > 5) 
		return FAILRULE_BIT;// limit per sentence reply
	if (totalCounter++ > 15) 
		return FAILRULE_BIT; // limit per input from user

	if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACELOG,(char*)"\r\n");
	FunctionResult result;
	char* word = ARGUMENT(1);
	Output(word,buffer,result);
	if (strlen(buffer) >= (INPUT_BUFFER_SIZE - 100)) buffer[INPUT_BUFFER_SIZE-100] = 0;	// might be smaller buffer
	Convert2Blanks(buffer); // break apart underscored words

	// put possessives back together.
	char* at = buffer;
	while ((at = strstr(at,(char*)" '")))
	{
		if (buffer[1] == 's') memmove(at,at+1,strlen(at)); 
		if (buffer[1] == ' ' || buffer[1] == ',' || buffer[1] == '.' || buffer[1] == ';' || buffer[1] == '?' || buffer[1] == ':') memmove(at,at+1,strlen(at)); 
		++at;
	}

	if (!strcmp(lastInputSubstitution,buffer)) return FAILRULE_BIT; // same result as before, apparently looping

	if (showInput) Log(ECHOSTDTRACELOG,(char*)"^input: %s\r\n",buffer);
	else if (trace & TRACE_FLOW) Log(STDTRACELOG,(char*)"^input given: %s\r\n",buffer);
	AddInput(buffer);
	strcpy(lastInputSubstitution,buffer);
    *buffer = 0;
	return NOPROBLEM_BIT;
}

static FunctionResult RemoveTokenFlagsCode(char* buffer)
{
	int64 flags;
	ReadInt64(ARGUMENT(1),flags);
	tokenFlags &= -1 ^ flags;
	return NOPROBLEM_BIT;
}

static FunctionResult SetTokenFlagsCode(char* buffer)
{
	int64 flags;
	ReadInt64(ARGUMENT(1),flags);
	tokenFlags |= flags;
	return NOPROBLEM_BIT;
}

static FunctionResult SetWildcardIndexCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	int index = GetWildcardID(arg1);
	if (index == ILLEGAL_MATCHVARIABLE) return FAILRULE_BIT;
	SetWildCardIndexStart(index); // start here
	return NOPROBLEM_BIT;
}

//////////////////////////////////////////////////////////
/// NUMBER FUNCTIONS
//////////////////////////////////////////////////////////

static FunctionResult ComputeCode(char* buffer)
{
	int64 value = NOT_A_NUMBER;
	char* arg1 = ARGUMENT(1);
	char* op = ARGUMENT(2);
	char* arg2 = ARGUMENT(3);
	//   for long digits, move to float
	if (strlen(arg2) >= 11 || strlen(arg1) >= 11 || strchr(arg1,'.') || strchr(arg2,'.') || !stricmp(op,(char*)"divide") || !stricmp(op,(char*)"root") || !stricmp(op,(char*)"square_root") || !stricmp(op,(char*)"quotient") || *op == '/') //   float
	{
		float fvalue = (float) NOT_A_NUMBER;
		float number1 = (strchr(arg1,'.')) ? (float) atof(arg1) : (float)Convert2Integer(arg1);
		float number2 = (strchr(arg2,'.')) ? (float) atof(arg2) :  (float)Convert2Integer(arg2);
		//   we must test case insenstive because arg2 might be capitalized (like add and ADD for attention deficit disorder)
		if (*op == '+' || !stricmp(op,(char*)"plus") || !stricmp(op,(char*)"add")|| !stricmp(op,(char*)"and")) fvalue = number1 + number2; 
		else if (!stricmp(op,(char*)"minus") || !stricmp(op,(char*)"subtract")|| !stricmp(op,(char*)"deduct")|| !stricmp(op,(char*)"take away") || *op == '-' ) fvalue = number1 - number2;
		else if (!stricmp(op,(char*)"x") || !stricmp(op,(char*)"times") || !stricmp(op,(char*)"multiply") || *op == '*') fvalue = number1 * number2;
		else if (!stricmp(op,(char*)"divide") || !stricmp(op,(char*)"quotient") || *op == '/' ) 
		{
			if (number2 == 0) 
			{
				strcpy(buffer,(char*)"infinity");
				return NOPROBLEM_BIT;
			}
			else fvalue = number1 / number2;
		}
        else if (!stricmp(op,(char*)"remainder") || !stricmp(op,(char*)"modulo") || !stricmp(op,(char*)"mod") || *op == '%') 
		{
			ReportBug((char*)"illegal mod op in float")
			return FAILRULE_BIT;
		}
        else if (!stricmp(op,(char*)"random") )
		{
			ReportBug((char*)"illegal random op in float")
  			return FAILRULE_BIT;
		}
        else if (!stricmp(op,(char*)"root") || !stricmp(op,(char*)"square_root") ) fvalue = (float) sqrt(number1);  
        else if (!stricmp(op,(char*)"^") || !stricmp(op,(char*)"^^") ||!stricmp(op,(char*)"power") || !stricmp(op,(char*)"exponent")) 
        {
			int power = (int)Convert2Integer(arg2);
            if (power >= 1)
            {
				fvalue = number1;
				while (--power) fvalue *= number1;
			}
            else if (power == 0) fvalue = 1;
			else return FAILRULE_BIT;
		}
		if (fvalue != NOT_A_NUMBER) 
		{
			long x = (long) fvalue;

			if ((float)x == fvalue) 
			{
#ifdef WIN32
				sprintf(buffer,(char*)"%I64d",(long long int) x); 
#else
				sprintf(buffer,(char*)"%lld",(long long int) x); 
#endif
			}
			else sprintf(buffer,(char*)"%1.2f",fvalue);
		}
		else sprintf(buffer,(char*)"%s",(char*)" ?");
	}
	else //   integer
    {
		int64 value1 = Convert2Integer(arg1);
		int64 value2 = Convert2Integer(arg2);
		if (*op == '+' || !stricmp(op,(char*)"add")|| !stricmp(op,(char*)"and") || !stricmp(op,(char*)"plus")) value = value1 + value2;
		else if (*op == '-' || !stricmp(op,(char*)"deduct") || !stricmp(op,(char*)"minus") || !stricmp(op,(char*)"sub") || !stricmp(op,(char*)"subtract") || !stricmp(op,(char*)"take_away")) value = value1 - value2;
		else if (*op == '*' || !stricmp(op,(char*)"x") || !stricmp(op,(char*)"multiply") || !stricmp(op,(char*)"times")) value = value1 * value2;
		else if ( *op == '%' || !stricmp(op,(char*)"mod") || !stricmp(op,(char*)"modulo") || !stricmp(op,(char*)"remainder")) value = value1 % value2;
		else if (!stricmp(op,(char*)"random")) value = random((unsigned int)(value2 - value1)) + value1; 
 		else if (*op == '<' && op[1] == '<')  value = value1 << value2;  // BUT FLAWED if shift >= 32
		else if (*op == '>' && op[1] == '>') value = value1 >> value2;
		else if (*op == '^' || !stricmp(op,(char*)"exponent") || !stricmp(op,(char*)"power"))
		{
			if (value2 >= 1) 
			{
				value = value1;
				while (--value2) value *= value1;
			}
			else if (value2 == 0) value = 1;
			else return FAILRULE_BIT;
		}
		if (value == NOT_A_NUMBER) strcpy(buffer,(char*)"?");
		else 
		{
#ifdef WIN32
			sprintf(buffer,(char*)"%I64d",(long long int) value); 
#else
			sprintf(buffer,(char*)"%lld",(long long int) value); 
#endif
		}
	}
	return NOPROBLEM_BIT;
}

static FunctionResult IsNumberCode(char* buffer)
{
	return IsDigitWord(ARGUMENT(1)) ? NOPROBLEM_BIT : FAILRULE_BIT;
}

static FunctionResult TimeFromSecondsCode(char* buffer)
{
	int64 seconds;
	char* word = ARGUMENT(1);
	ReadInt64(word,seconds);
	time_t sec = (time_t) seconds;

	// optional 2nd arg changes time zone as offset
	int sign = 1;
	char* offset = ARGUMENT(2);
	if (*offset == '+') ++offset;
	else if (*offset == '-')
	{
		sign = -1;
		++offset;
	}
	int offsetHours = atoi(offset) * sign;
	seconds += offsetHours *  60 * 60; // hours offset

	// convert to text string in whatever timezone the raw is in.
	strcpy(buffer,ctime(&sec));
	*strchr(buffer,'\n') = 0; // erase newline at end

	return NOPROBLEM_BIT;
}

static FunctionResult TimeInfoFromSecondsCode(char* buffer)
{
	int64 seconds;
	char* word = ARGUMENT(1);
	ReadInt64(word,seconds);
	time_t sec = (time_t) seconds;
	struct tm *time = localtime((time_t *)&sec);
	if (impliedWild != ALREADY_HANDLED)  
	{
		SetWildCardIndexStart(impliedWild); //   start of wildcards to spawn
		impliedWild = ALREADY_HANDLED;
	}
	else SetWildCardIndexStart(0); //   start of wildcards to spawn
	char value[MAX_WORD_SIZE];
	sprintf(value,(char*)"%d",time->tm_sec);
	SetWildCard(value,value,0,0);
	sprintf(value,(char*)"%d",time->tm_min);
	SetWildCard(value,value,0,0);
	sprintf(value,(char*)"%d",time->tm_hour);
	SetWildCard(value,value,0,0);
	sprintf(value,(char*)"%d",time->tm_mday);
	SetWildCard(value,value,0,0);
	sprintf(value,(char*)"%s",months[time->tm_mon]); // january = 0
	SetWildCard(value,value,0,0);
	sprintf(value,(char*)"%d",time->tm_year + 1900);
	SetWildCard(value,value,0,0);
	sprintf(value,(char*)"%s",days[time->tm_wday]); // sunday = 0
	SetWildCard(value,value,0,0);

	return NOPROBLEM_BIT;
}


static FunctionResult TimeToSecondsCode(char* buffer)
{
	struct tm timeinfo;
	time_t rawtime;
	time (&rawtime );
	memcpy((char*)&timeinfo,(char*)localtime (&rawtime ),sizeof(timeinfo)); // get daylight savings value in

	/*
	tm_sec	int	seconds after the minute	0-61*
	tm_min	int	minutes after the hour	0-59
	tm_hour	int	hours since midnight	0-23
	tm_mday	int	day of the month	1-31
	tm_mon	int	months since January	0-11
	tm_year	int	years since 1900	
	tm_wday	int	days since Sunday	0-6
	tm_yday	int	days since January 1	0-365
	tm_isdst	int	Daylight Saving Time flag	
	*/
	//   Www Mmm dd hh:mm:ss yyyy Where Www is the weekday, Mmm the month in letters, dd the day of the month, hh:mm:ss the time, and yyyy the year. Sat May 20 15:21:51 2000
	timeinfo.tm_wday = timeinfo.tm_yday = 0;
	char* seconds = ARGUMENT(1);  
	if (*seconds == '-') return FAILRULE_BIT;
	ReadInt(seconds,timeinfo.tm_sec);
	if (timeinfo.tm_sec > 61) return FAILRULE_BIT; // leap seconds allowed
	char* minutes = ARGUMENT(2);  
	if (*minutes == '-') return FAILRULE_BIT;
	timeinfo.tm_min = atoi(minutes);
	char* hours = ARGUMENT(3);  
	if (*hours == '-') return FAILRULE_BIT;
	timeinfo.tm_hour = atoi(hours);
	char* day = ARGUMENT(4);  
	if (!IsDigit(*day)) return FAILRULE_BIT;
	timeinfo.tm_mday = atoi(day);
	if (timeinfo.tm_mday == 0) return FAILRULE_BIT; // day must be 1 or higher

	char* month = ARGUMENT(5);  
	if (IsDigit(*month)) timeinfo.tm_mon = atoi(month) - 1;	 // 0 based
	else if (!strnicmp(month,(char*)"jan",3)) timeinfo.tm_mon = 0;
	else if (!strnicmp(month,(char*)"feb",3)) timeinfo.tm_mon = 1;
	else if (!strnicmp(month,(char*)"mar",3)) timeinfo.tm_mon = 2;
	else if (!strnicmp(month,(char*)"apr",3)) timeinfo.tm_mon = 3;
	else if (!strnicmp(month,(char*)"may",3)) timeinfo.tm_mon = 4;
	else if (!strnicmp(month,(char*)"jun",3)) timeinfo.tm_mon = 5;
	else if (!strnicmp(month,(char*)"jul",3)) timeinfo.tm_mon = 6;
	else if (!strnicmp(month,(char*)"aug",3)) timeinfo.tm_mon = 7;
	else if (!strnicmp(month,(char*)"sep",3)) timeinfo.tm_mon = 8;
	else if (!strnicmp(month,(char*)"oct",3)) timeinfo.tm_mon = 9;
	else if (!strnicmp(month,(char*)"nov",3)) timeinfo.tm_mon = 10;
	else if (!strnicmp(month,(char*)"dec",3)) timeinfo.tm_mon = 11;
	char* year = ARGUMENT(6);  
	if (*year == '-') return FAILRULE_BIT;
	timeinfo.tm_year = atoi(year); // years since 1900
	if (timeinfo.tm_year < 1970 || timeinfo.tm_year > 2100) return FAILRULE_BIT; // unacceptable year
	timeinfo.tm_year -= 1900;	// pass in as years since 1900
	char* daylightsavings = ARGUMENT(7);  
	if (*daylightsavings == '1' || *daylightsavings == 't' ||  *daylightsavings == 'T') timeinfo.tm_isdst = 1;
	else if (*daylightsavings == '0' || *daylightsavings == 'f' ||  *daylightsavings == 'F') timeinfo.tm_isdst = 0;
	
	time_t time = mktime (&timeinfo);
	if (time == -1) return FAILRULE_BIT;
#ifdef WIN32
    sprintf(buffer,(char*)"%I64d",(int64) time); 
#else
	sprintf(buffer,(char*)"%lld",(int64) time);
#endif
	return NOPROBLEM_BIT;
}
//////////////////////////////////////////////////////////
/// DEBUG FUNCTIONS
//////////////////////////////////////////////////////////

static FunctionResult LogCode(char* buffer)
{
	char* stream = ARGUMENT(1);
	uint64 flags;
	bool bad = false;
	bool response = false;
	stream = ReadFlags(stream,flags,bad,response); // try for flags
	if (flags && *stream == ')') ++stream; // skip end of flags
	char name[MAX_WORD_SIZE];
	*name = 0;
	FunctionResult result;
	bool keep = false;
	char* fname =  NULL;
	if (!strnicmp(stream,(char*)"CLOSE",5))
	{
		stream = ReadCompiledWord(stream,name); // close
		stream = ReadShortCommandArg(stream,name,result,OUTPUT_NOQUOTES|OUTPUT_EVALCODE|OUTPUT_NOTREALBUFFER); // name of file
		if (*name == '"') 
		{
			size_t len = strlen(name);
			name[len-1] = 0;	// remove trailing "
		}
		fname = (*name == '"') ? (name+1) : name;
		for (unsigned int i = 0; i < MAX_LOG_NAMES; ++i)
		{
			if (!stricmp(fname,lognames[i])) // found already open
			{
				FClose(logfiles[i]);
				logfiles[i] = NULL;
				*lognames[i] = 0;
				return NOPROBLEM_BIT;
			}
		}
		return FAILRULE_BIT;
	}

	if (!strnicmp(stream,(char*)"OPEN ",5)) keep = true; // dont close it
	if (!strnicmp(stream,(char*)"FILE ",5) || !strnicmp(stream,(char*)"OPEN ",5)) // write data to this file
	{
		stream = ReadCompiledWord(stream,name); // FILE or OPEN
		stream = ReadShortCommandArg(stream,name,result,OUTPUT_NOQUOTES|OUTPUT_EVALCODE|OUTPUT_NOTREALBUFFER); // name of file
		if (*name == '"') 
		{
			size_t len = strlen(name);
			name[len-1] = 0;	// remove trailing "
		}
		fname = (*name == '"') ? (name+1) : name;
		if (!strnicmp(stream,(char*)"NEW",3)) // start with a clean file
		{
			char junk[MAX_WORD_SIZE];
			stream = ReadCompiledWord(stream,junk);
			FILE* out = FopenUTF8Write(fname);
			if (out) FClose(out);
			else return FAILRULE_BIT;
		}
	}

	++outputNest;
	WORDP lock = dictionaryLocked;
	dictionaryLocked = (WORDP)22; // allow format string to work even while compiling a table
	Output(stream,buffer,result,OUTPUT_EVALCODE | (unsigned int) flags | OUTPUT_NOTREALBUFFER);
	--outputNest;
	dictionaryLocked = lock;

	if (fname)
	{
		FILE* out = NULL;
		bool cached = false;
		for (unsigned int i = 0; i < MAX_LOG_NAMES; ++i)
		{
			if (!stricmp(fname,lognames[i])) // found already open
			{
				out = logfiles[i]; 
				cached = true;
				break;
			}
		}
		if (!out) // not cached
		{
			out = FopenUTF8WriteAppend(fname);
			if (keep)
			{
				for (unsigned int i = 0; i < MAX_LOG_NAMES; ++i) // try to cache it
				{
					if (!logfiles[i]) // found already open
					{
						logfiles[i] = out; 
						strcpy(lognames[i],fname);
						cached = true;
						break;
					}
				}
			}
		}
		if (out) 
		{
			fwrite(buffer,1,strlen(buffer),out);
			if (!cached) FClose(out);
		}
		else 
		{
			*buffer = 0;
			return FAILRULE_BIT;
		}
	}
	else Log(STDTRACELOG,(char*)"%s",buffer);
	if (flags & OUTPUT_ECHO && !echo) printf((char*)"%s",buffer);
	*buffer = 0;
	return NOPROBLEM_BIT;
}


//////////////////////////////////////////////////////////
/// OUTPUT FUNCTIONS
//////////////////////////////////////////////////////////

static FunctionResult FlushOutputCode(char* buffer)
{
	if (planning) return FAILRULE_BIT;
	if (!AddResponse(currentOutputBase,responseControl)) return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

static FunctionResult InsertOutput(char* stream, char* buffer, int index)
{
	// add at end, then alter order
	FunctionResult result;
	Output(stream,buffer,result,OUTPUT_EVALCODE);
	if (AddResponse(buffer,responseControl))
	{
		memmove(&responseOrder[index+1],&responseOrder[index],responseIndex - index); // shift order out 1
		responseOrder[index] = (unsigned char)(responseIndex-1);
	}
	else result = FAILRULE_BIT;
	return result;
}

static FunctionResult InsertPrintCode(char* buffer) 
{     
	if (planning) return FAILRULE_BIT;
	char* stream = ARGUMENT(1);
	uint64 flags;
	bool bad = false;
	bool response = false;
	stream = ReadFlags(stream,flags,bad,response); // try for flags
	if (flags) ++stream; // skip end of flags
	FunctionResult result;
	char beforeIndex[MAX_WORD_SIZE];
	stream = ReadShortCommandArg(stream,beforeIndex,result); 
	int index = 0;
	
	if (*beforeIndex == '~') // put before 1st reference to this topic
	{
		int topic = FindTopicIDByName(beforeIndex);
		for (int i = responseIndex-1; i > 0; --i)
		{
			if (topic == responseData[responseOrder[i]].topic) index = responseOrder[i];
		}	
	}
	else if (IsDigit(*beforeIndex)) // numeric index he gives must be 1 based, eg before %response 
	{
		index = atoi(beforeIndex);
		if (index <= 0 || index > (int)responseIndex) return FAILRULE_BIT;
		index = responseOrder[index-1]; // the current location of the output
	}
	return InsertOutput(stream,buffer,index);
}

static FunctionResult PrintCode(char* buffer) 
{     
	if (planning) return FAILRULE_BIT;
	char* stream = ARGUMENT(1);
	uint64 flags;
	bool bad = false;
	bool response = false;
	stream = ReadFlags(stream,flags,bad,response); // try for flags
	if ((flags || response) && *stream == ')') ++stream; // skip end of flags

	FunctionResult result;
	Output(stream,buffer,result,OUTPUT_EVALCODE | (unsigned int) flags);
	if (!(flags & OUTPUT_RETURNVALUE_ONLY) && !AddResponse(buffer,response ? (unsigned int)flags : responseControl)) result = FAILRULE_BIT;
	return result;
}

static FunctionResult PrePrintCode(char* buffer)
{
	if (planning) return FAILRULE_BIT;
	char* stream = ARGUMENT(1); 
	uint64 flags;
	bool bad = false;
	bool response = false;
	stream = ReadFlags(stream,flags,bad,response); // try for flags
	if (flags) ++stream; // skip end of flags

	return InsertOutput(stream,buffer,0);
}

static FunctionResult RepeatCode(char* buffer)
{ 
	if (postProcessing)
	{
		ReportBug((char*)"Illegal to use ^Repeat during postprocessing");
		return FAILRULE_BIT;
	}
	AddRepeatable(currentRule); // local repeats allowed this volley
	return NOPROBLEM_BIT;
}

static FunctionResult SetPronounCode(char* buffer) 
{  
	// argument1 is a word to use
	// mark(word _xxx) enable word mark at location of _xxx variable
	char* ptr = ARGUMENT(1);
	if (!*ptr) return FAILRULE_BIT;

	FunctionResult result;
	char word[MAX_WORD_SIZE];
	ptr = ReadShortCommandArg(ptr,word,result); // what is being marked
	if (result & ENDCODES) return result;
	if (!*word) return FAILRULE_BIT; // missing arg

	char word1[MAX_WORD_SIZE];
	ptr = ReadCompiledWord(ptr,word1);  // the locator

	int startPosition;
	int endPosition;
	if (!*word1 || *word1 == ')') startPosition = endPosition = 1; // default mark  (ran out or hit end paren of call
	else if (IsDigit(*word1)) endPosition = startPosition = atoi(word1); // named number as index
	else if (*word1 == '_') //  wildcard position designator
	{
		startPosition = wildcardPosition[GetWildcardID(word1)] & 0x0000ffff; // the match location
		endPosition = wildcardPosition[GetWildcardID(word1)] >> 16; 
	}
	else return FAILRULE_BIT;

	if (startPosition < 1) startPosition = 1;
	if (startPosition > wordCount)  startPosition = wordCount;
	WORDP D = StoreWord(word);
	MarkFacts(MakeMeaning(D),startPosition,startPosition);

	WORDP entry;
	WORDP canonical;
	uint64 sysflags = 0;
	uint64 cansysflags = 0;
	WORDP revise;
	GetPosData(-1,word,revise,entry,canonical,sysflags,cansysflags,false); // NOT first try
	wordStarts[startPosition] = reuseAllocation(wordStarts[startPosition],D->word); 
	wordCanonical[startPosition] = (canonical) ? canonical->word : D->word;	
	if (!wordCanonical[startPosition]) wordCanonical[startPosition] = D->word;

	uint64 bits = D->properties & (NOUN_PROPERTIES | NOUN_BITS|NOUN_INFINITIVE|LOWERCASE_TITLE);
	allOriginalWordBits[startPosition] = bits;
	finalPosValues[startPosition] = bits;
	MarkTags(startPosition);

	return NOPROBLEM_BIT;
}

//////////////////////////////////////////////////
/// OUTPUT ACCESS
//////////////////////////////////////////////////

static FunctionResult LastSaidCode(char* buffer)
{
	if (chatbotSaidIndex) 
	{
		sprintf(buffer,(char*)"%s",chatbotSaid[chatbotSaidIndex-1]);
		char* special;
		char* at = buffer;
		while ((special = strchr(at,'\\')))
		{
			if (special[1] == 'r')
			{
				memmove(special+1,special+2,strlen(special+1));
				*special = '\r';
			}
			else if (special[1] == 'n')
			{
				memmove(special+1,special+2,strlen(special+1));
				*special = '\n';
			}
			at = special+1;
		}
	}
	return NOPROBLEM_BIT;
}

static FunctionResult ResponseCode(char* buffer)
{
	int index = atoi(ARGUMENT(1)) -1 ;
	if (index >= responseIndex || index < 0) return FAILRULE_BIT;
	sprintf(buffer,(char*)"%s",responseData[responseOrder[index]].response);
	return NOPROBLEM_BIT;
}

static FunctionResult ResponseQuestionCode(char* buffer)
{
	int index = atoi(ARGUMENT(1)) - 1; // which response (1 based)
	if (index >= responseIndex || index < 0) return FAILRULE_BIT;
	char* ptr = TrimSpaces(responseData[responseOrder[index]].response,false);
	strcpy(buffer,(ptr[strlen(ptr)-1] == '?') ? (char*) "1" : (char*) ""); 
	return NOPROBLEM_BIT;
}

static FunctionResult ResponseRuleIDCode(char* buffer)
{
	int index = atoi(ARGUMENT(1)) - 1; // they say 1, we use 0
	if (index >= responseIndex) return FAILRULE_BIT;
	ComputeWhy(buffer,index);
	return NOPROBLEM_BIT;
}

//////////////////////////////////////////////////////////
/// POSTPROCESSING FUNCTIONS
//////////////////////////////////////////////////////////

static FunctionResult AnalyzeCode(char* buffer)
{
	char* word = ARGUMENT(1);
	SAVEOLDCONTEXT()
	FunctionResult result;
	Output(word,buffer,result);
	// Convert2Blanks(buffer); // remove any system underscoring back to blanks -- block it from outside if you want
	if (*buffer == '"') // if a string, remove quotes
	{
		size_t len = strlen(buffer);
		if (buffer[len-1] == '"') 
		{
			buffer[len-1] = 0;
			*buffer = ' ';
		}
	}
	PrepareSentence(buffer,true,false,true); 
	*buffer = 0; // only wanted effect of script
	RESTOREOLDCONTEXT()
	return NOPROBLEM_BIT;
}

static FunctionResult PostPrintBeforeCode(char* buffer) // only works if post processing
{     
	if (!postProcessing) 
	{
		ReportBug((char*)"Cannot use ^PostPrintBefore except in postprocessing");
		return FAILRULE_BIT;
	}
	
	char* stream = ARGUMENT(1);		
	uint64 flags;
	bool bad = false;
	bool response = false;
	stream = ReadFlags(stream,flags,bad,response); // try for flags
	if (flags) ++stream; // skip end of flags

	FunctionResult result;
	Output(stream,buffer,result,OUTPUT_EVALCODE| (unsigned int)flags);

	// prepend output 
	if (AddResponse(buffer,responseControl))
	{
		memmove(&responseOrder[0+1],&responseOrder[0],responseIndex - 0); // shift order out 1
		responseOrder[0] = (unsigned char)(responseIndex-1);
	}

	*buffer = 0;
	return result;
}

static FunctionResult PostPrintAfterCode(char* buffer) // only works if post processing
{     
	if (!postProcessing) 
	{
		ReportBug((char*)"Cannot use ^PostProcessPrintAfter except in postprocessing");
		return FAILRULE_BIT;
	}
	
	char* stream = ARGUMENT(1);		
	uint64 flags;
	bool bad = false;
	bool response = false;
	stream = ReadFlags(stream,flags,bad,response); // try for flags
	if (flags) ++stream; // skip end of flags

	FunctionResult result;
	Output(stream,buffer,result,OUTPUT_EVALCODE| (unsigned int)flags);

	// postpend output 
	AddResponse(buffer,responseControl);
	*buffer = 0;
	return result;
}

static FunctionResult SleepCode(char* buffer)
{
	int milliseconds = atoi(ARGUMENT(1));
#ifdef WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif 
	return NOPROBLEM_BIT;
}

static FunctionResult EnvironmentCode(char* buffer)
{
	const char* env_p = std::getenv(ARGUMENT(1));
	if (!env_p || !*env_p) return FAILRULE_BIT;
	strcpy(buffer,env_p);
	return NOPROBLEM_BIT;
}

static FunctionResult BacktraceCode(char* buffer)
{
	int i = globalDepth +1;
	char rule[MAX_WORD_SIZE];
	while (--i > 0) 
	{
		strncpy(rule,ruleDepth[i],50);
		rule[50] = 0;
		sprintf(buffer,"Depth %d: %s - %s\r\n",i,nameDepth[i],rule);
		buffer += strlen(buffer);
	}
	return NOPROBLEM_BIT;
}

static FunctionResult ReviseOutputCode(char* buffer)
{
	// if (postProcessing) return FAILRULE_BIT;
	char* arg1 = ARGUMENT(1); // index first, rest is output
	if (!IsDigit(*arg1)) return FAILRULE_BIT;
	int index = atoi(arg1) - 1;
	if (index >= responseIndex || index < 0) return FAILRULE_BIT;
	char* arg2 = ARGUMENT(2);
	if (!stricmp(arg2,"null") || !stricmp(arg2,"\"\"")) *arg2 = 0;
	strcpy(responseData[index].response,arg2);
	return NOPROBLEM_BIT;
}

//////////////////////////////////////////////////////////
/// COMMAND FUNCTIONS
//////////////////////////////////////////////////////////

static FunctionResult CommandCode(char* buffer) 
{
	DoCommand(ARGUMENT(1),NULL,false);
	return NOPROBLEM_BIT;
}

FunctionResult IdentifyCode(char* buffer) 
{	
	char* os;
#ifdef WIN32
	os = "Windows";
#elif IOS
	os = "IOS";
#elif __MACH__
	os = "MACH";
#else
	os = "LINUX";
#endif
	sprintf(buffer,(char*)"ChatScript Version %s  %ld bit %s compiled %s\r\n",version,(long int)(sizeof(char*) * 8),os,compileDate);

	return NOPROBLEM_BIT;
}

FunctionResult DebugCode(char* buffer) 
{	
	char* xarg = ARGUMENT(1);
	if (!stricmp(xarg,(char*)"deeptrace") && trace) deeptrace = !deeptrace;
	return NOPROBLEM_BIT;
}

static FunctionResult ReturnCode(char* buffer)
{
	char* arg = ARGUMENT(1);
	char* output = AllocateBuffer();
	FunctionResult result;
	FreshOutput(arg,output,result,OUTPUT_NOCOMMANUMBER, MAX_BUFFER_SIZE);
	FreeBuffer(); // usable still
	if (result != NOPROBLEM_BIT) return result;
	if (*output) memmove(buffer,output,strlen(output)+1);
	char* x = functionAnswerName;
	if (functionAnswerBase != buffer && *output) 
	{
		int xx = 0;
	}
	//	memmove(functionAnswerBase,output,strlen(output)+1);
	if (!stricmp(buffer,"null")) 
		*buffer = 0;	// treat null as null
	return ENDCALL_BIT;
}

static FunctionResult EndCode(char* buffer)
{ 
	char* word = ARGUMENT(1);
	if (!stricmp(word,(char*)"RULE")) return ENDRULE_BIT;
	if (!stricmp(word,(char*)"LOOP")) return ENDLOOP_BIT;
	if (!stricmp(word,(char*)"TOPIC") || !stricmp(word,(char*)"PLAN")) return ENDTOPIC_BIT;
	if (!stricmp(word,(char*)"SENTENCE")) return ENDSENTENCE_BIT;
	if (!stricmp(word,(char*)"INPUT")) return ENDINPUT_BIT;
  	if (!stricmp(word,(char*)"CALL")) return ENDCALL_BIT;
	return FAILRULE_BIT;
}

static FunctionResult EvalCode(char* buffer) //  ??? needed with output eval instead?
{	
	FunctionResult result;
	char* stream = ARGUMENT(1);
	uint64 flags;
	bool bad = false;
	bool response = false;
	stream = ReadFlags(stream,flags,bad,response); // try for flags
	if (flags && *stream == ')') ++stream; // skip end of flags
	Output(stream,buffer,result,OUTPUT_EVALCODE|(unsigned int)flags); 
	return result;
}

static FunctionResult FailCode(char* buffer) 
{      
	char* word = ARGUMENT(1);
	if (!stricmp(word,(char*)"LOOP")) return FAILRULE_BIT;
	if (!stricmp(word,(char*)"RULE")) return FAILRULE_BIT;
	if (!stricmp(word,(char*)"TOPIC") || !stricmp(word,(char*)"PLAN")) 
	{
		RemovePendingTopic(currentTopicID);
		return FAILTOPIC_BIT;
	}
	if (!stricmp(word,(char*)"SENTENCE")) return FAILSENTENCE_BIT;
	if (!stricmp(word,(char*)"INPUT")) return FAILINPUT_BIT;
	return FAILRULE_BIT;
}

FunctionResult MatchesCode(char* buffer)
{
	GetPatternData(buffer);
	return NOPROBLEM_BIT;
}

FunctionResult MatchCode(char* buffer) 
{     
	char word[MAX_WORD_SIZE];
	char word1[MAX_WORD_SIZE];
	char* at = ReadCompiledWord(ARGUMENT(1),word1);
	char pack[MAX_WORD_SIZE];
	char* base = pack;

	if (*word1 == USERVAR_PREFIX && !*at) strcpy(word,GetUserVariable(word1)); //   solitary user var, decode it  eg match($var)
	else if (*word1 == '_' && !*at) strcpy(word,wildcardCanonicalText[GetWildcardID(word1)]); //   solitary user var, decode it  eg match($var)
	else if (*word1 == '@' && !*at) 
	{
		FunctionResult result;
		ReadShortCommandArg(word1,word,result,OUTPUT_NOQUOTES|OUTPUT_EVALCODE|OUTPUT_NOTREALBUFFER); 
		if (result != NOPROBLEM_BIT) return result;
	}
	else 
	{
		if (word1[0] == FUNCTIONSTRING && word1[1] == '(') strcpy(word,word1+1);
		else strcpy(word,word1); // otherwise it is what to say (like from idiom table)
	}

	if (*word == '~') // named an existing rule
	{
		char* rule;
		bool fulllabel = false;
		int id = 0;
		bool crosstopic = false;
		char* dot = strchr(word,'.');
		if (!dot)  return FAILRULE_BIT;
		if (dot && IsDigit(dot[1])) rule = GetRuleTag(currentTopicID,id,word);
		else rule = GetLabelledRule(currentTopicID,word,(char*)"",fulllabel,crosstopic,id,currentTopicID);
		if (!rule) return FAILRULE_BIT;
		GetPattern(rule,NULL,pack);
		++base; // ignore starting paren
	}
	else
	{
		char* ptr = word;
		if (*word)  
		{
			size_t len = strlen(word);
			strcpy(word+len,(char*)" )"); // insure it has a closing paren (even if it has);
			if (*word == '"') 
			{
				word[len-1] = ' '; // change closing " to space
				++ptr;	// skip opening "
				if (*ptr == FUNCTIONSTRING) ++ptr; // bypass the mark

				// now purify of any internal \" " marked strings
				char* x = strchr(ptr,'\\');
				while (x)
				{
					if (x[1] == '"') memmove(x,x+1,strlen(x));	// remove escape
					x = strchr(x + 1,'\\'); // next?
				}
			}
		}
		if (*ptr == FUNCTIONSTRING) ++ptr;	// skip compiled string mark
		while (*ptr == ' ') ++ptr;	// prepare for start
		if (*ptr != '(') // insure it has a pattern start
		{
			memmove(ptr+1,ptr,strlen(ptr)+1);
			*ptr = '(';
		}

		at = pack;
		*at = 0;
	#ifdef DISCARDSCRIPTCOMPILER 
		base = ptr;	// do the best you can, may not be laid out properly
	#else
		int oldDepth = globalDepth;
		if (setjmp(scriptJump[++jumpIndex])) // return on script compiler error
		{
			--jumpIndex;
			globalDepth = oldDepth;
			return FAILRULE_BIT;
		}
		char junk[MAX_WORD_SIZE];
		ReadNextSystemToken(NULL,NULL,junk,false,false); // flush cache
		ReadPattern(ptr, NULL, at,false,false,true); // compile the pattern
		strcat(at,(char*)" )");
		at = pack; // for debug retry
		base = pack; // the compiled pattern
		--jumpIndex;
	#endif
	}
 	if (!*base) return FAILRULE_BIT;	// NO DATA?
	bool uppercasem = false;
	int matched = 0;
	wildcardIndex = 0;  //   reset wildcard allocation on top-level pattern match
	int first = 0;
	int last = 0;
	if (*base == '(') ++base;		// skip opening paren of a pattern
 	if (*base == ' ') ++base;		// skip opening space of a pattern
    bool match = Match(base,0,0,(char*)"(",1,0,first,last,uppercasem,matched,0,0) != 0;  //   skip paren and treat as NOT at start depth, dont reset wildcards- if it does match, wildcards are bound
	
	if (clearUnmarks) // remove transient global disables.
	{
		clearUnmarks = false;
		for (int i = 1; i <= wordCount; ++i) unmarked[i] = 1;
	}
	ShowMatchResult(!match ? FAILRULE_BIT : NOPROBLEM_BIT ,base,NULL);
	if (!match) return FAILRULE_BIT;
	char data[10];
	sprintf(data,"%d",first);
	SetUserVariable("$$csmatch_start",data);
	sprintf(data,"%d",last);
	SetUserVariable("$$csmatch_end",data);
	return NOPROBLEM_BIT;
}

static FunctionResult NoRejoinderCode(char* buffer)
{
	norejoinder = true;
	return NOPROBLEM_BIT;
}

static FunctionResult NoTraceCode(char* buffer)
{      
	unsigned int oldtrace = trace;
	if (!(trace & TRACE_ALWAYS))	trace = TRACE_ON; // allow specific topic and function traces
	FunctionResult result;
	*buffer = 0;
	ChangeDepth(1,(char*)"NoTraceCode");
	char* arg1 = ARGUMENT(1);
	Output(arg1,buffer,result);
	ChangeDepth(-1,(char*)"NoTraceCode");
	trace = (modifiedTrace) ? modifiedTraceVal : oldtrace;
	return result; 
}

#include <map>
using namespace std;
extern std::map <WORDP, int> triedData; // per volley index into string space
static int saveCount = 0;

static FunctionResult SaveSentenceCode(char* buffer)
{
	if (documentMode) return FAILRULE_BIT;
	char* arg1 = ARGUMENT(1);
	if (!*arg1) return FAILRULE_BIT;	// need an id
	MEANING M = MakeMeaning(StoreWord(arg1,AS_IS));

	// compute words (4byte int) needed
	int size = 2; // basic list
	size += 2;	// tokenflags
	size += 1;	// preparation count
	++size; // wordcount
	size += 6 * wordCount;	// wordStarts + wordCanonical + finalpos(2) + topics and concepts
	size += 1 + derivationLength + 256/2; // derivation count and sentence words and derivation index

	++size;  // map counter
	for (std::map<WORDP,int>::iterator it=triedData.begin(); it!=triedData.end(); ++it) size += 2; // key and stringspace reference
	size += MAX_SENTENCE_LENGTH/4;
	
	int total = size;
	if (total & 1) ++total; // round to even set of words, for int64 align
	total += 1; // save bits for moretocome and moretocomequestion

	unsigned int* memory = (unsigned int*) AllocateString(NULL,total/2,8,false); // int64 aligned
	memory[0] = savedSentences;
	savedSentences = String2Index((char*) memory); // threaded list
	memory[1] = M; // key 
	memory[2] = sentencePreparationIndex;
	((uint64*)memory)[3] = tokenFlags; // 3,4
	memory[5] = wordCount;
	memory[6] = (moreToCome) ? 1 : 0;
	if (moreToComeQuestion) memory[6] |= 2;

	int n = 7; // store from here
	WORDP D;
	for (int i = 1; i <= wordCount; ++i)
	{
		D = StoreWord(wordStarts[i],AS_IS);
		M = MakeMeaning(D);
		memory[n++] = M;
		D = StoreWord(wordCanonical[i],AS_IS);
		M = MakeMeaning(D);
		memory[n++] = M;

		memory[n++] = finalPosValues[i] >> 32;
		memory[n++] = finalPosValues[i] & 0x00000000ffffffff;

		// concepts and topics lists are threaded links in string space. [0] is the concept/topic, [1] is next link
		// that list will be valid later on restore
		memory[n++] = concepts[i];
		memory[n++] = topics[i];
	}

	// store derivation data
	memory[n++] = derivationLength;
	for (int i = 1; i <= derivationLength; ++i)
	{
		D = StoreWord(derivationSentence[i],AS_IS);
		M = MakeMeaning(D);
		memory[n++] = M;
	}
	memmove(memory+n,derivationIndex,sizeof(short int) * 256); // why is this so big, could be half size?
	n += 256/2;

	unsigned int* counterLocation = memory + n++;
	int counter = 0;
	for (std::map<WORDP,int>::iterator it=triedData.begin(); it!=triedData.end(); ++it)
	{
		++counter;
		memory[n++] = MakeMeaning(it->first); // key
		memory[n++] = it->second;
	}
	*counterLocation = counter; // backfill with number to do

	memmove(memory+n,unmarked,sizeof(char) * MAX_SENTENCE_LENGTH); // clear all mark suppression
	n += MAX_SENTENCE_LENGTH/4;

	if (n != size)
	{
		int xx = 0; // TROUBLE
	}
	saveCount = n;
	sprintf(buffer,"%d",n); // words
	return NOPROBLEM_BIT;
}

static FunctionResult IfCode(char* buffer)
{
	return FAILRULE_BIT;	// shouldnt come thru here
}
	
static FunctionResult LoopCode(char* buffer)
{
	return FAILRULE_BIT;	// shouldnt come thru here
}

static FunctionResult RestoreSentenceCode(char* buffer)
{
	if (documentMode) return FAILRULE_BIT;
	char* arg1 = ARGUMENT(1);
	if (!*arg1) return FAILRULE_BIT;	// need an id
	MEANING M = MakeMeaning(StoreWord(arg1,AS_IS));

	unsigned int list = savedSentences;
	unsigned int* memory = NULL;
	while (list) // find the sentence referred to
	{
		memory = (unsigned int*) Index2String(list);
		if (memory[1] == M) break; // found it
		list = memory[0];
	}
	if (!list) return FAILRULE_BIT;

	if ((unsigned int) lastRestoredIndex == memory[2]) return NOPROBLEM_BIT;	// is already current
	lastRestoredIndex = memory[2];

	ClearWhereInSentence();
	tokenFlags = ((uint64*)memory)[3]; // 3,4
	wordCount = memory[5];
	moreToCome = (memory[6] & 1) ? true : false;
	moreToComeQuestion = (memory[6] & 2) ? true : false;
	
	int n = 7; 
	WORDP D;
	for (int i = 1; i <= wordCount; ++i)
	{
		M = memory[n++];
		D = Meaning2Word(M);
		wordStarts[i] = D->word;
		M = memory[n++];
		D = Meaning2Word(M);
		wordCanonical[i] = D->word;
		finalPosValues[i] = memory[n++];
		finalPosValues[i] <<= 32;
		finalPosValues[i] |= memory[n++];
		concepts[i] = memory[n++]; // contain string space indices that are still valid
		topics[i] = memory[n++];
	}
	
	// get derivation data
	derivationLength = memory[n++];
	for (int i = 1; i <= derivationLength; ++i)
	{
		M = memory[n++];
		D = Meaning2Word(M);
		derivationSentence[i] = D->word;
	}
	memmove(derivationIndex,memory+n,sizeof(short int) * 256);
	n += 256/2;

	// fill in WHERE data on recorded words
	int counter = memory[n++];
	for (int i = 0; i < counter; ++i)
	{
		WORDP key = Meaning2Word(memory[n++]);
		triedData[key] = memory[n++];
	}
	memmove(unmarked,memory+n,sizeof(char) * MAX_SENTENCE_LENGTH);
	n += MAX_SENTENCE_LENGTH/4;

	if (n != saveCount)
	{
		int xx = 0; // inconsistent
	}

	if (trace & TRACE_FLOW)
	{
		Log(STDTRACELOG,"RestoreSentence: ");
		for (int i = 1; i <= wordCount; ++i) Log(STDTRACELOG,"%s ",wordStarts[i]);
		Log(STDTRACELOG,"\r\n");
	}

	return NOPROBLEM_BIT;
}

static FunctionResult NoFailCode(char* buffer)
{      
	char word[MAX_WORD_SIZE];
	char* ptr = ReadCompiledWord(ARGUMENT(1),word);
	FunctionResult result;
	ChangeDepth(1,(char*)"noFailCode");
	Output(ptr,buffer,result);
	ChangeDepth(-1,(char*)"noFailCode");
	if (result == RESTART_BIT) return result;
	else if (!stricmp(word,(char*)"RULE")) return (FunctionResult) (result & (RESTART_BIT|ENDTOPIC_BIT|FAILTOPIC_BIT|RETRYTOPIC_BIT|ENDSENTENCE_BIT|FAILSENTENCE_BIT|ENDINPUT_BIT|RETRYSENTENCE_BIT|ENDCALL_BIT));
	else if (!stricmp(word,(char*)"TOPIC")) return (FunctionResult) ( result & (RESTART_BIT|ENDSENTENCE_BIT|FAILSENTENCE_BIT|RETRYSENTENCE_BIT|ENDINPUT_BIT|RETRYINPUT_BIT|ENDCALL_BIT));
	else if (!stricmp(word,(char*)"LOOP")) return (FunctionResult) ( result & (RESTART_BIT|ENDTOPIC_BIT|FAILTOPIC_BIT|RETRYTOPIC_BIT| ENDSENTENCE_BIT|FAILSENTENCE_BIT|RETRYSENTENCE_BIT| ENDINPUT_BIT|RETRYINPUT_BIT|ENDCALL_BIT));
	else if (!stricmp(word,(char*)"SENTENCE") || !stricmp(word,(char*)"INPUT")) return NOPROBLEM_BIT;
	return FAILRULE_BIT; // not a legal choice
}

static FunctionResult NotNullCode(char* buffer)
{
	FunctionResult result;
	Output(ARGUMENT(1),buffer,result);
	if (*buffer) *buffer = 0;
	else return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

static FunctionResult ResultCode(char* buffer)
{
	FunctionResult result;
	Output(ARGUMENT(1),buffer,result);
	if (result == RESTART_BIT) return result;
	*buffer = 0;
	strcpy(buffer,ResultCode(result));
	return NOPROBLEM_BIT;
}

static FunctionResult RetryCode(char* buffer)
{
	char* arg = ARGUMENT(1);
	if (!stricmp(arg,(char*)"TOPIC"))  return RETRYTOPIC_BIT;
	if (!stricmp(arg,(char*)"sentence"))  return RETRYSENTENCE_BIT;
	if (!stricmp(arg,(char*)"input"))  
		return RETRYINPUT_BIT;
	if (!stricmp(arg,(char*)"toprule")) 
	{
		return RETRYTOPRULE_BIT;
	}
	return RETRYRULE_BIT;
}

static WORDP memoryDict = 0;
static char* memoryText = 0;
static FACT* memoryFact = 0;

static WORDP memoryDictBase = 0;
static char* memoryTextBase = 0;
static FACT* memoryFactBase = 0;

FunctionResult MemoryMarkCode(char* buffer)
{
	memoryText = stringFree;
	memoryDict = dictionaryFree;
	memoryFact = factFree;
	return NOPROBLEM_BIT;
}

void SetBaseMemory()
{
	memoryTextBase = stringFree;
	memoryDictBase = dictionaryFree;
	memoryFactBase = factFree;
}

void ResetBaseMemory()
{
	ClearUserVariables(memoryTextBase); // reset any above and delete from list but leave alone ones below
	ResetFactSystem(memoryFactBase);// empties all fact sets and releases facts above marker
	DictionaryRelease(memoryDictBase,memoryTextBase); // word & text
	ReportBug((char*)"Emergency Memory Reset\r\n");
	echo = true;
	Log(STDTRACELOG,(char*)"Emergency Memory Reset\r\n");
	echo = false;
}

FunctionResult MemoryFreeCode(char* buffer)
{
	if (!memoryText) return FAILRULE_BIT;
	memset(wordStarts,0,sizeof(char*)*MAX_SENTENCE_LENGTH);
	for (unsigned int i = 0; i < MAX_SENTENCE_LENGTH; ++i) 
	{
		if (wordStarts[i] && wordStarts[i] < memoryText)
			wordStarts[i] = 0;	// do not point to released space
	}
	ClearUserVariables(memoryText); // reset any above and delete from list but leave alone ones below
	ResetFactSystem(memoryFact);// empties all fact sets and releases facts above marker
	DictionaryRelease(memoryDict,memoryText); // word & text
	return NOPROBLEM_BIT;
}

FunctionResult ClearContextCode(char* buffer)
{
	contextIndex = 0;
	return NOPROBLEM_BIT;
}

FunctionResult AuthorizedCode(char* buffer)
{
	if (server && !VerifyAuthorization(FopenReadOnly((char*)"authorizedIP.txt"))) return FAILRULE_BIT; // authorizedIP
	return NOPROBLEM_BIT;
}
	
FunctionResult AddContextCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	int topic = (!strcmp(arg1,(char*)"~")) ? currentTopicID : FindTopicIDByName(arg1);
	if (!topic) return FAILRULE_BIT;
	AddContext(topic,ARGUMENT(2));
	return NOPROBLEM_BIT;
}

FunctionResult InContextCode(char* buffer)
{
	char* arg = ARGUMENT(1);
	char* dot = strchr(arg,'.');
	int topic = currentTopicID;
	if (dot) 
	{
		*dot = 0;
		topic = FindTopicIDByName(arg);
		arg = dot + 1;
	}
	unsigned int turn = InContext(topic,arg);
	if (turn == 0) return FAILRULE_BIT;
	sprintf(buffer,(char*)"%d",turn);
	return NOPROBLEM_BIT;
}

FunctionResult LoadCode(char* buffer)
{
	FunctionResult answer;
	char* arg1 = ARGUMENT(1);
	if (!stricmp(arg1,(char*)"null")) // unload
	{
		if (!topicBlockPtrs[2]) return FAILRULE_BIT;	// nothing is loaded
		ReturnToAfterLayer(1,false);	// drop 2 info.. but dictionary is now unlocked. Need to relock it.
		answer = NOPROBLEM_BIT;
	}
	else answer = LoadLayer(2,arg1,BUILD2);
	dictionaryLocked = dictionaryPreBuild[2];
	stringLocked = stringsPreBuild[2];
	factLocked = factsPreBuild[2]; // unlock
	return answer;
}

FunctionResult ArgumentCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	char* arg2 = ARGUMENT(2);
	if (!IsDigit(*arg1) || !callIndex) return FAILRULE_BIT;	// must be number index within some call
	int d = callIndex - 1;// start of real
	while (*arg2 && d >=0)
	{
		if (!stricmp(callStack[d]->word,arg2)) break; // found named
		--d;
	}
	if (d < 0) return FAILRULE_BIT;	// could not find requested topic
	unsigned int arg = MACRO_ARGUMENT_COUNT(callStack[d]); // how many arguments can it handle
	unsigned int requestedArg = atoi(arg1);
	if (requestedArg == 0) return NOPROBLEM_BIT;  // just checking to see if caller exists
	if (requestedArg > arg) return FAILRULE_BIT;	// not a legal arg value, too high
	strcpy(buffer,callArgumentList[callArgumentBases[d]+requestedArg]);
	return NOPROBLEM_BIT;
}

//////////////////////////////////////////////////////////
/// DATABASE FUNCTIONS
//////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////
/// WORD MANIPULATION
//////////////////////////////////////////////////////////

static FunctionResult BurstCode(char* buffer) //   take value and break into facts of burst-items as subjects
{//   ^burst(^cause : )   1: data source 2: burst character  optional 0th argument is WORDCOUNT

	//   prepare spot to store pieces
	MEANING verb;
	MEANING object;
	if (impliedWild != ALREADY_HANDLED)  SetWildCardIndexStart(impliedWild); //   start of wildcards to spawn
	object = verb = Mburst;
	*buffer = 0;
	bool count = false;
	char* ptr = ARGUMENT(1); //   what to burst
	if (*ptr == '"' ) // if a quoted string, remove the quotes
	{
		++ptr;
		size_t len = strlen(ptr);
		if (ptr[len-1] == '"') ptr[len-1] = 0;
	}
	bool once = false;
	if (!stricmp(ptr,(char*)"count")) 
	{
		count = true;
		ARGUMENT(1) = ARGUMENT(2);
		ptr = ARGUMENT(1);
		ARGUMENT(2) = ARGUMENT(3);
		ARGUMENT(3) = ARGUMENT(4);
		ARGUMENT(4) = ARGUMENT(5);
	}
	else if (!stricmp(ptr,(char*)"once")) 
	{
		once = true;
		ARGUMENT(1) = ARGUMENT(2);
		ptr = ARGUMENT(1);
		ARGUMENT(2) = ARGUMENT(3);
		ARGUMENT(3) = ARGUMENT(4);
		ARGUMENT(4) = ARGUMENT(5);
	}

	unsigned int counter = 0;
	if (count && *ptr) counter = 1; // there is at least one.
	
	bool tracing = false;
	if (trace)
	{
		WORDP D = FindWord("^Burst");
		if (D->internalBits & MACRO_TRACE || trace & TRACE_FACT) tracing = true;
	}

	//   get string to search for. If quoted, remove the quotes
	char* scan = ARGUMENT(2);	//   how to burst
	char* scan1 = scan;

	if (!*scan || !*scan1) // scan1 test just to suppress compiler warning
	{
		scan = " "; // default is space AND OR _
		scan1 = "_";
	}
	else // clear any special characters
	{
		char* find;
		while ((find = strstr(scan,(char*)"\\r"))) // convert cr
		{
			*find = '\r';
			memmove(find+1,find+2,strlen(find)+1);
		}
		while ((find = strstr(scan,(char*)"\\n"))) // convert cr
		{
			*find = '\n';
			memmove(find+1,find+2,strlen(find)+1);
		}
		while ((find = strstr(scan,(char*)"\\t"))) // convert cr
		{
			*find = '\t';
			memmove(find+1,find+2,strlen(find)+1);
		}
	}
	
	if (*ptr == '"' ) // if a quoted string, remove the quotes
	{
		++ptr;
		size_t len = strlen(ptr);
		if (ptr[len-1] == '"') ptr[len-1] = 0;
	}
	ChangeDepth(1,"^Burst");

	if (*scan == '"' ) // if a quoted string as burst char, remove the quotes
	{
		++scan;
		size_t len = strlen(scan);
		if (scan[len-1] == '"') scan[len-1] = 0;
		if (*scan == 0) // explode into characters
		{  
			--ptr; //   what to explode
			char word[MAX_WORD_SIZE];
			word[1] = 0;
			SET_FACTSET_COUNT(impliedSet,0);
			while (*++ptr && ptr[1]) // leave rest for end
			{
				*word = *ptr;
				if (tracing) Log(STDTRACELOG,(char*)"[%d]: %s ",counter,word);
				++counter;
				//   store piece before scan marker
				if (impliedWild != ALREADY_HANDLED)  SetWildCard(word,word,0,0);
				else if (impliedSet != ALREADY_HANDLED)
				{
					MEANING T = MakeMeaning(StoreWord(word,AS_IS));
					FACT* F = CreateFact(T, verb,object,FACTTRANSIENT|FACTDUPLICATE);
					AddFact(impliedSet,F);
				}
				else //   dump straight to output buffer, first piece only
				{
					strcpy(buffer,word);
					break;
				}
				if (once) break;
			}
			if (count) sprintf(buffer,(char*)"%d",counter); // just doing count
			if (impliedWild != ALREADY_HANDLED)  
			{
				SetWildCard(ptr,ptr,0,0);
				SetWildCard((char*)"",(char*)"",0,0); // clear next one
			}
			else if (impliedSet != ALREADY_HANDLED)
			{
				if (*ptr) AddFact(impliedSet,CreateFact(MakeMeaning(StoreWord(ptr,AS_IS)), verb,object,FACTTRANSIENT|FACTDUPLICATE));
			}
			else if (!*buffer) strcpy(buffer,ptr);

			impliedSet = impliedWild = ALREADY_HANDLED;	
			currentFact = NULL;
			ChangeDepth(-1,"^Burst");
			return NOPROBLEM_BIT;
		}
		scan1 = scan;
	}

	//   loop that splits into pieces and stores them
	if (!scan[1]) // strip off leading and trailing separators, must occur BETWEEN tokens
	{
		while (*ptr == *scan) ++ptr; 
		while (*ptr == *scan1) ++ptr; 
		char* end = ptr + strlen(ptr) - 1;
		while (*end == *scan) *end-- = 0;
		while (*end == *scan1) *end-- = 0;
	}

	char* hold = strstr(ptr,scan);
	char* hold1 = (scan1) ? strstr(ptr,scan1) : NULL;
	if (!hold) hold = hold1; // only has second
	if (hold1 && hold1 < hold) hold = hold1; // sooner
	size_t scanlen = strlen(scan);

	if (impliedSet != ALREADY_HANDLED) SET_FACTSET_COUNT(impliedSet,0);
	while (hold)
	{
		*hold = 0;	//   terminate first piece
		if (*ptr == 0) {;} // null piece - breaking here makes no sense at start
		if (tracing) Log(STDTRACELOG,(char*)"%d: %s ",counter,ptr);
		++counter;
		//   store piece before scan marker
		if (count) {;}
		else if (impliedWild != ALREADY_HANDLED)  SetWildCard(ptr,ptr,0,0);
		else if (impliedSet != ALREADY_HANDLED)
		{
			if (*ptr)
			{
				MEANING T = MakeMeaning(StoreWord(ptr,AS_IS));
				FACT* F = CreateFact(T, verb,object,FACTTRANSIENT|FACTDUPLICATE);
				AddFact(impliedSet,F);
			}
		}
		else //   dump straight to output buffer, first piece only
		{
			strcpy(buffer,ptr);
			break;
		}

		ptr = hold + scanlen; //   ptr after scan marker
		if (!*ptr) hold = 0;
		while (*ptr)
		{
			hold = strstr(ptr,scan);
			hold1 = (scan1) ? strstr(ptr,scan1) : NULL;
			if (!hold) hold = hold1; // only has second
			if (hold1 && hold1 < hold) hold = hold1; // sooner
			if (hold == ptr && !scan[1]) ++ptr;// there is an excess of single splits here
			else break;
		}

		if (once) break;
	}

	//   assign the last piece
	char result[MAX_WORD_SIZE];
	if (count) // just doing count
	{
		sprintf(result,(char*)"%d",counter);
		ptr = result;
	}
	if (impliedWild != ALREADY_HANDLED)  
	{
		SetWildCard(ptr,ptr,0,0);
		SetWildCard((char*)"",(char*)"",0,0); // clear next one
	}
	else if (impliedSet != ALREADY_HANDLED)
	{
		if (*ptr) AddFact(impliedSet,CreateFact(MakeMeaning(StoreWord(ptr,AS_IS)), verb,object,FACTTRANSIENT|FACTDUPLICATE));
	}
	else if (!*buffer) strcpy(buffer,ptr);

	if (tracing) Log(STDTRACELOG,(char*)"%d: %s ",counter,ptr);
	ChangeDepth(-1,"^Burst");
	impliedSet = impliedWild = ALREADY_HANDLED;	//   we did the assignment
	currentFact = NULL; // should not advertise any created facts
	return NOPROBLEM_BIT;
}

static FunctionResult CanonCode(char* buffer) 
{
#ifndef DISCARDSCRIPTCOMPILER
	if (!compiling) return FAILRULE_BIT;	 // only during script processing
	SaveCanon(ARGUMENT(1),ARGUMENT(2));
	return NOPROBLEM_BIT;
#else
	return FAILRULE_BIT;
#endif
}

static FunctionResult FlagsCode(char* buffer)
{
	WORDP D = FindWord(ARGUMENT(1));
	if (!D) return FAILRULE_BIT;
#ifdef WIN32
	sprintf(buffer,(char*)"%I64d",D->systemFlags); 
#else
	sprintf(buffer,(char*)"%lld",D->systemFlags); 
#endif
	return NOPROBLEM_BIT;
}

static FunctionResult UppercaseCode(char* buffer)
{
	strcpy(buffer, (IsUpperCase(ARGUMENT(1)[0])) ? (char*) "1" : (char*) "0");
	return NOPROBLEM_BIT;
}

static FunctionResult PropertiesCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	WORDP D = FindWord(arg1);
	if (!D) return FAILRULE_BIT;
#ifdef WIN32
	sprintf(buffer,(char*)"%I64d",D->properties); 
#else
	sprintf(buffer,(char*)"%lld",D->properties); 
#endif
	return NOPROBLEM_BIT;
}

static FunctionResult LayerCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	WORDP D = FindWord(arg1);
	if (!D) return FAILRULE_BIT;
	if (D->internalBits & BUILD0) strcpy(buffer,"0");
	else if (D->internalBits & BUILD1) strcpy(buffer,"1");
	else if (D->internalBits & BUILD2) strcpy(buffer,"2");
	else if (IS_NEW_WORD(D)) strcpy(buffer,"user");
	else strcpy(buffer,"wordnet");
	return NOPROBLEM_BIT;
}

static char* NextWord(char* ptr, WORDP& D,bool canon)
{
	char word[MAX_WORD_SIZE];
	ptr = ReadCompiledWord(ptr,word);
	MakeLowerCase(word);
	if (canon)
	{
		WORDP entry,canonical;
		uint64 sysflags = 0;
		uint64 cansysflags = 0;
		WORDP revise;
		GetPosData(-1,word,revise,entry,canonical,sysflags,cansysflags); 
		if (canonical) strcpy(word,canonical->word);
		else if (entry) strcpy(word,entry->word);
	}
	MakeLowerCase(word);
	D = StoreWord(word);
	return ptr;
}

static FunctionResult IntersectWordsCode(char* buffer)
{
	unsigned int store = (impliedSet == ALREADY_HANDLED) ? 0 : impliedSet;
	SET_FACTSET_COUNT(store,0);
	WORDP words[2000];
	int index = 0;

	char* arg1 = ARGUMENT(1);
	if (*arg1 == '"')
	{
		size_t len = strlen(arg1);
		if (arg1[len-1] == '"')
		{
			arg1[len-1] = 0;
			++arg1;
		}
	}
	Convert2Blanks(arg1);
	char* at = arg1;
	while ((at = strchr(at,'~'))) *at = ' '; 

	char* arg2 = ARGUMENT(2); 
	if (*arg2 == '"')
	{
		size_t len = strlen(arg2);
		if (arg2[len-1] == '"')
		{
			arg2[len-1] = 0;
			++arg2;
		}
	}
	Convert2Blanks(arg2);
	at = arg2;
	while ((at = strchr(at,'~'))) *at = ' '; 

	bool canon = (!stricmp(ARGUMENT(3),(char*)"canonical"));
	WORDP D;
	while (*arg1)
	{
		arg1 = NextWord(arg1,D,canon);
		D->internalBits |= INTERNAL_MARK;
		words[++index] = D;
	}

	unsigned int count = 0;
	if (index) 
	{
		while (*arg2) 
		{
			arg2 = NextWord(arg2,D,canon);
			if (D->internalBits & INTERNAL_MARK)
			{
				FACT* old = factFree;
				FACT* F = CreateFact(MakeMeaning(D),Mintersect,MakeMeaning(D),FACTTRANSIENT|FACTDUPLICATE);
				if (old != factFree) 
				{
					AddFact(store,F);
					count = 1;
				}
			}
		}
	
		while (index) RemoveInternalFlag(words[index--],INTERNAL_MARK);
	}

	if (impliedSet == ALREADY_HANDLED && count == 0) return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

static FunctionResult JoinCode(char* buffer) 
{     
	char* original = buffer;
	char* ptr = ARGUMENT(1);
	bool autospace = false;
	if (!strnicmp(ptr,(char*)"AUTOSPACE",9))
	{
		autospace = true;
		ptr += 10;
	}
    while (ptr)
	{
		char word[MAX_WORD_SIZE];
		char* at = ReadCompiledWord(ptr,word); 
        if (*word == ')' || !*word) break; //   done
		size_t len = strlen(word);
		if (*word == '"' && word[1] == ' ' && word[2] == '"') // pure simple space
		{
			strcpy(buffer,(char*)" ");
			ptr = at;
		}
		else if (*word == '"' && word[1] ==  FUNCTIONSTRING) // compiled code being joined
		{
			FunctionResult result = NOPROBLEM_BIT;
			ReformatString(*word,word+2,buffer,result,0);
			if (result != NOPROBLEM_BIT) return result;
			ptr = at;
		}
		else if (*word == '"' && word[len-1] == '"')
		{
			word[len-1] = 0;
			strcpy(buffer,word+1);
			ptr = at;
		}
 		else 
		{
			FunctionResult result;
			char* bigword = AllocateInverseString(NULL,MAX_BUFFER_SIZE);
			ptr = ReadCommandArg(ptr,bigword,result,0,MAX_BUFFER_SIZE);
			if (!(result & ENDCODES))
			{
				size_t olen = strlen(bigword);
				if (olen >= currentOutputLimit) 
				{
					strncpy(buffer,bigword,currentOutputLimit-1);
					buffer[currentOutputLimit-1] = 0;
					ReportBug((char*)"join of %d exceeded caller limit of %d. Truncated: %s\r\n",olen,currentOutputLimit,buffer);
				}
				else strcpy(buffer,bigword);
				ReleaseInverseString(bigword);
			}
			else 
			{
				ReleaseInverseString(bigword);
				return result;
			}
		}
		bool did = *buffer != 0;
		buffer += strlen(buffer);
		if (autospace && did) *buffer++ = ' '; 
    }
	if (autospace && original != buffer) *--buffer = 0;
 	return NOPROBLEM_BIT;
}

static FunctionResult PhraseCode(char* buffer)
{
	char* arg = ARGUMENT(1);
	char posn[MAX_WORD_SIZE];
	char type[MAX_WORD_SIZE];
	bool canonical = false;
	int n;
	arg = ReadCompiledWord(arg,type);
	arg = ReadCompiledWord(arg,posn);
	if (!strnicmp(arg,(char*)"canonical",9)) canonical = true;
	if (*posn == '\'') memmove(posn,posn+1,strlen(posn));
	if (IsDigit(*posn))
	{
		n = atoi(posn);
		if (n == 0 || n > wordCount) return FAILRULE_BIT;
	}
	else if (*posn == '_')  n = WildStartPosition(posn);
	else if (*posn == '^') 
	{
		char word[MAX_WORD_SIZE];
		ReadArgument(posn,word);
		n = atoi(word);
	}
	else if (*posn == USERVAR_PREFIX) n = atoi(GetUserVariable(posn));
	else return FAILRULE_BIT;
	int i = n;
	if (!stricmp(type,(char*)"noun")) // noun phrase
	{
		if (roles[i] & (MAINOBJECT|MAINSUBJECT) && verbals[i]) // like "to play football"
		{
			int x = verbals[i];
			if (!x) return FAILRULE_BIT;
			while (i && verbals[--i] & x){;}
			while (verbals[++n] & x){;};
			--n;
		}
		else if (roles[i] & (MAINOBJECT|MAINSUBJECT) && clauses[i]) // "I like *whatever tastes good"
		{
			int x = clauses[i];
			if (!x) return FAILRULE_BIT;
			while (i && clauses[--i] & x){;}
			while (clauses[++n] & x){;};
			--n;
		}		
		else while (posValues[--i] & (ADJECTIVE_BITS|DETERMINER_BITS|POSSESSIVE|ADVERB)){;}
		// for now ignore , and conjunct coord grabbing like "my fat, luxurious file"
	}
	else if (!stricmp(type,(char*)"preposition")) // prep phrase
	{
		int x = phrases[n];
		if (!x) return FAILRULE_BIT;
		if (phrases[x] & phrases[startSentence])
		{
			strcat(buffer,wordStarts[startSentence]);
			strcat(buffer,(char*)"_");
			strcat(buffer,wordStarts[n]);
		}
		else while (i && phrases[--i] & x){;}
		while (phrases[n+1] == x) ++n;	// find actual end
	}
	else if (!stricmp(type,(char*)"verbal")) // verbal phrase
	{
		int x = verbals[n];
		if (!x) return FAILRULE_BIT;
		while (i && verbals[++i] & x){;}
		int tmp = i-1;
		i = n;
		n = tmp;
	}
	else if (!stricmp(type,(char*)"adjective")) // complement phrase
	{
		while (posValues[--i] & (ADJECTIVE_BITS|ADVERB)){;}
	}
	else return FAILRULE_BIT;
	if (n > wordCount) return FAILRULE_BIT;
	while (++i <= n)
	{
		if (canonical) strcat(buffer,wordCanonical[i]);
		else strcat(buffer,wordStarts[i]);
		if (i != n) strcat(buffer,wildcardSeparator);
	}
	return NOPROBLEM_BIT;
}

static FunctionResult POSCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	char* arg2 = ARGUMENT(2);

	char* arg3 = ARGUMENT(3);
	char* arg4 = ARGUMENT(4);
	if (!stricmp(arg1,(char*)"raw"))
	{
		int n = atoi(arg2);
		if (n < 1 || n > (int)wordCount) return FAILRULE_BIT;
		strcpy(buffer,wordStarts[n]);
		return NOPROBLEM_BIT;
	}
	if (!stricmp(arg1,(char*)"conjugate"))
	{
		int64 pos;
		ReadInt64(arg3,pos);
		if (pos & (VERB_PRESENT_PARTICIPLE | NOUN_GERUND))
		{
			ARGUMENT(1) = AllocateInverseString((char*)"verb");
			ARGUMENT(3) = AllocateInverseString((char*)"present_participle");
			POSCode(buffer);
		}
		else if (pos & VERB_PAST_PARTICIPLE) 
		{
			ARGUMENT(1) = AllocateInverseString((char*)"verb");
			ARGUMENT(3) = AllocateInverseString((char*)"past_participle");
			POSCode(buffer);
		}
		else if (pos & VERB_PAST) 
		{ 
			if (!stricmp(arg2,(char*)"be") && pos & SINGULAR_PERSON) strcpy(buffer,(char*)"was"); // for 1st and 3rd person singular,  default is were
			else
			{
				ARGUMENT(1) = AllocateInverseString((char*)"verb");
				ARGUMENT(3) = AllocateInverseString((char*)"past");
				POSCode(buffer);
			}
		}
		else if (pos & VERB_PRESENT) 
		{ 
			if (!stricmp(arg2,(char*)"be") && pos & SINGULAR_PERSON) strcpy(buffer,(char*)"am"); // default is is. good for are
			else
			{
				ARGUMENT(1) = AllocateInverseString((char*)"verb");
				ARGUMENT(3) = AllocateInverseString((char*)"present");
				POSCode(buffer);
			}
		}
		else if (pos & VERB_PRESENT_3PS) 
		{ 
			ARGUMENT(1) = AllocateInverseString((char*)"verb");
			ARGUMENT(3) = AllocateInverseString((char*)"present3ps");
			POSCode(buffer);
		}
		else if (pos & NOUN_PLURAL || pos & NOUN_PROPER_PLURAL)
		{
			ARGUMENT(1) = AllocateInverseString((char*)"noun");
			ARGUMENT(3) = AllocateInverseString((char*)"plural");
			POSCode(buffer);
		}
		else if (pos & PLACE_NUMBER)
		{
			size_t len = strlen(arg2);
			char c = arg2[len-1];
			int val = atoi(arg2);
			if (val == 11 || val == 12 || val == 13) sprintf(buffer,(char*)"%dth",val);
			else if (c == '1') sprintf(buffer,(char*)"%sst",arg2);
			else if (c == '2') sprintf(buffer,(char*)"%snd",arg2); 
			else if (c == '3') sprintf(buffer,(char*)"%srd",arg2);
			else if (IsDigit(*arg2)) sprintf(buffer,(char*)"%sth",arg2);
			else strcpy(buffer,arg2); // first, second, third etc
		}
		else if (pos & MORE_FORM && pos & ADJECTIVE) 
		{ 
			ARGUMENT(1) = AllocateInverseString((char*)"adjective");
			ARGUMENT(3) = AllocateInverseString((char*)"more");
			POSCode(buffer);
		}
		else if (pos & MOST_FORM && pos & ADJECTIVE) 
		{ 
			ARGUMENT(1) = AllocateInverseString((char*)"adjective");
			ARGUMENT(3) = AllocateInverseString((char*)"most");
			POSCode(buffer);
		}
		else if (pos & MORE_FORM && pos & ADVERB) 
		{ 
			ARGUMENT(1) = AllocateInverseString((char*)"adverb");
			ARGUMENT(3) = AllocateInverseString((char*)"more");
			POSCode(buffer);
		}
		else if (pos & MOST_FORM && pos & ADVERB) 
		{ 
			ARGUMENT(1) = AllocateInverseString((char*)"adverb");
			ARGUMENT(3) = AllocateInverseString((char*)"most");
			POSCode(buffer);
		}
		else if (pos & PRONOUN_POSSESSIVE) 
		{ 
			if (!stricmp(arg2,(char*)"he")) strcpy(buffer,(char*)"his"); // currently we keep pronouns as is, but we might use canonical on them
			else if (!stricmp(arg2,(char*)"his")) strcpy(buffer,(char*)"his");
			else if (!stricmp(arg2,(char*)"she")) strcpy(buffer,(char*)"her");
			else if (!stricmp(arg2,(char*)"her")) strcpy(buffer,(char*)"her");
			else if (!stricmp(arg2,(char*)"it")) strcpy(buffer,(char*)"its");
			else if (!stricmp(arg2,(char*)"its")) strcpy(buffer,(char*)"its");
			else if (!stricmp(arg2,(char*)"they")) strcpy(buffer,(char*)"their");
			else if (!stricmp(arg2,(char*)"their")) strcpy(buffer,(char*)"their");
			else if (!stricmp(arg2,(char*)"you")) strcpy(buffer,(char*)"your");
			else if (!stricmp(arg2,(char*)"my")) strcpy(buffer,(char*)"my");
			else if (!stricmp(arg2,(char*)"I")) strcpy(buffer,(char*)"my");
		}
		else if (pos & PRONOUN_OBJECT) 
		{ 
			if (!stricmp(arg2,(char*)"he")) strcpy(buffer,(char*)"him");
			else if (!stricmp(arg2,(char*)"she")) strcpy(buffer,(char*)"her");
			else if (!stricmp(arg2,(char*)"I")) strcpy(buffer,(char*)"me");
			else if (!stricmp(arg2,(char*)"they")) strcpy(buffer,(char*)"them");
			else if (!stricmp(arg2,(char*)"him")) strcpy(buffer,(char*)"him");
			else if (!stricmp(arg2,(char*)"her")) strcpy(buffer,(char*)"her");
			else if (!stricmp(arg2,(char*)"me")) strcpy(buffer,(char*)"me");
			else if (!stricmp(arg2,(char*)"them")) strcpy(buffer,(char*)"them");
		else strcpy(buffer,arg2);
		}
		else strcpy(buffer,arg2);
		return NOPROBLEM_BIT;
	}
	if (!stricmp(arg1,(char*)"syllable"))
	{
		sprintf(buffer,(char*)"%d",ComputeSyllables(arg2));
		return NOPROBLEM_BIT;
	}
	if (!stricmp(arg1,(char*)"hex64"))
	{
		int64 num;
		ReadInt64(arg2,num);
#ifdef WIN32
		sprintf(buffer,(char*)"0x%016I64x",num);
#else
		sprintf(buffer,(char*)"0x%016llx",num); 
#endif
		return NOPROBLEM_BIT;
	}
	if (!stricmp(arg1,(char*)"hex32"))
	{
		int num;
		ReadInt(arg2,num);
		sprintf(buffer,(char*)"0x%08x",num);
		return NOPROBLEM_BIT;
	}
	if (!stricmp(arg1,(char*)"type"))
	{
		if (*arg2 == '~') strcpy(buffer,(char*)"concept");
		else if (IsDigit(*arg2)) strcpy(buffer,(char*)"number");
		else if (IsAlphaUTF8(*arg2)) strcpy(buffer,(char*)"word");
		else strcpy(buffer,(char*)"unknown");
		return NOPROBLEM_BIT;
	}
	if (!stricmp(arg1,(char*)"common"))
	{
		if (!arg2) return FAILRULE_BIT;
		WORDP D = FindWord(arg2,0,PRIMARY_CASE_ALLOWED);
		if (!D) return NOPROBLEM_BIT;
		uint64 level = (D->systemFlags & COMMON7);
		level >>= (64-14);
		sprintf(buffer,(char*)"%d",(int)level);
		return NOPROBLEM_BIT;
	}
	if (!stricmp(arg1,(char*)"verb"))
	{
		if (!arg2) return FAILRULE_BIT;
		char word[MAX_WORD_SIZE];
		MakeLowerCopy(word,arg2);
		char* infin = GetInfinitive(word,true); 
		if (!infin) infin = word;
		if (!stricmp(arg3,(char*)"present_participle")) 
		{
			char* use = GetPresentParticiple(word);
			if (!use) return FAILRULE_BIT;
			strcpy(buffer,use);
		}
		else if (!stricmp(arg3,(char*)"past_participle")) 
		{
			char* use = GetPastParticiple(word);
			if (!use) return FAILRULE_BIT;
			strcpy(buffer,use);
		}
		else if (!stricmp(arg3,(char*)"infinitive")) 
		{
			char* verb = GetInfinitive(word,true);
			if (!verb) return FAILRULE_BIT;
			strcpy(buffer,verb);
		}
		else if (!stricmp(arg3,(char*)"past")) 
		{
			char* past = GetPastTense(infin);
			if (!stricmp(infin,(char*)"be"))
			{
				if (!stricmp(arg4,(char*)"I")) past = "was";
				else  past = "were";
			}
			if (!past) return FAILRULE_BIT;
			strcpy(buffer,past);
		}
		else if (!stricmp(arg3,(char*)"present3ps")) 
		{
			char* third = GetThirdPerson(infin);
			if (!third) return FAILRULE_BIT;
			strcpy(buffer,third);
		}
		else if (!stricmp(arg3,(char*)"present")) 
		{
			char* third = GetPresent(infin);
			if (!stricmp(infin,(char*)"be"))
			{
				if (!stricmp(arg4,(char*)"I")) third = "am";
				else if (!stricmp(arg4,(char*)"you") || !stricmp(arg4,(char*)"we") || !stricmp(arg4,(char*)"they")) third = "are";
				else  third = "is";
			}
			if (!stricmp(infin,(char*)"do"))
			{
				if (!stricmp(arg4,(char*)"I")) third = "do";
				else if (!stricmp(arg4,(char*)"you") || !stricmp(arg4,(char*)"we") || !stricmp(arg4,(char*)"they")) third = "do";
				else  third = "does";
			}
			if (!third) return FAILRULE_BIT;
			strcpy(buffer,third);
		}
		else if (!stricmp(arg3,(char*)"match"))
		{
			WORDP D = FindWord(arg4);
			char* plural = GetPluralNoun(D);
			char* verb;
			if (!plural || stricmp(plural,arg4)) // singular noun
			{
				verb = GetThirdPerson(arg2);
				if (verb)  strcpy(buffer,verb);
			}
			else // plural noun
			{
				verb = GetInfinitive(arg2,false);
				if (verb) 
				{
					if (!stricmp(verb,(char*)"be")) strcpy(buffer,(char*)"are");
					else strcpy(buffer,verb);
				}
			}
			if (!*buffer) strcpy(buffer,arg2);
		}
		if (IsUpperCase(ARGUMENT(2)[0])) *buffer = GetUppercaseData(*buffer);
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"aux")) // (aux do you)
	{
		if (!arg2) return FAILRULE_BIT;
		char word[MAX_WORD_SIZE];
		MakeLowerCopy(word,ARGUMENT(2));
		char* result = word;
   
		if (!strcmp(arg2,(char*)"do")) //   present tense
		{
			if (strcmp(arg3,(char*)"I") && strcmp(arg3,(char*)"you")) result = "does"; 
			else result = "do";
		}
		else if (!strcmp(arg2,(char*)"have")) 
		{
			if (strcmp(arg3,(char*)"I") && strcmp(arg3,(char*)"you")) result = "has"; 
			else result = "have";
		}
		else if (!strcmp(arg2,(char*)"be")) 
		{
			if (!strcmp(arg3,(char*)"I") ) result = "am";
			else if (!strcmp(arg3,(char*)"you")) result = "are"; 
			else result = "is";
		}
		else if (!strcmp(arg2,(char*)"was") || !strcmp(arg2,(char*)"were")) //   past tense
		{
			if (!strcmp(arg3,(char*)"I") ) result = "was";
			result = "were";
		}
		else result = arg2;
		strcpy(buffer,result);
		if (IsUpperCase(arg2[0])) *buffer = GetUppercaseData(*buffer);
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"pronoun"))
	{
		if (!stricmp(arg3,(char*)"flip"))
		{
			unsigned int n = BurstWord(arg2,0);
			for (unsigned int i = 0; i < n; ++i)
			{
				char* word = GetBurstWord(i);
				if (!stricmp(word,(char*)"my")) word = "your";
				else if (!stricmp(word,(char*)"your")) word = "my";
				else if (!stricmp(word,(char*)"I")) word = "you";
				else if (!stricmp(word,(char*)"you")) word = "I";
				strcpy(buffer,word);
				buffer += strlen(buffer);
				if (i != (n-1)) strcpy(buffer,(char*)" ");
				buffer += strlen(buffer);
			}
			return NOPROBLEM_BIT;
		}
	}
	else if (!stricmp(arg1,(char*)"adjective"))
	{
		if (!arg2) return FAILRULE_BIT;
		char word[MAX_WORD_SIZE];
		MakeLowerCopy(word,ARGUMENT(2));
		char* adj = word; 
		if (!stricmp(arg3,(char*)"more"))
		{
			char* more = GetAdjectiveMore(adj);
			if (!more) sprintf(buffer,(char*)"more %s",adj);
			else strcpy(buffer,more);
		}
		else if (!stricmp(arg3,(char*)"most"))
		{
			char* most = GetAdjectiveMost(adj);
			if (!most) sprintf(buffer,(char*)"most %s",adj);
			else strcpy(buffer,most);
		}
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"adverb"))
	{
		if (!arg2) return FAILRULE_BIT;
		char word[MAX_WORD_SIZE];
		MakeLowerCopy(word,ARGUMENT(2));
		char* adv = word; 
		if (!stricmp(arg3,(char*)"more"))
		{
			char* more = GetAdverbMore(adv);
			if (!more) sprintf(buffer,(char*)"more %s",adv);
			else strcpy(buffer,more);
		}
		else if (!stricmp(arg3,(char*)"most"))
		{
			char* most = GetAdverbMost(adv);
			if (!most) sprintf(buffer,(char*)"most %s",adv);
			else strcpy(buffer,most);
		}
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"noun"))
	{
		if (!stricmp(arg3,(char*)"proper")) 
		{
			// we know the word, use its known casing for spelling
			WORDP D = FindWord(arg2,0,UPPERCASE_LOOKUP);
			if (D)
			{
				strcpy(buffer,D->word);
				return NOPROBLEM_BIT;
			}

			// synthesize appropriate casing
			unsigned int n = BurstWord(arg2);
			for (unsigned int i = 0; i < n; ++i)
			{
				char* word = GetBurstWord(i);
				D = FindWord(word,0,LOWERCASE_LOOKUP);
				if (D && D->properties & LOWERCASE_TITLE); //   allowable particles and connecting words that can be in lower case
				else *word = GetUppercaseData(*word);
				strcat(buffer,word);
				if (i != (n-1)) strcat(buffer,(char*)" ");
			}
			return NOPROBLEM_BIT;
		}
		if (!stricmp(arg3,(char*)"lowercaseexist"))	// lower case legal?
		{
			WORDP D = FindWord(arg2,0,LOWERCASE_LOOKUP);
			return (D) ? NOPROBLEM_BIT : FAILRULE_BIT;
		}
		if (!stricmp(arg3,(char*)"uppercaseexist"))	// upper case legal?
		{
			WORDP D = FindWord(arg2,0,UPPERCASE_LOOKUP);
			return (D) ? NOPROBLEM_BIT : FAILRULE_BIT;
		}

		char* noun =  GetSingularNoun(arg2,true,false);
		if (!noun) return NOPROBLEM_BIT;
		if (!stricmp(arg3,(char*)"singular") || (atoi(arg3) == 1 && !strchr(arg3,'.')) || !stricmp(arg3,(char*)"-1") || !stricmp(arg3,(char*)"one")) // allow number 1 but not float
		{
			strcpy(buffer,noun);
			return NOPROBLEM_BIT;		
		}
		else if (!stricmp(arg3,(char*)"plural") || IsDigit(arg3[0]) || (*arg3 == '-' && IsDigit(arg3[1])) ) // allow number non-one and negative 1
		{
			//   swallow the args. for now we KNOW they are wildcard references
			char* plural = GetPluralNoun(StoreWord(noun));
			if (!plural) return NOPROBLEM_BIT;
			strcpy(buffer,plural);
			return NOPROBLEM_BIT;
		}
		else if (!stricmp(arg3,(char*)"irregular") ) // generate a response only if plural is irregular from base (given)
		{
			//   swallow the args. for now we KNOW they are wildcard references
			char* plural = GetPluralNoun(StoreWord(noun));
			if (!plural) return NOPROBLEM_BIT;
			size_t len = strlen(noun);
			if (strnicmp(plural,noun,len)) strcpy(buffer,plural); // show plural when base not in it
			return NOPROBLEM_BIT;
		}
	}
	else if (!stricmp(arg1,(char*)"determiner")) //   DETERMINER noun
	{
		size_t len = strlen(arg2);
		if (arg2[len-1] == 'g' && arg2[len-2] == 'n' && arg2[len-3] == 'i' && GetInfinitive(arg2,false)) //   no determiner on gerund
		{
			strcpy(buffer,arg2);
			return NOPROBLEM_BIT;
		}
		//   already has one builtinto the word or phrase
		if (!strnicmp(arg2,(char*)"a_",2) || !strnicmp(arg2,(char*)"an_",3) || !strnicmp(arg2,(char*)"the_",4)) 
		{
			strcpy(buffer,arg2);
			return NOPROBLEM_BIT;
		}

		WORDP D = FindWord(arg2);
		if (D && D->properties & (NOUN_PROPER_SINGULAR|NOUN_PROPER_PLURAL))  //no determiner, is mass or proper name
		{
			strcpy(buffer,arg2);
			return NOPROBLEM_BIT;
		}

		//   if a plural word, use no determiner
		char* s = GetSingularNoun(arg2,true,false);
		if (!s || stricmp(arg2,s)) //   if has no singular or isnt same, assume we are plural and add the
		{
			sprintf(buffer,(char*)"the %s",arg2);
			return NOPROBLEM_BIT;
		}

		//   provide the determiner now
		*buffer++ = 'a';
		*buffer = 0;
		if (IsVowel(*arg2)) *buffer++ = 'n'; //   make it "an"
		*buffer++ = ' ';	//   space before the word
		strcpy(buffer,arg2);
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"place"))
	{
		int value = (int)Convert2Integer(arg2);
		if ((value%10) == 1) sprintf(buffer,(char*)"%dst",value); 
		else if ((value%10) == 2) sprintf(buffer,(char*)"%dnd",value);
		else if ((value%10) == 3) sprintf(buffer,(char*)"%drd",value);
		else sprintf(buffer,(char*)"%dth",value);
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"capitalize") || !stricmp(arg1,(char*)"uppercase"))
	{
		strcpy(buffer,arg2);
		*buffer = GetUppercaseData(*buffer);
		char* at = buffer;
		while (*++at)
		{
			if (*at == ' ' || *at == '_') at[1] = GetUppercaseData(at[1]);
		}
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"allupper"))
	{
		strcpy(buffer,arg2);
		MakeUpperCase(buffer);
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"lowercase"))
	{
		MakeLowerCopy(buffer,arg2);
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"canonical"))
	{
		WORDP entry = NULL,canonical = NULL;
		uint64 sysflags = 0;
		uint64 cansysflags = 0;
		WORDP revise;
		if (*arg2) GetPosData(-1,arg2,revise,entry,canonical,sysflags,cansysflags);
		if (canonical) strcpy(buffer,canonical->word);
		else if (entry) strcpy(buffer,entry->word);
		else strcpy(buffer,arg2);
		return NOPROBLEM_BIT;
	}

	else if (!stricmp(arg1,(char*)"integer"))
	{
		strcpy(buffer,arg2);
		char* period = strchr(arg2,'.');
		if (period)
		{
			float val = (float) atof(arg2);
			*period = 0;
			int64 vali;
			ReadInt64(arg2,vali);
			if ((float) vali == val) strcpy(buffer,arg2);
		}
		return NOPROBLEM_BIT;
	}
	return FAILRULE_BIT;
}

static void RhymeWord(WORDP D, uint64 flag)
{
	if (!(D->properties & (NOUN | VERB | ADJECTIVE | ADVERB | DETERMINER_BITS | PRONOUN_BITS | CONJUNCTION | PREPOSITION | AUX_VERB ))) return;
	if (D->word && !IsAlphaUTF8(*D->word)) return;
	if (D->properties & (NOUN_PROPER_SINGULAR | NOUN_PROPER_PLURAL | NOUN_TITLE_OF_ADDRESS)) return;	// want ordinary words
	if (strchr(D->word,'_') || strchr(D->word,'-') ) return; // only normal words and not multi words either

	char* tail = (char*) flag;
	size_t n = strlen(tail);
	size_t len = strlen(D->word);
	if (len <= n) return;  // too short to rhyme
	if (D->word[len-1] != tail[n-1] || D->word[len-2] != tail[n-2]) return;	// must end the same way for last 2 letters
	if (!stricmp(D->word,tail)) return; // cannot have whole match
	if (!IsVowel(tail[n-1]) && !IsVowel(tail[n-2]) && D->word[len-3] != tail[n-3]) return;	// if 2 consonant ending, vowel before must match also
	if ((len - n) > 3) return; // should be similar in size

	if (!IsVowel(tail[n-1]) && IsVowel(tail[n-2]) )// if vowel-consonant ending, then before must match type also
	{
		if (IsVowel(tail[n-3]) && IsVowel(D->word[len-3])){;}
		else if (!IsVowel(tail[n-3]) && !IsVowel(D->word[len-3])){;}
		else return;	// if 2 consonant ending, vowel before must match also
	}
	if (!IsVowel(tail[n-2]) && IsVowel(tail[n-1]) ) //if consonant vowel ending, then before must match character before also
	{
		if (tail[n-3] != D->word[len-3]) return;
	}

	if (FACTSET_COUNT(rhymeSet) >= 500) return; //   limit
	WORDP E = StoreWord((char*)"1");
	AddFact(spellSet,CreateFact(MakeMeaning(E,0),MakeMeaning(FindWord((char*)"word")),MakeMeaning(D,0),FACTTRANSIENT|FACTDUPLICATE));
}

static FunctionResult RhymeCode(char* buffer) 
{   
	int set = (impliedSet == ALREADY_HANDLED) ? 0 : impliedSet;
	SET_FACTSET_COUNT(set,0);
	rhymeSet = set;
	WalkDictionary(RhymeWord,(uint64)ARGUMENT(1));
	if (FACTSET_COUNT(set))
	{
		impliedSet = ALREADY_HANDLED;
		return NOPROBLEM_BIT;
	}

	return FAILRULE_BIT;
}

static FunctionResult FindTextCode(char* buffer) 
{ 
	bool insensitive = false;

	// what to search in
	char* target = ARGUMENT(1);
	if (!*target) return FAILRULE_BIT; 

	// find value
	char* find = ARGUMENT(2);
  	if (!*find) return FAILRULE_BIT;

	unsigned int start = atoi(ARGUMENT(3));
	if (start >= strlen(target)) return FAILRULE_BIT;

	char* buf = AllocateBuffer();
	strcpy(buf,target); // so we dont lose real blanks status
	if (*target != '_') Convert2Blanks(target); // if we explicitly request _, use it
	if (*find != '_') Convert2Blanks(find); // if we explicitly request _, use it

	if (strstr(ARGUMENT(4),(char*)"insensitive")) 
	{
		MakeLowerCase(find);
		MakeLowerCase(target);
	}

    char* found;
	size_t len = strlen(find);
	found = strstr(target+start,find); // case sensitive
    if (found)
	{
		unsigned int offset = found - target;
		char word[MAX_WORD_SIZE];
		sprintf(buffer,(char*)"%d",(int)(offset + len)); // where it ended (not part of it)
		sprintf(word,(char*)"%d",(int)offset);
		found = buf + offset;	// change reference frame back to original

		int count = 1;
		char* at = buf - 1; // original input before _ conversion
		bool nonblank = false;
		while (*++at)
		{
			bool quote = false;
			if (*at == '"' && *(at-1) != '\\') quote = !quote;
			else if (!quote && *at == ' ' && nonblank) // ignore contiguous blanks and string blanks
			{
				++count;	// will be next word
				nonblank = false;
			}
			if (*at != ' ') nonblank = true;
			if (at >= found) break;	// started with this word
		}

		SetUserVariable((char*)"$$findtext_start",word); // where it started
		sprintf(word,(char*)"%d",count);
		SetUserVariable((char*)"$$findtext_word",word); // where it started as words
	}
	FreeBuffer();
	if (!found)  return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

static FunctionResult ExtractCode(char* buffer) 
{ 
	// what to search in
	char* target = ARGUMENT(1);
	if (!*target) return FAILRULE_BIT;
	size_t len = strlen(target);
	bool startFromEnd = false;
	int offset = 0;
	char* arg2 = ARGUMENT(2);
	if (*arg2 == '-') {
		startFromEnd = true;
		++arg2;
	}
	char* arg3 = ARGUMENT(3);
	if (*arg3 == '-') offset = -1;
	else if (*arg3 == '+') offset = 1;
	if (offset != 0) ++arg3;
	if (!IsDigit(*arg2)) return FAILRULE_BIT;
	if (!IsDigit(*arg3)) return FAILRULE_BIT;
	unsigned int start = atoi(arg2);
	unsigned int end = atoi(arg3);
	if (startFromEnd) start = len - start;
	if (offset > 0) end = start + end;
	else if (offset < 0) {
		start = start - end;
		end = start + end;
	}
	if (start >= len) return FAILRULE_BIT;
	if (end > ( len +1)) end = len + 1;
	if (end < start) return FAILRULE_BIT; 
	strncpy(buffer,target+start,(end-start));
	buffer[(end-start)] = 0;
	return NOPROBLEM_BIT;
}

static FunctionResult SubstituteCode(char* buffer) 
{ 
	char* arg1 = ARGUMENT(1);
	bool wordMode = false;
	bool insensitive = false;
	if (*arg1 == '"') ++arg1;
	char word[MAX_WORD_SIZE];
	while (*arg1)
	{
		arg1 = ReadCompiledWord(arg1,word);
		if (!*word) break;
		if (*word == 'c' || *word == 'C') wordMode = false;
		if (*word == 'w' || *word == 'W') wordMode = true;
		if (*word == 'i' || *word == 'I') insensitive = true;
	}
	char* original = buffer;	// for debug
	// adjust substitution value
	char* substituteValue = ARGUMENT(4);
	size_t substituteLen = strlen(substituteValue);
	if (substituteLen > 1 && *substituteValue == '"' && substituteValue[substituteLen-1] == '"') // quoted expression means use the interior of it
	{
		substituteValue[substituteLen-1] = 0; 
		++substituteValue;
		substituteLen -= 2; 
	}
	// if (*substituteValue != '_') Convert2Blanks(substituteValue); // if we explicitly request _, use it  but Task_Test will be ruined

	// what to search in
	char copy[MAX_WORD_SIZE * 4];
	*copy = ' '; // protective leading blank for -1 test
	char* arg2 = ARGUMENT(2);
	strcpy(copy+1,arg2);
	char* target = copy+1;
	// target may be null string

	// find value
	char* find = ARGUMENT(3);
  	if (!*find) return FAILRULE_BIT;
	size_t findLen = strlen(find);
	if (findLen > 1 && *find == '"' && find[findLen-1] == '"') // find of a quoted thing means use interior
	{
		find[findLen-1] = 0; 
		++find;
		findLen -= 2; 
	}
	if (findLen == 0) // can never make headway
		return FAILRULE_BIT;

    char* found;
	bool changed = false;
	strcpy(buffer,target);
	target = buffer;
	size_t subslen = strlen(substituteValue);
	if (insensitive)
	{
		MakeLowerCase(find);
		MakeLowerCase(target);
	}
	while ((found = strstr(target,find))) // case sensitive
    {
		// no partial matches
		if (wordMode)
		{
			char c = found[findLen];	
			if (IsAlphaUTF8OrDigit(c) || IsAlphaUTF8OrDigit(*(found-1))) // skip nonword match
			{
				target = found + findLen;
				continue;
			}
		}
		changed = true;
		char buf[8000];
		strcpy(buf,found+findLen); // preserve what comes after the match
		strcpy(found,substituteValue);
		strcat(found,buf);
		target = found+subslen;
	}

	// check for FAIL request
	char* notify = ARGUMENT(5);
	if (*notify || impliedIf != ALREADY_HANDLED) return (changed) ? NOPROBLEM_BIT : FAILRULE_BIT; // if user wants possible failure result
	return NOPROBLEM_BIT;
}

static void SpellOne(WORDP D, uint64 data)
{
	char* match = (char*) data;
	if (FACTSET_COUNT(spellSet) >= 500) return; //   limit

	if (!(D->properties & (NOUN | VERB | ADJECTIVE | ADVERB | DETERMINER_BITS | PRONOUN_BITS | CONJUNCTION | PREPOSITION | AUX_VERB ))) return;
	if (D->word && !IsAlphaUTF8(*D->word)) return;
	if (D->properties & (NOUN_PROPER_SINGULAR | NOUN_PROPER_PLURAL | NOUN_TITLE_OF_ADDRESS)) return;	// want ordinary words
	if (strchr(D->word,'_') ) return; // only normal words and not multi words either
	if (MatchesPattern(D->word,match))
	{
		WORDP E = StoreWord((char*)"1");
		AddFact(spellSet,CreateFact(MakeMeaning(E,0),MakeMeaning(FindWord((char*)"word")),MakeMeaning(D,0),FACTTRANSIENT|FACTDUPLICATE));
	}
}

static unsigned int  Spell(char* match, unsigned int set)
{
	char pattern[MAX_WORD_SIZE];
	SET_FACTSET_COUNT(set,0);
	if (match[1] == '-') match[1] = 0;	// change 4-letter to 4
	MakeLowerCopy(pattern,match);
	spellSet = set;
	WalkDictionary(SpellOne,(uint64)pattern);
    return FACTSET_COUNT(set);
}

static FunctionResult SpellCode(char* buffer) //- locates up to 100 words in dictionary matching pattern and stores them as facts in @0
{
#ifdef INFORMATION
Fails if no words are found. Words must begin with a letter and be marked as a part of speech
(noun,verb,adjective,adverb,determiner,pronoun,conjunction,prepostion).

Not all words are found in the dictionary. The system only stores singular nouns and base
forms of verbs, adverbs, and adjectives unless it is irregular.

Pattern is a sequence of characters, with * matching 0 or more characters and . matching
exactly one. Pattern must cover the entire string. Pattern may be prefixed with a number, which
indicates how long the word must be. E.g.

^spell((char*)"4*")	# retrieves 100 4-letter words
^spell((char*)"3a*")  # retrieves 3-letter words beginning with "a"
^spell((char*)"*ing") # retrieves words ending in "ing" 
#endif

	return (Spell(ARGUMENT(1),0)) ? NOPROBLEM_BIT : FAILRULE_BIT;
}

static FunctionResult SexedCode(char* buffer)
{
	WORDP D = FindWord(ARGUMENT(1));
	if (!D || !(D->properties & (NOUN_HE|NOUN_SHE))) strcpy(buffer,ARGUMENT(4)); //   it 
	else if (D->properties & NOUN_HE) strcpy(buffer,ARGUMENT(2)); //   he
	else strcpy(buffer,ARGUMENT(3)); //   she
	return NOPROBLEM_BIT;
}

static FunctionResult TallyCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	if (!*arg1) return FAILRULE_BIT;
	WORDP D = StoreWord(arg1);
	char* arg2 = ARGUMENT(2);
	if (!*arg2 ) sprintf(buffer,(char*)"%d",GetWordValue(D));
	else SetWordValue(D,atoi(arg2));
	return NOPROBLEM_BIT;
}

static FunctionResult WordsCode(char* buffer)  
{
	WORDP set[100];
	char* arg1 = ARGUMENT(1);
	if (!*arg1)	return FAILRULE_BIT;
	if (*arg1 == '"')
	{
		++arg1;
		size_t len = strlen(arg1);
		if (arg1[len-1] == '"') arg1[len-1] = 0;	 //remove quotes
	}
	int i = GetWords(arg1,set,false);
	MEANING M = MakeMeaning(StoreWord("words"));
	while (i)
	{
		FACT* F = CreateFact(MakeMeaning(set[--i]), M,M,FACTTRANSIENT); 
		if (impliedSet != ALREADY_HANDLED)
		{
			AddFact(impliedSet,F);
		}
	}

	impliedSet = ALREADY_HANDLED;	
	return NOPROBLEM_BIT;
}

static bool WordPropogate(MEANING at, MEANING find)
{
	if (at == find) return true;
	WORDP D = Meaning2Word(at);
	if (D->inferMark == inferMark) return false;
	D->inferMark = inferMark;
	FACT* F = GetSubjectHead(D);
	// propogate to find set
	while(F) 
	{
		if (F->verb == Mmember && WordPropogate(F->object,find)) return true;
		F = GetSubjectNext(F);
	}

	return false;
}

static FunctionResult WordInConceptCode(char* buffer)  
{
	WORDP set[100];
	char* arg1 = ARGUMENT(1);
	if (!*arg1)	return FAILRULE_BIT;
	if (*arg1 == '"')
	{
		++arg1;
		size_t len = strlen(arg1);
		if (arg1[len-1] == '"') arg1[len-1] = 0;	 //remove quotes
	}
	char* arg2 = ARGUMENT(2);
	if (*arg2 != '~') return FAILRULE_BIT;
	MEANING find = MakeMeaning(StoreWord(arg2));
	int i = GetWords(arg1,set,false);
	NextInferMark();
	while (i)
	{
		MEANING M = MakeMeaning(set[--i]); 
		WORDP D = Meaning2Word(M);
		FACT* F = GetSubjectNondeadHead(D);
		// propogate to find set
		while(F) 
		{
			if (F->verb == Mmember && WordPropogate(F->object,find))
			{
				sprintf(buffer,"%s",Meaning2Word(F->subject)->word);
				return NOPROBLEM_BIT;
			}
			F = GetSubjectNondeadNext(F);
		}
	}
	if (ARGUMENT(3)) return FAILRULE_BIT;

	return NOPROBLEM_BIT;
}

#ifndef DISCARDCOUNTER
static FunctionResult WordCountCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	if (!*arg1)	return FAILRULE_BIT;
	WORDP D = StoreWord(arg1);
	char* arg2 = ARGUMENT(2);
	if (!*arg2 ) sprintf(buffer,(char*)"%d",D->counter);
	else D->counter = atoi(arg2);
	return NOPROBLEM_BIT;
}
#endif

static char* xbuffer;

static void DWalker(WORDP D,uint64 fn)
{
	if (*D->word == USERVAR_PREFIX || *D->word == ':' || *D->word == '^' || *D->word == '~' || *D->word == SYSVAR_PREFIX || *D->word == ENDUNIT || *D->word == '"') return; // not real stuff
	if (D->internalBits & HAS_SUBSTITUTE) return;
	if (D->properties & (PUNCTUATION |COMMA|PAREN|QUOTE )) return; // " will cause a crash
	if (strchr(D->word,' ')) return;
	FunctionResult result;
	char* function = (char*)fn;
	char word[MAX_WORD_SIZE];
	sprintf(word,(char*)"( %s )",D->word);
	DoFunction(function,word,xbuffer,result); 
	xbuffer += strlen(xbuffer);
}

static FunctionResult WalkDictionaryCode(char* buffer)
{
	FunctionResult result;
	xbuffer = buffer;
	char fn[MAX_WORD_SIZE];
	char* function = ReadShortCommandArg(ARGUMENT(1),fn,result,OUTPUT_NOQUOTES|OUTPUT_EVALCODE|OUTPUT_NOTREALBUFFER); 
	if (result != NOPROBLEM_BIT) return result;
	function = fn;
	if (*function == '\'') ++function; // skip over the ' 
	WalkDictionary(DWalker,(uint64)function);
	return NOPROBLEM_BIT;
}


//////////////////////////////////////////////////////////
/// DICTIONARY
//////////////////////////////////////////////////////////

static FunctionResult GetPropertyCodes(char* who,char* ptr, uint64 &val, uint64 &sysval,unsigned int &internalBits, unsigned int &parseBits)
{
	while (ptr && *ptr)
	{
		char arg[MAX_WORD_SIZE];
		ptr = ReadCompiledWord(ptr,arg);
		if (!*arg || *arg == ')') break;
		if (*arg == '^' && IsDigit(arg[1])) strcpy(arg,callArgumentList[atoi(arg+1)+fnVarBase]);
		if (!stricmp(arg,(char*)"CONCEPT"))  
		{
			if (*who != '~') return FAILRULE_BIT; // must be a concept name
			internalBits = CONCEPT;
		}
	
		// fact marks
		else if (IsDigit(arg[0])) ReadInt64(arg,(int64&)sysval);
		else 
		{
			uint64 bits = FindValueByName(arg);
			if (bits) val |= bits;
			else {
				bits = FindSystemValueByName(arg);
				if (!bits) 
				{
					bits = FindParseValueByName(arg);
					if (!bits) 
					{
						bits = FindMiscValueByName(arg); // goes to sysval
						if (!bits) Log(STDTRACELOG,(char*)"Unknown addproperty value %s\r\n",arg);
						else sysval |= bits;
					}
					else parseBits |= bits;
				}
				else sysval |= bits;
			}
		}
	}
	return (!sysval && !val && !internalBits) ? FAILRULE_BIT : NOPROBLEM_BIT;
}

static FunctionResult RemoveInternalFlagCode(char* buffer)
{
	char* ptr = ARGUMENT(1);
	char arg1[MAX_WORD_SIZE];
	FunctionResult result = NOPROBLEM_BIT;
	ptr = ReadShortCommandArg(ptr,arg1,result,OUTPUT_NOQUOTES|OUTPUT_EVALCODE|OUTPUT_NOTREALBUFFER);
	if (result != NOPROBLEM_BIT) return result;
	if (!*arg1) return FAILRULE_BIT;
	WORDP D = FindWord(arg1,0,PRIMARY_CASE_ALLOWED); // add property to dictionary word
	if (!D) return FAILRULE_BIT;
	char* arg2 = ARGUMENT(2);
	if (!stricmp(arg2,(char*)"HAS_SUBSTITUTE"))
	{
		D->internalBits &= -1 ^ HAS_SUBSTITUTE;
	}
	else return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

static FunctionResult AddPropertyCode(char* buffer)
{
	char* ptr = ARGUMENT(1);
	char arg1[MAX_WORD_SIZE];
	FunctionResult result = NOPROBLEM_BIT;
	if (*ptr == '@') ptr = ReadCompiledWord(ptr,arg1); // dont eval a set
	else ptr = ReadShortCommandArg(ptr,arg1,result,OUTPUT_NOQUOTES|OUTPUT_EVALCODE|OUTPUT_NOTREALBUFFER);
	if (result != NOPROBLEM_BIT) return result;
	if (!*arg1) return FAILRULE_BIT;
	WORDP D = NULL;
	int store = 0;
	unsigned int count = 0;
	if (*arg1 == '@') // add property to all facts in set either on a field or fact as a whole
	{
		store = GetSetID(arg1);
		if (store == ILLEGAL_FACTSET) return FAILRULE_BIT;
		count =  FACTSET_COUNT(store);
	}
	else  D = StoreWord(arg1,0); // add property to dictionary word
	char arg3 = *GetSetType(arg1);

	uint64 val = 0;
	uint64 sysval = 0;
	unsigned int internalBits = 0;
	unsigned int parseBits = 0;
	result = GetPropertyCodes(arg1,ptr,val,sysval,internalBits,parseBits);
	if (result != NOPROBLEM_BIT) return result;
	if (!compiling) 
		dictionaryBitsChanged = true;
	if (D) // add to dictionary entry
	{
		if (val & NOUN_SINGULAR && D->internalBits & UPPERCASE_HASH) //make it right
		{
			val ^= NOUN_SINGULAR;
			val |= NOUN_PROPER_SINGULAR;
		}
		AddProperty(D,val);
		AddSystemFlag(D,sysval);
		if (internalBits & CONCEPT) AddInternalFlag(D,(unsigned int)(CONCEPT|buildID)); 
	}
	else if (*arg1 == '@') // add to all properties of fact set
	{
		for (unsigned int i = 1; i <= count; ++i)
		{
			FACT* F = factSet[store][i];
			if (arg3 == 's') D = Meaning2Word(F->subject); 
			else if (arg3 == 'v') D = Meaning2Word(F->verb);
			else if (arg3 == 'o') D = Meaning2Word(F->object);
			else
			{
				F->flags |= sysval;
				if (trace & TRACE_INFER && CheckTopicTrace()) TraceFact(F);
			}
			if (D)
			{
				uint64 val1 = val;
				if (val1 & NOUN_SINGULAR && D->internalBits & UPPERCASE_HASH) //make it right
				{
					val1 ^= NOUN_SINGULAR;
					val1 |= NOUN_PROPER_SINGULAR;
				}
				AddProperty(D,val1);
				AddSystemFlag(D,sysval);
				if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" %s\n",D->word);
			}
		}
	}
	return NOPROBLEM_BIT;
}

static FunctionResult DefineCode(char* buffer)
{ 
	char* w = ARGUMENT(1);
	WORDP D = FindWord(w,0);
	if (!D) return NOPROBLEM_BIT;

	bool noun = false;
	bool verb = false;
	bool adjective = false;
	bool adverb = false;
	char* which = ARGUMENT(2);
	bool all = false;
	if (!stricmp(which,(char*)"all")) 
	{
		all = true;
		which = "";
	}
	if (!stricmp(ARGUMENT(3),(char*)"all")) all = true;

	for (int i = GetMeaningCount(D); i >= 1; --i)
	{
		MEANING T = GetMaster(GetMeaning(D,i)) | GETTYPERESTRICTION(GetMeaning(D,i));
		int index = Meaning2Index(T);
		WORDP E = Meaning2Word(T);
		char* gloss = GetGloss(E,index);
		unsigned int restrict = GETTYPERESTRICTION(T);
		if (gloss && restrict & NOUN && !noun && (!*which || !stricmp(which,(char*)"NOUN")))
		{
			if (verb) sprintf(buffer,(char*)"As a noun it means %s. ",gloss);
			else sprintf(buffer,(char*)"The noun %s means %s. ",ARGUMENT(1),gloss);
			if (!all) noun = true;
			else strcat(buffer,(char*)"\n");
			buffer += strlen(buffer);

        }
		else if (gloss && restrict & VERB && !verb && (!*which || !stricmp(which,(char*)"VERB")))
		{
			if (noun) sprintf(buffer,(char*)"As a verb it means %s. ",gloss);
			else sprintf(buffer,(char*)"The verb %s means %s. ",ARGUMENT(1),gloss);
			if (!all) verb = true;
			else strcat(buffer,(char*)"\n");
			buffer += strlen(buffer);
        }
		else if (gloss && restrict & ADJECTIVE && !noun && !verb && !adjective && (!*which  || !stricmp(which,(char*)"ADJECTIVE")))
		{
			sprintf(buffer,(char*)"The adjective %s means %s. ",ARGUMENT(1),gloss);
			if (!all) adjective = true;
			else strcat(buffer,(char*)"\n");
			buffer += strlen(buffer);
        }
 		else if (gloss && restrict & ADVERB && !adverb && !noun && !verb && !adjective && (!*which  || !stricmp(which,(char*)"ADVERB")))
		{
			sprintf(buffer,(char*)"The adverb %s means %s. ",ARGUMENT(1),gloss);
			if (!all) adverb = true;
    		else strcat(buffer,(char*)"\n");
			buffer += strlen(buffer);
	    }
	}
    return NOPROBLEM_BIT;
}

static void ArgFlags(uint64& properties, uint64& flags,unsigned int & internalbits)
{
	char* arg2 = ARGUMENT(2);
	char* arg3 = ARGUMENT(3);
	properties = FindValueByName(arg2);
	properties |= FindValueByName(arg3);
	properties |= FindValueByName(ARGUMENT(4));
	properties |= FindValueByName(ARGUMENT(5));
	properties |= FindValueByName(ARGUMENT(6));

	flags = FindSystemValueByName(arg2);
	flags |= FindSystemValueByName(arg3);
	flags |= FindSystemValueByName(ARGUMENT(4));
	flags |= FindSystemValueByName(ARGUMENT(5));
	flags |= FindSystemValueByName(ARGUMENT(6));

	internalbits = 0;
	if (!stricmp(arg2,(char*)"CONCEPT") || !stricmp(arg3,(char*)"CONCEPT") || !stricmp(ARGUMENT(4),(char*)"CONCEPT") || 
		!stricmp(ARGUMENT(5),(char*)"CONCEPT") || !stricmp(ARGUMENT(6),(char*)"CONCEPT"))
	{
		internalbits |= CONCEPT;
	}
	if (!stricmp(arg2,(char*)"TOPIC") || !stricmp(arg3,(char*)"TOPIC") || !stricmp(ARGUMENT(4),(char*)"TOPIC") || 
		!stricmp(ARGUMENT(5),(char*)"TOPIC") || !stricmp(ARGUMENT(6),(char*)"TOPIC"))
	{
		internalbits |= TOPIC;
	}
}

static FunctionResult HasAnyPropertyCode(char* buffer)
{
	WORDP canonical = NULL;
	uint64 dprop;
	uint64 dsys;
	char* arg = ARGUMENT(1);
	WORDP D = FindWord(arg,0,PRIMARY_CASE_ALLOWED);
	WORDP revise;
	if (!D)  GetPosData(-1,arg,revise,D,canonical,dprop,dsys);  // WARNING- created dict entry if it doesnt exist yet
	else 
	{
		dsys = D->systemFlags;
		dprop = D->properties;
	}
	uint64 properties;
	uint64 flags;
	unsigned int internalbits;
	ArgFlags(properties,flags,internalbits);
	if ((internalbits & CONCEPT) && (D->internalBits & TOPIC))  internalbits ^= CONCEPT;
	return (dprop & properties || dsys & flags || D->internalBits & internalbits) ? NOPROBLEM_BIT : FAILRULE_BIT;
}

static FunctionResult HasAllPropertyCode(char* buffer)
{
	WORDP canonical = NULL;
	uint64 dprop = 0;
	uint64 dsys;
	char* arg = ARGUMENT(1);
	WORDP D = FindWord(arg,0,PRIMARY_CASE_ALLOWED);
	WORDP revise;
	if (!D)  GetPosData(-1,arg,revise,D,canonical,dprop,dsys); 
	else 
	{
		dsys = D->systemFlags;
		dprop = D->properties;
	}
	uint64 properties;
	uint64 flags;
	unsigned int internalbits;
	ArgFlags(properties,flags,internalbits);
	if (!flags && !properties) return FAILRULE_BIT;
	if ((internalbits & CONCEPT) && (D->internalBits & TOPIC)) return FAILRULE_BIT;
	return ((dprop & properties) == properties && (dsys & flags) == flags && (D->internalBits & internalbits) == internalbits) ? NOPROBLEM_BIT : FAILRULE_BIT; // has all the bits given
}

static FunctionResult RemovePropertyCode(char* buffer)
{
	char* ptr = ARGUMENT(1);
	char arg1[MAX_WORD_SIZE];
	FunctionResult result = NOPROBLEM_BIT;
	if (*ptr == '@') ptr = ReadCompiledWord(ptr,arg1); // dont eval a set
	else ptr = ReadShortCommandArg(ptr,arg1,result,OUTPUT_NOQUOTES|OUTPUT_EVALCODE|OUTPUT_NOTREALBUFFER);
	if (result != NOPROBLEM_BIT) return result;
	char arg3 = *GetSetType(arg1);
	if (!*arg1) return FAILRULE_BIT;
	WORDP D = NULL;
	int store = 0;
	unsigned int count = 0;
	if (*arg1 == '@') 
	{
		store = GetSetID(arg1);
		if (store == ILLEGAL_FACTSET) return FAILRULE_BIT;
		count = FACTSET_COUNT(store);
	}
	else  D = StoreWord(arg1,0); 

	uint64 val = 0;
	uint64 sysval = 0;
	unsigned int internalBits = 0;
	unsigned int parseBits = 0;
	result = GetPropertyCodes(arg1,ptr,val,sysval,internalBits,parseBits);
	if (result != NOPROBLEM_BIT) return result;
	if (D) // remove to dictionary entry
	{
		RemoveProperty(D,val);
		RemoveSystemFlag(D,sysval);
	}
	else // remove to all properties of set
	{
		for (unsigned int i = 1; i <= count; ++i)
		{
			FACT* F = factSet[store][i];
			if (arg3 == 's') D = Meaning2Word(F->subject);
			else if (arg3 == 'v') D = Meaning2Word(F->verb);
			else if (arg3 == 'o') D = Meaning2Word(F->object); 
			else  
			{
				F->flags &= -1 ^ sysval; // uses miscflags retrieval
				if (trace & TRACE_INFER && CheckTopicTrace()) TraceFact(F);
			}
			if (D)
			{
				RemoveProperty(D,val);
				RemoveSystemFlag(D,sysval);
				if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" %s\n",D->word);
			}
		}
	}
	return NOPROBLEM_BIT;
}


//////////////////////////////////////////////////////////
/// MULTIPURPOSE
//////////////////////////////////////////////////////////

static FunctionResult DisableCode(char* buffer) 
{
	char* arg1 = ARGUMENT(1);
	char* arg2 = ARGUMENT(2);
	if (!stricmp(arg1,(char*)"topic"))
	{
		if (!*arg2) return FAILRULE_BIT;
		int id = FindTopicIDByName(ARGUMENT(2));
		if (id) 
		{
			if (GetTopicFlags(id) & TOPIC_SYSTEM) return FAILRULE_BIT;
			if (!(GetTopicFlags(id) & TOPIC_BLOCKED)) AddTopicFlag(id,TOPIC_BLOCKED|TOPIC_USED);
			return NOPROBLEM_BIT;       
		}
	}
	else if (!stricmp(arg1,(char*)"rule")) // 1st one found
	{
		if (planning) return FAILRULE_BIT;
		int id = 0;
		int topic = currentTopicID;
		bool fulllabel;
		bool crosstopic;
		char* rule;
		char* dot = strchr(arg2,'.');
		if (*arg2 == '~') 
		{
			rule = currentRule;
			id = currentRuleID;
		}
		else if (dot && IsDigit(dot[1])) rule = GetRuleTag(topic,id,arg2);
		else rule = GetLabelledRule(topic,arg2,(char*)"",fulllabel,crosstopic,id,currentTopicID);
		if (!rule) return FAILRULE_BIT;
		SetRuleDisableMark(topic,id);
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"rejoinder") || !stricmp(arg1,(char*)"outputrejoinder"))
	{
		outputRejoinderRuleID = NO_REJOINDER;
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"inputrejoinder"))
	{
		inputRejoinderRuleID = NO_REJOINDER;
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"save")) 
	{
		stopUserWrite = true;
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(arg1,(char*)"write")) 
	{
		int set = GetSetID(ARGUMENT(1));
		if (set == ILLEGAL_FACTSET) return FAILRULE_BIT;
		setControl &= -1ll ^ (1ull << set);
		return NOPROBLEM_BIT;
	}

	return FAILRULE_BIT;
}

static FunctionResult EnableCode(char* buffer)
{
	char* arg2 = ARGUMENT(2);
	if (!stricmp(ARGUMENT(1),(char*)"topic"))
	{
		 //   topic name to enable
		if (!*arg2) return FAILRULE_BIT;
		if (!stricmp(arg2,(char*)"all"))
		{
			for (int start = 1; start <= numberOfTopics; ++start) 
			{

				if (GetTopicFlags(start) & TOPIC_SYSTEM) continue;
				RemoveTopicFlag(start,TOPIC_BLOCKED);
			}
			return NOPROBLEM_BIT;
		}
		int id = FindTopicIDByName(arg2);
		if (!id) return FAILRULE_BIT;
		if (GetTopicFlags(id) & TOPIC_SYSTEM) return FAILRULE_BIT;
		RemoveTopicFlag(id,TOPIC_BLOCKED);
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(ARGUMENT(1),(char*)"rule")) 
	{
		if (planning) return FAILRULE_BIT;
		int id = 0;
		int topic = currentTopicID;
		bool fulllabel;
		bool crosstopic;
		char* rule;
		char* dot = strchr(arg2,'.');
		if (dot && IsDigit(dot[1])) rule = GetRuleTag(topic,id,arg2);
		else rule = GetLabelledRule(topic,arg2,ARGUMENT(3),fulllabel,crosstopic,id,currentTopicID);
		if (!rule) return FAILRULE_BIT;
		UndoErase(rule,topic,id);
		AddTopicFlag(topic,TOPIC_USED); 
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(ARGUMENT(1),(char*)"save")) 
	{
		stopUserWrite = false;
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(ARGUMENT(1),(char*)"usedrules")) // rules turned off this volley as we went along
	{
		FlushDisabled();
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(ARGUMENT(1),(char*)"write")) 
	{
		int set = GetSetID(ARGUMENT(2));
		if (set == ILLEGAL_FACTSET) return FAILRULE_BIT;
		setControl |= (1ull << set);
		return NOPROBLEM_BIT;
	}
	return FAILRULE_BIT;
}

FunctionResult LengthCode(char* buffer)
{
	char* word = ARGUMENT(1);
	if (*word == '@' && IsDigit(word[1])) // how many facts in factset
	{
		int store = GetSetID(word);
		if (store == ILLEGAL_FACTSET) sprintf(buffer, (char*)"%d", (int)strlen(word)); // characters in word
		else 
		{
			unsigned int count = FACTSET_COUNT(store);
			sprintf(buffer, (char*)"%d", count);
		}
	}
	else if (*word == '~') // how many top level members in set
	{
		WORDP D = FindWord(word,0);
		if (!D) return FAILRULE_BIT;
		int count = 0;
		FACT* F = GetObjectNondeadHead(D);
		while (F)
		{
			if (F->verb == Mmember) ++count;
			F = GetObjectNondeadNext(F);
		}
		sprintf(buffer,(char*)"%d",count);
	}
	else if (!strnicmp(word,(char*)"ja-",3) || !strnicmp(word,(char*)"jo-",3)) // elements in a json array or object
	{
		int count = 0;
		WORDP D = FindWord(word);
		if (D)
		{
			FACT* F = GetSubjectNondeadHead(D); 
			while (F)
			{
				++count;
				F = GetSubjectNondeadNext(F); 
			}
		}
		else return FAILRULE_BIT;	// it doesnt exist
		sprintf(buffer,(char*)"%d",count);
	}
	else if (!*word) strcpy(buffer,(char*)"0"); // NULL has 0 length (like a null value array)
	else sprintf(buffer,(char*)"%d",(int)strlen(word)); // characters in word
	return NOPROBLEM_BIT;
}

static FunctionResult NextCode(char* buffer)
{
	char* ptr = ARGUMENT(1); // GAMBIT or RESPONDER or RULE OR FACT or INPUT
	char arg1[MAX_WORD_SIZE];
	char arg2[MAX_WORD_SIZE];
	ptr = ReadCompiledWord(ptr,arg1);
	ReadCompiledWord(ptr,arg2);
	if (stricmp(arg1,(char*)"FACT") || *arg2 != '@') // eval all but FACT @1subjecct
	{
		FunctionResult result;
		ReadCommandArg(ptr,arg2,result,OUTPUT_NOTREALBUFFER|OUTPUT_EVALCODE|OUTPUT_UNTOUCHEDSTRING,MAX_WORD_SIZE);
	}
	if (!stricmp(arg1,(char*)"LOOP"))  
		return NEXTLOOP_BIT;
	if (!stricmp(arg1,(char*)"FACT")) 
	{
		ARGUMENT(1) = AllocateInverseString(arg2);
		return FLR(buffer,(char*)"n");
	}
	if (!stricmp(arg1,(char*)"INPUT") || !stricmp(arg1,(char*)"SENTENCE")) // same meaning
	{
		SAVEOLDCONTEXT()
		*buffer = 0;
		while (ALWAYS) // revise inputs until prepass doesnt change them
		{
			nextInput = SkipWhitespace(nextInput);
			if (!*nextInput) return FAILRULE_BIT;
			PrepareSentence(nextInput,true,true);
			if (!wordCount && (*nextInput | (responseIndex != 0))) // ignore this input
			{
				RESTOREOLDCONTEXT()
				return NOPROBLEM_BIT; 
			}
			if (!PrepassSentence(GetUserVariable((char*)"$cs_prepass"))) break; // it was quiet
		}
 		if (!wordCount) return FAILRULE_BIT;
		++inputSentenceCount; //  sentence id of volley has moved on
		RESTOREOLDCONTEXT()
	}
	else  // gambit, responder, rule, REJOINDER
	{
		bool gambit = (*arg1 == 'G' || *arg1 == 'g');
		bool responder = !stricmp(arg1,(char*)"responder");
		bool rejoinder = !stricmp(arg1,(char*)"rejoinder");
		int topic = currentTopicID;
		int id;
		bool fulllabel = false;
		bool crosstopic = false;
		char* rule;
		char* dot = strchr(arg2,'.');
		if (dot && IsDigit(dot[1])) rule = GetRuleTag(topic,id,arg2);
		else rule = GetLabelledRule(topic,arg2,arg2,fulllabel,crosstopic,id,currentTopicID);
		if (!rule) return FAILRULE_BIT; // unable to find labelled rule 

		char* data = rule;
		while (data)
		{
			data = FindNextRule( (gambit || responder) ? NEXTTOPLEVEL : NEXTRULE,data,id);
			if (!data || !*data) break;
		
			if (gambit && TopLevelGambit(data)) break;
			else if (responder &&  (TopLevelStatement(data) || TopLevelQuestion(data))) break; 
			else if (rejoinder && Rejoinder(data)) break;
			else if (rejoinder) return FAILRULE_BIT;	// no more rejoinders
			else if (!gambit && !responder && !rejoinder) break;	// any next rule
		}
		if (!data || !*data) return FAILRULE_BIT;
		sprintf(buffer,(char*)"%s.%d.%d",GetTopicName(topic),TOPLEVELID(id),REJOINDERID(id));
	}
	return NOPROBLEM_BIT;
}

static FunctionResult FLRCodeR(char* buffer)
{
	char* arg  = ARGUMENT(1);
	char word[MAX_BUFFER_SIZE];
	GetPossibleFunctionArgument(arg,word); // pass thru or convert
	arg = word;
	ARGUMENT(1) = AllocateInverseString(arg); // put it back in case it changed

	if (*arg == USERVAR_PREFIX) arg = GetUserVariable(arg);
	else if (*arg == '_') arg =  GetwildcardText(GetWildcardID(arg), true);

	if (*arg == '@') return FLR(buffer,(char*)"r");
	else if (*arg == '~')  return RandomMember(buffer,arg);
	else if (!strnicmp(arg,"jo-",3) || !strnicmp(arg,"ja-",3))
	{
		WORDP X = FindWord(arg);
		FACT* F = (X) ? GetSubjectNondeadHead(X) : NULL;
		int count = 0;
		while (F)
		{
			++count;
			F = GetSubjectNondeadNext(F);
		}
		count = random(count); // which one to pick (0 ..n)
		F = (X) ? GetSubjectNondeadHead(X) : NULL;
		while (F)
		{
			if (count-- == 0) break; // select this one
			F = GetSubjectNondeadNext(F);
		}
		if (!F) return FAILRULE_BIT;
		sprintf(buffer,"%d",Fact2Index(F));
		return NOPROBLEM_BIT;
	}
	else return FAILRULE_BIT;
}

static FunctionResult NthCode(char* buffer)
{
	char* arg = ARGUMENT(1);
	char arg1[MAX_WORD_SIZE];
	char arg2[MAX_WORD_SIZE];
	arg = GetPossibleFunctionArgument(arg,arg1); // pass thru or convert
	FunctionResult result;
	ReadCommandArg(arg,arg2,result,OUTPUT_NOTREALBUFFER|OUTPUT_NOCOMMANUMBER|ASSIGNMENT); 
	if (result != NOPROBLEM_BIT) return result;

	if (*arg1 == USERVAR_PREFIX) strcpy(arg1,GetUserVariable(arg1));
	else if (*arg1 == '_') strcpy(arg1, GetwildcardText(GetWildcardID(arg1), true));
		
	int count = 0;
	int n = atoi(arg2); // which one

	if (*arg1 == '~') // nth member of set, counting from 0
	{
		WORDP D = FindWord(arg1);
		FACT* F = GetObjectNondeadHead(D);
		while (F) // back to front order, need to invert, count how many
		{
			++count;
			F = GetObjectNondeadNext(F);
		}
		if (count <= n) return FAILRULE_BIT; // not enough or bad count
		F = GetObjectNondeadHead(D);
		while (F && --n >= 0) F = GetObjectNondeadNext(F); // back to front order, need to invert
		strcpy(buffer,Meaning2Word(F->subject)->word);
		return NOPROBLEM_BIT;
	}
	else if (!strnicmp(arg1,"jo-",3) || !strnicmp(arg1,"ja-",3))
	{
		WORDP D = FindWord(arg1);
		FACT* F = GetSubjectNondeadHead(D);
		while (F) // back to front order, need to invert, count how many
		{
			++count;
			F = GetSubjectNondeadNext(F);
		}
		if (count <= n) return FAILRULE_BIT; // not enough or bad count
		F = GetSubjectNondeadHead(D);
		while (F && ++n < count) F = GetSubjectNondeadNext(F); // back to front order, need to invert
		sprintf(buffer,"%d",Fact2Index(F));
		return NOPROBLEM_BIT;
	}

	ARGUMENT(1) = AllocateInverseString(arg1); // put it back in case it changed
	if (*arg1 == '@') return FLR(buffer,arg2);
	else return FAILRULE_BIT;
}

static FunctionResult ResetCode(char* buffer)
{
	char* word = ARGUMENT(1);
	if (!stricmp(word,(char*)"USER"))
	{
		if (planning) return FAILRULE_BIT;
		int depth = globalDepth; // reset clears depth, but we are still in process so need to restore it
		ResetUser(buffer);
		globalDepth = depth;
		*buffer = 0;
		ProcessInput(buffer);

#ifndef DISCARDTESTING
		wasCommand = COMMANDED;	// lie so system will save revised user file
#endif
		return ENDINPUT_BIT;
	}
	else if (!stricmp(word,(char*)"TOPIC"))
	{
		word = ARGUMENT(2);
		int topic;
		if (*word == '*' && word[1] == 0) // all topics
		{
			if (!all) ResetTopics(); 
		}
		else if ((topic = FindTopicIDByName(word))) ResetTopic(topic);
		else return FAILRULE_BIT;
		return NOPROBLEM_BIT;
	}
	else if (!stricmp(word,(char*)"OUTPUT"))
	{
		responseIndex = 0;
		return NOPROBLEM_BIT;
	}
	else if (*word == '@') // reset a fact set for browsing sequentially
	{
		int store = GetSetID(word);
		if (store == ILLEGAL_FACTSET) return FAILRULE_BIT;
		factSetNext[store] = 0;
		if (trace & TRACE_INFER) Log(STDTRACELOG,(char*)" @%d[%d] ",store,FACTSET_COUNT(store));
		return NOPROBLEM_BIT;
	}
	return FAILRULE_BIT;
}

//////////////////////////////////////////////////////////
/// EXTERNAL ACCESS
//////////////////////////////////////////////////////////

static FunctionResult ExportCode(char* buffer)
{
	char* set = ARGUMENT(2);
	char* append = ARGUMENT(3); // optional 3rd argument is append or overwrite
	if (*set == '@') return (ExportFacts(ARGUMENT(1),GetSetID(set),append)) ? NOPROBLEM_BIT : FAILRULE_BIT;
	else if (!strnicmp(set,"ja-",3) || !strnicmp(set,"jo-",3) || !*set) return ExportJson(ARGUMENT(1),set,append);
	else return FAILRULE_BIT;
}

static FunctionResult ImportCode(char* buffer)
{
	return (ImportFacts(buffer,ARGUMENT(1),ARGUMENT(2),ARGUMENT(3),ARGUMENT(4))) ? NOPROBLEM_BIT : FAILRULE_BIT;
}

static FunctionResult GetRemoteFileCode(char* buffer)
{
    FunctionResult result = FAILRULE_BIT;
	char name[MAX_WORD_SIZE];
	strcpy(name,ARGUMENT(1));
	char* arg2 = ARGUMENT(2);	// optional specifier - mongo x y z
#ifndef DISCARDMONGO
	if (!stricmp(arg2,"mongo")) // transfer mongo to local
	{
		// get the mongo data.
		if (filesystemOverride != MONGOFILES) // currently not connected
		{
			ARGUMENT(1) = ARGUMENT(3); // set up server init arguments  URL
			ARGUMENT(2) = ARGUMENT(4); // DB
			ARGUMENT(3) = ARGUMENT(5); // Collection
			ARGUMENT(4) = ARGUMENT(6); // what kind of data  topic, ltm, log
			result = MongoInit(NULL); // init filesys collection
			if (result != NOPROBLEM_BIT) return result;
			ARGUMENT(2) = ARGUMENT(4);	// for mongoGetDoc
		} // Otherwise we are using mongo filesystem values
		else ARGUMENT(2) = ARGUMENT(3);
		char* buffer = GetFreeCache();
		
		char* dot = strchr(name,'.');
		if (dot) *dot = 0;	 // terminate any suffix, not legal in mongo key
		result = mongoGetDocument(name,buffer,(userCacheSize - MAX_USERNAME),false); // can take argument(2)
		if (dot) *dot = '.';	 
		
		if (result != NOPROBLEM_BIT)
		{
			FreeUserCache(); // WE CAN still use the data buffer
			return result;
		}
		if (filesystemOverride != MONGOFILES) MongoClose(NULL); // close filesys collection

		// write to local filesystem
		FILE* out = FopenBinaryWrite(name);
		if (out)
		{
			fprintf(out,"%s",ARGUMENT(1));	 // write out the user header (calling mongo removed .xxx)
			char one[3];
			*one = 0;
			fwrite(one,1,1,out); // write out the string end.
			fwrite(buffer,1,strlen(buffer),out); // write out the actual data.
			FClose(out);
		}
		FreeUserCache(); 
		return (out) ? NOPROBLEM_BIT : FAILRULE_BIT;
	}
	else 
#endif
    return result;
}

static FunctionResult PopenCode(char* buffer)
{
	char   psBuffer[MAX_WORD_SIZE];
	FILE   *pPipe;
	char* arg;
	arg = ARGUMENT(1);

	// convert \" to " within params
	if (*arg == '"') ++arg;
	size_t len = strlen(arg);
	if (arg[len-1] == '"') arg[len-1] = 0;
	char* fix;
	while ((fix = strchr(arg,'\\'))) memmove(fix,fix+1,strlen(fix)); // remove protective backslash
	
	// adjust function reference name
	char* function = ARGUMENT(2);
	if (*function == '\'') ++function; // skip over the ' 

#ifdef WIN32
   if( (pPipe = _popen(arg,(char*)"rb")) == NULL ) return FAILRULE_BIT; //  "dir *.c /on /p", "rt" 
#else
   if( (pPipe = popen(arg,(char*)"r")) == NULL ) return FAILRULE_BIT; 
#endif
   psBuffer[0] = '(';
   psBuffer[1] = ' ';
   psBuffer[2] = '"'; // stripable string marker
   psBuffer[3] = ENDUNIT; // stripable string marker
   while( !feof( pPipe ) )
   {
		psBuffer[4] = 0;
		if( fgets( psBuffer+4, MAX_WORD_SIZE - 5, pPipe ) != NULL )
		 {
			FunctionResult result;
			char* p;
			while ((p = strchr(psBuffer,'\r'))) *p = ' ';
			while ((p = strchr(psBuffer,'\n'))) *p = ' ';
			strcat(psBuffer,(char*)"`\" )"); // trailing quote and ending paren
			if (*function == '^') DoFunction(function,psBuffer,buffer,result); 
			buffer += strlen(buffer);
			if (result == UNDEFINED_FUNCTION) result = FAILRULE_BIT;
		}
   }
#ifdef WIN32
   _pclose( pPipe );
#else
   pclose( pPipe );
#endif
   return NOPROBLEM_BIT;
}

static FunctionResult TCPOpenCode(char* buffer)
{
#ifdef INFORMATION
// POST http://de.sempar.ims.uni-stuttgart.de/parse HTTP/1.1
// Accept: text/html, application/xhtml+xml, */*
// Host: de.sempar.ims.uni-stuttgart.de
// Content-Type: application/x-www-form-urlencoded
// Content-Length: 31
//
// sentence=ich+bin&returnType=rdf

// e.g.  TCPOpen(POST "http://de.sempar.ims.uni-stuttgart.de/parse" "sentence=ich+bin&returnType=rdf" 'myfunc)
#endif

#ifdef DISCARDTCPOPEN
	char* msg = "tcpopen not available\r\n";
	SetUserVariable((char*)"$$tcpopen_error",msg);	// pass along the error
	Log(STDTRACELOG,msg);
	return FAILRULE_BIT;
#else
	size_t len;
	char* url;
	char directory[MAX_WORD_SIZE];
	char* arg;
	char kind = 0;
	FunctionResult result;
	bool encoded = false;
	if (!stricmp(ARGUMENT(1),(char*)"POST")) kind = 'P';
	else if (!stricmp(ARGUMENT(1),(char*)"GET")) kind = 'G';
	else if (!stricmp(ARGUMENT(1),(char*)"POSTU")) 
	{
		kind = 'P';
		encoded = true;
	}
	else if (!stricmp(ARGUMENT(1),(char*)"GETU")) 
	{
		kind = 'G';
		encoded = true;
	}
	else 
	{
		char* msg = "tcpopen- only POST and GET allowed\r\n";
		SetUserVariable((char*)"$$tcpopen_error",msg);	// pass along the error
		Log(STDTRACELOG,msg);
		return FAILRULE_BIT;
	}

	url = ARGUMENT(2);
	char* dot = strchr(url,'.');
	if (!dot) 
	{
		char* msg = "tcpopen- an url was not given\r\n";
		SetUserVariable((char*)"$$tcpopen_error",msg);	// pass along the error
		Log(STDTRACELOG,msg);
		return FAILRULE_BIT;
	}
	char* slash = strchr(dot,'/');
	if (slash) 
	{
		*slash = 0; // leave url as is
		strcpy(directory,slash+1);
	}
	else *directory = 0;

	// convert \" to " within params abd remove any wrapper
	arg = ARGUMENT(3);
	if (*arg == '"') ++arg;
	len = strlen(arg);
	if (arg[len-1] == '"') arg[len-1] = 0;
	char* fix;
	while ((fix = strchr(arg,'\\'))) memmove(fix,fix+1,strlen(fix)); // remove protective backslash

	char originalArg[MAX_WORD_SIZE];
	strcpy(originalArg,arg);

	// url encoding:
	char* ptr = arg - 1;
	if (!encoded) while (*++ptr)
	{
		if (!IsAlphaUTF8(*ptr) && isAlphabeticDigitData[*ptr] != VALIDDIGIT && *ptr != '-'  && *ptr != '_'  && *ptr != '.' && *ptr != '~' && *ptr != '=' && *ptr != '&')
		{
			if (*ptr == ' ')
			{
				*ptr = '+';
				continue;
			}
			memmove(ptr+3,ptr+1,strlen(ptr)); // reserve 2 extra chars
			ptr[1] = toHex[(*ptr >> 4)  & 0x0f];
			ptr[2] = toHex[(*ptr & 0x0f)];
			*ptr = '%';
			ptr += 2;
		}
	}
	
	// adjust function reference name
	char* function = ARGUMENT(4);
	if (*function == '\'') ++function; // skip over the ' 

	unsigned int port = 0;
	if (kind == 'P' || kind == 'G') port = 80;
	else
	{
		char* colon = strchr(url,':');
		if (colon)
		{
			*colon = 0;
			port = atoi(colon+1);
		}
	}
	clock_t start_time = ElapsedMilliseconds();
	int size = 0;
	char* tcpbuffer = AllocateInverseString(NULL,MAX_BUFFER_SIZE);
	char* startContent = tcpbuffer;
	try 
	{
		if (trace & TRACE_TCP && CheckTopicTrace()) Log(STDTRACELOG,(char*)"RAW TCP: %s/%s port:%d %s %s",url,directory,port,(kind == 'G' ) ? (char*)"GET" : (char*) "POST",originalArg);
		TCPSocket *sock = new TCPSocket(url, (unsigned short)port);
		
		if (kind == 'P')
		{
			if (*directory) sprintf(tcpbuffer,(char*)"POST /%s HTTP/1.0\r\nHost: %s\r\n",directory,url);
			else sprintf(tcpbuffer,(char*)"POST HTTP/1.0\r\nHost: %s\r\n",url);
		}
		else if (kind == 'G') sprintf(tcpbuffer,(char*)"GET /%s?%s HTTP/1.0\r\nHost: %s\r\n",directory,arg,url);
		sock->send(tcpbuffer, strlen(tcpbuffer) );
		if (trace & TRACE_TCP && CheckTopicTrace()) Log(STDTRACELOG,(char*)"\r\n%s",tcpbuffer);
		
		if (kind == 'P')
		{
			strcpy(tcpbuffer,(char*)"Content-Type: application/x-www-form-urlencoded\r\nAccept: text/html, application/xhtml+xml, */*\r\nAccept-Charset: utf-8\r\nUser-Agent: Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.2; WOW64; Trident/6.0)\r\n");
			sock->send(tcpbuffer, strlen(tcpbuffer) );
			if (trace & TRACE_TCP && CheckTopicTrace()) Log(STDTRACELOG,(char*)"%s",tcpbuffer);
			len = strlen(arg);
			sprintf(tcpbuffer,(char*)"Content-Length: %d\r\n\r\n%s\r\n",(unsigned int) len,arg);
		}
		else strcpy(tcpbuffer,(char*)"Content-Type: application/x-www-form-urlencoded\r\nAccept: text/html, application/xhtml+xml, */*\r\nAccept-Charset: utf-8\r\nUser-Agent: Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.2; WOW64; Trident/6.0)\r\n\r\n"); // GET
		sock->send(tcpbuffer, strlen(tcpbuffer) );
		if (trace & TRACE_TCP && CheckTopicTrace()) Log(STDTRACELOG,(char*)"%s",tcpbuffer);
	
		unsigned int bytesReceived = 1;              // Bytes read on each recv()
		unsigned int totalBytesReceived = 0;         // Total bytes read
		char* base = tcpbuffer;
		*base = 0;
		bool hasContent = false;
		int allowedBytes = maxBufferSize - 10;
		while (bytesReceived > 0) 
		{
			// Receive up to the buffer size bytes from the sender
			bytesReceived = sock->recv(base, allowedBytes);
			allowedBytes -= bytesReceived;
			totalBytesReceived += bytesReceived;
			base += bytesReceived;
			if (!hasContent && (kind == 'P' || kind == 'G' ) ) // std POST/GET http formats
			{
				startContent = strstr(tcpbuffer,(char*)"\r\n\r\n"); // body separator
				if (!startContent) continue; // not found yet
				startContent += 4;

				char* lenheader = strstr(tcpbuffer,(char*)"Content-Length: "); // look for earlier size info
				if (lenheader)
				{
					size = atoi(SkipWhitespace(lenheader+16)); // size of body
					hasContent = true;
				}
			}
			if (hasContent && (base-startContent) >= size) break;	// we have enough
		}
		delete(sock);
		*base++ = 0;
		*base++ = 0;
		// chatbot replies this
		if (trace & TRACE_TCP && CheckTopicTrace()) Log(STDTRACELOG,(char*)"tcp received: %d bytes %s",totalBytesReceived,tcpbuffer);
		if (timing & TIME_TCP && CheckTopicTime()) {
			int diff = ElapsedMilliseconds() - start_time;
			if (timing & TIME_ALWAYS || diff > 0) Log(STDTIMETABLOG, (char*)"TCP open time: %d ms for %s %s/%s\r\n", diff, (kind == 'G') ? (char*)"GET" : (char*) "POST", url,directory);
		}
	}
	catch(SocketException e) { 
		char* msg = "tcpopen- failed to connect to server or died in transmission\r\n";
		SetUserVariable((char*)"$$tcpopen_error",msg);	// pass along the error
		Log(STDTRACELOG,msg);
		Log(STDTRACELOG,(char*)"failed to connect to server %s %d\r\n",url,port); 
		ReleaseInverseString(tcpbuffer); 
		return FAILRULE_BIT;
	}

	// process http return for validity
	if (kind == 'P' || kind == 'G')
	{
		if (strnicmp(tcpbuffer,(char*)"HTTP",4)) 
		{
			char* msg = "tcpopen- no HTTP ack code\r\n";
			SetUserVariable((char*)"$$tcpopen_error",msg);	
			Log(STDTRACELOG,msg);
			ReleaseInverseString(tcpbuffer);
			return FAILRULE_BIT;
		}
		char* space = strchr(tcpbuffer,' ');
		space = SkipWhitespace(space);	// go to end of whitespace
		if (trace & TRACE_TCP && CheckTopicTrace()) Log(STDTRACELOG,(char*)"response: %s",space);
		if (*space != '2') 
		{
			char msg[MAX_WORD_SIZE];
			space[5] = 0;
			sprintf(msg,(char*)"tcpopen- HTTP ack code bad %s\r\n",space);
			SetUserVariable((char*)"$$tcpopen_error",msg);	
			Log(STDTRACELOG,msg);
			ReleaseInverseString(tcpbuffer);
			return FAILRULE_BIT;	// failure code of some kind
		}
	}
	
	userRecordSourceBuffer = startContent;
	char* buf1 = AllocateInverseString(NULL,MAX_BUFFER_SIZE);
	buf1[0] = '(';
	buf1[1] = ' ';
	buf1[2] = '"'; // strippable string marker
	buf1[3] = ENDUNIT; // strippable string marker
	result = NOPROBLEM_BIT;
	while (result == NOPROBLEM_BIT)
	{
		if (ReadALine(buf1+4,0) < 0) break;
		if (!buf1[4]) continue;		// no content
		char* actual = TrimSpaces(buf1);
		strcat(actual,(char*)"`\" )"); // trailing quote and ending paren
		if (*function == '^') DoFunction(function,actual,buffer,result); 
		buffer += strlen(buffer);
		if (result == UNDEFINED_FUNCTION) 
		{
			char* msg = "tcpopen- no such function to call";
			SetUserVariable((char*)"$$tcpopen_error",msg);	
			Log(STDTRACELOG,msg);
			result = FAILRULE_BIT;
		}
		else if (result & FAILCODES)
		{
			char* msg = "tcpopen- function call failed";
			SetUserVariable((char*)"$$tcpopen_error",msg);	
			Log(STDTRACELOG,msg);
		}
	}
	userRecordSourceBuffer = NULL;
	ReleaseInverseString(tcpbuffer);
	return result;
#endif
}

static FunctionResult SystemCode(char* buffer)
{
	char word[MAX_WORD_SIZE];
	*word = 0;
	char* stream = ARGUMENT(1);
	while (stream && *stream)
	{
		FunctionResult result;
		char name[MAX_WORD_SIZE];
		stream = ReadShortCommandArg(stream,name,result,OUTPUT_NOQUOTES|OUTPUT_EVALCODE|OUTPUT_NOTREALBUFFER); // name of file
		if (*name)
		{
			strcat(word,name);
			strcat(word,(char*)" ");
		}
	}
	if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACELOG,(char*)"system: %s ",word);
	sprintf(buffer,(char*)"%d",system(word));
	if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACELOG,(char*)" result: %s r\n",buffer);
	return  NOPROBLEM_BIT;
}

//////////////////////////////////////////////////////////
/// FACTS
//////////////////////////////////////////////////////////

static FunctionResult CreateFactCode(char* buffer)
{ 
	currentFact = NULL;
	char* arg = SkipWhitespace(ARGUMENT(1));
	char word[MAX_WORD_SIZE];
	char* at = ReadCompiledWord(arg,word);
	if (!*at) // single arg, eval it to get real one....
	{
		FunctionResult result;
		ReadCommandArg(arg,buffer,result,OUTPUT_NOQUOTES|OUTPUT_EVALCODE|OUTPUT_NOTREALBUFFER); 
		if (result != NOPROBLEM_BIT) return result;
		at = buffer;
		if (*at == '(') at += 2; // skip paren start as would be done by WriteFact
		EatFact(at);
		*buffer = 0;
	}
	else EatFact(arg); // PUTS NOTHING IN THE OUTPUT BUFFER but can be assigned from.
	return (currentFact) ? NOPROBLEM_BIT : FAILRULE_BIT;
}

FACT* DeleteFromList(FACT* oldlist,FACT* oldfact,GetNextFact getnext,SetNextFact setnext)
{ // olditem can be either a meaning or a factoid. we are alloing for the oldlist fact that has olditem as subject
	FACT* start = oldlist;
	if (trace & TRACE_FACT) 
	{
		Log(STDTRACELOG,(char*)" old fact: ");
		TraceFact(oldfact);
	}
	if (trace & TRACE_FACT) 
	{
		Log(STDTRACELOG,(char*)" old list head: ");
		TraceFact(oldlist);
	}
	if (oldlist == oldfact) 
	{
		FACT* G = (*getnext)(oldlist);
		if (trace & TRACE_FACT) 
		{
			Log(STDTRACELOG,(char*)" merely skipping head to ");
			TraceFact(G);
		}
		return G;
	}
	FACT* prior = oldlist;
	while (oldlist)
	{
		if (trace & TRACE_FACT) 
		{
			Log(STDTRACELOG,(char*)"at: ");
			TraceFact(oldlist);
		}
		FACT* H = (*getnext)(oldlist);
		if (oldlist == oldfact)
		{
			if (trace & TRACE_FACT) 
			{
				Log(STDTRACELOG,(char*)"set to next: ");
				TraceFact(H);
			}
			(*setnext)(prior,H); // remove this from list by skip over
			break;
		}
		prior = oldlist;
		oldlist = H;
	}

	return start;
}

FACT* AddToList(FACT* newlist,FACT* oldfact,GetNextFact getnext,SetNextFact setnext)
{
	FACT* start = newlist;
	if (trace & TRACE_FACT) TraceFact(oldfact);
	if (trace & TRACE_FACT) TraceFact(newlist);
	if (start < oldfact) // we will head the lise
	{
		if (trace & TRACE_FACT) TraceFact(oldfact);
		(*setnext)(oldfact,newlist);
		return oldfact;
	}
	FACT* prior = newlist;
	while (newlist)
	{
		if (newlist < oldfact) // add fact into list by insert
		{
			(*setnext)(oldfact, newlist);
			(*setnext)(prior, oldfact);
			break;
		}
		prior = newlist;
		newlist = (*getnext)(newlist);
	}
	return start;
}

static void ShowLinks(char* msg,WORDP D,GetNextFact getnext)
{
	echo = 1;
	Log(STDTRACELOG,(char*)"%s %s chain: \r\n",msg,D->word);
	FACT* F = GetSubjectNondeadHead(D);
	while (F)
	{
		TraceFact(F);
		F = (*getnext)(F);
	}
	Log(STDTRACELOG,(char*)"end chain: \r\n");

}

static void ShowFactLinks(char* msg,FACTOID D,GetNextFact getnext)
{
	echo = 1;
	FACT* G = Index2Fact(D);
	TraceFact(G );
	Log(STDTRACELOG,(char*)"%s ",msg);
	FACT* F = GetSubjectNondeadHead(G );
	while (F)
	{
		TraceFact(F);
		F = (*getnext)(F);
	}
	Log(STDTRACELOG,(char*)"end chain: \r\n");
}

FunctionResult ReviseFactCode(char* buffer)
{ 
	currentFact = NULL;
	char* arg = ARGUMENT(1);
	FACTOID index = atoi(arg);
	FACT* F = Index2Fact(index);
	if (F <= factLocked || F->flags & FACTDEAD) return FAILRULE_BIT; // only user undead facts
	if (F <= factsPreBuild[2])  return FAILRULE_BIT; // may not revise facts built into world
	char* subject = ARGUMENT(2);
	char* verb = ARGUMENT(3);
	char* object = ARGUMENT(4);
	unsigned int flags = F->flags;

	if (stricmp(subject,(char*)"null"))
	{
#ifndef DISCARDJSON
		if (flags & (JSON_ARRAY_FACT | JSON_OBJECT_FACT)) return FAILRULE_BIT;	// may not change subject of json.. linked possibly from above
#endif
		if (flags & FACTSUBJECT)
		{
			unsigned int newsubject = atoi(subject); // find the fact replacing it
			FACT* newfact = Index2Fact(index);
			if (!newfact) return FAILRULE_BIT;
			FACTOID oldsubject = (FACTOID) F->subject;
			FACT* oldfact = Index2Fact(oldsubject);
			if (index != oldsubject) 
			{
				FACT* X = DeleteFromList(GetSubjectHead(oldfact),F,GetSubjectNext,SetSubjectNext); // dont use nondead
				SetSubjectHead(oldfact,X);
				X = AddToList(GetSubjectHead(newfact),F,GetSubjectNext,SetSubjectNext);  // dont use nondead
				SetSubjectHead(newfact,X);
				F->subject = newsubject;
			}
		}
		else // word replacement
		{
			WORDP oldsubject = Meaning2Word(F->subject);
			WORDP newsubject = StoreWord(subject,AS_IS);
			if (oldsubject != newsubject) 
			{
				FACT* X = DeleteFromList(GetSubjectHead(oldsubject),F,GetSubjectNext,SetSubjectNext);  // dont use nondead
				SetSubjectHead(oldsubject,X);
				X = AddToList(GetSubjectHead(newsubject),F,GetSubjectNext,SetSubjectNext);  // dont use nondead
				SetSubjectHead(newsubject,X);
				F->subject = MakeMeaning(newsubject);
			}
		} 
	}
	if (stricmp(verb,(char*)"null"))
	{
#ifndef DISCARDJSON
		if (flags & JSON_ARRAY_FACT ) return FAILRULE_BIT;	// may not change index of json array. breaks sequencing
#endif
		if (flags & FACTVERB)
		{
			unsigned int newverb = atoi(verb); // find the fact replacing it
			FACT* newfact = Index2Fact(index);
			if (!newfact) return FAILRULE_BIT;
			FACTOID oldverb = (FACTOID) F->verb;
			FACT* oldfact = Index2Fact(oldverb);
			if (index != oldverb) 
			{
				FACT* X = DeleteFromList(GetVerbHead(oldfact),F,GetVerbNext,SetVerbNext);  // dont use nondead
				SetVerbHead(oldfact,X);
				X = AddToList(GetVerbHead(newfact),F,GetVerbNext,SetVerbNext);  // dont use nondead
				SetVerbHead(newfact,X);
				F->verb = newverb;
			}
		}
		else // word replacement
		{
			WORDP oldverb = Meaning2Word(F->verb);
			WORDP newverb = StoreWord(verb,AS_IS);
			if (oldverb != newverb) 
			{
				FACT* X = DeleteFromList(GetVerbHead(oldverb),F,GetVerbNext,SetVerbNext);  // dont use nondead
				SetVerbHead(oldverb,X);
				X = AddToList(GetVerbHead(newverb),F,GetVerbNext,SetVerbNext);  // dont use nondead
				SetVerbHead(newverb,X);
				F->verb = MakeMeaning(newverb);
			}
		} 
	}
	if (stricmp(object,(char*)"null"))
	{
		if (flags & FACTOBJECT)
		{
			unsigned int newobject = atoi(object); // find the fact replacing it
			FACT* newfact = Index2Fact(index);
			if (!newfact) return FAILRULE_BIT;
			FACTOID oldobject = (FACTOID) F->object;
			FACT* oldfact = Index2Fact(oldobject);
			if (index != oldobject) 
			{
				FACT* X = DeleteFromList(GetObjectHead(oldfact),F,GetObjectNext,SetObjectNext);  // dont use nondead
				SetObjectHead(oldfact,X);
				X = AddToList(GetObjectHead(newfact),F,GetObjectNext,SetObjectNext);  // dont use nondead
				SetObjectHead(newfact,X);
				F->object = newobject;
			}
		}
		else // word replacement
		{
			WORDP oldObject= Meaning2Word(F->object);
			WORDP newObject = StoreWord(object,AS_IS);
			MEANING value = MakeMeaning(newObject);
#ifndef DISCARDJSON
			if (flags & JSON_FLAGS)
			{
				flags &= -1 ^ ( JSON_PRIMITIVE_VALUE | JSON_STRING_VALUE | JSON_OBJECT_VALUE | JSON_ARRAY_VALUE );
				value = jsonValue(object,flags);
				newObject = Meaning2Word(value);
				F->flags = flags;	// revised for possible new object type
			}
#endif
			if (oldObject != newObject) 
			{
				FACT* X = DeleteFromList(GetObjectHead(oldObject),F,GetObjectNext,SetObjectNext);  // dont use nondead
				SetObjectHead(oldObject,X);
				X = AddToList(GetObjectHead(newObject),F,GetObjectNext,SetObjectNext);  // dont use nondead
				SetObjectHead(newObject,X);
				F->object = value;
			}
		} 
	}

	if (trace & (TRACE_INFER|TRACE_FACT)) TraceFact(F,false);
#ifdef INFORMATION
	As we create facts, older facts (lower index) will be farther down the list. When we erase a fact, we should be at the top of all xref lists.
#endif
	return NOPROBLEM_BIT;
}

static void GenerateConceptList(bool tracing,int list, int set,char* filter,size_t len,int wordindex)
{
	int x = concepts[wordindex];
	x = 0;
	while (x)
	{
		MEANING* data = (MEANING*) Index2String(x);
		MEANING M = *data;
		WORDP D = Meaning2Word(M);
		x = data[1];
	}

	while (list)
	{
		MEANING* at = (MEANING*)Index2String(list);
		list = (unsigned int) at[1];
		MEANING M = *at;
		WORDP X = Meaning2Word(M);
		if (!X) // THIS SHOULD NOT HAPPEN BUT DID
		{
			int xx = 0;
		}
		if (X && !(X->systemFlags & NOCONCEPTLIST) && (!len || !strnicmp(X->word,filter,len))) 
		{
			// found at this word index, get its correct range
			int actualStart, actualEnd;
			if (!GetNextSpot(X,wordindex-1,actualStart,actualEnd,false)) continue;
			if (wordindex != actualStart) continue;	// didnt find it
			int position = (actualStart << 8) | actualEnd;
			char word[MAX_WORD_SIZE];
			sprintf(word,"%d",position);
			FACT* F = CreateFact(M,Mconceptlist,MakeMeaning(StoreWord(word)),FACTTRANSIENT);
			int n = FACTSET_COUNT(set);
			int i;
			for (i = n; i > 0; --i)
			{
				if (factSet[set][i] == F) break; // repeated
			}
			if (!i) 
			{
				if (tracing) TraceFact(F);
				AddFact(set,F);
			}
		}
	}
}

static FunctionResult ConceptListCode(char* buffer)
{
	int set = (impliedSet == ALREADY_HANDLED) ? 0 : impliedSet;
	SET_FACTSET_COUNT(set,0);
	unsigned int how = 0;
	char* arg = ARGUMENT(1);
	char word[MAX_BUFFER_SIZE];
	arg = ReadCompiledWord(arg,word);
	if (!stricmp(word,(char*)"CONCEPT"))
	{
		arg = GetPossibleFunctionArgument(arg,word);
		how = 1;
	}
	else if (!stricmp(word,(char*)"TOPIC"))
	{
		arg = GetPossibleFunctionArgument(arg,word);
		how = 2;
	}
	else if (!stricmp(word,(char*)"BOTH"))
	{
		arg = GetPossibleFunctionArgument(arg,word);
		how = 3;
	}
	else return FAILRULE_BIT;

	char filter[MAX_WORD_SIZE];
	FunctionResult result;
	ReadCommandArg(arg,filter,result,OUTPUT_NOTREALBUFFER|OUTPUT_NOCOMMANUMBER|ASSIGNMENT); // possible filter
	if (*word == '_' || *word == USERVAR_PREFIX || IsDigit(*word) || !*word) {;} // normal expected

	int start = 1;
	int end = 1;
	if (*word == '\'') memmove(word,word+1,strlen(word));
	
	if (*word == '_') 
	{
		start = WildStartPosition(word);  //  wildcard position designator
		end = WildEndPosition(word);  
	}
	else if (*word == USERVAR_PREFIX) start = end = atoi(GetUserVariable(word));  //  user var
	else if (IsDigit(*word)) start = end = atoi(word);
	else if (!*word) end = wordCount; // overall
	else return FAILRULE_BIT;

	if (start < 1 || start > wordCount) return FAILRULE_BIT;

	bool tracing = false;
	if (trace)
	{
		WORDP D = FindWord("^ConceptList");
		if (D->internalBits & MACRO_TRACE || trace & TRACE_FACT) tracing = true;
	}

	size_t len = strlen(filter);
	for (int i = start; i <= end; ++i)
	{
		if (tracing) Log(STDTRACELOG,"@%d %s ",i,wordStarts[i]);
		if (how & 1) GenerateConceptList(tracing,concepts[i], set,filter,len,i);
		if (how & 2) GenerateConceptList(tracing,topics[i], set,filter,len,i);
	}
	if (impliedSet == ALREADY_HANDLED && FACTSET_COUNT(set) == 0) return FAILRULE_BIT;
	impliedSet = ALREADY_HANDLED;
	currentFact = NULL;
	return NOPROBLEM_BIT;
}

static FunctionResult CreateAttributeCode(char* buffer)
{ 
	currentFact = NULL;
	EatFact(ARGUMENT(1),0,true);
	if (currentFact && !(currentFact->flags & FACTATTRIBUTE)) return FAILINPUT_BIT;	// kill the whole line.
	return (currentFact) ? NOPROBLEM_BIT : FAILRULE_BIT; // fails if pre-existing fact cant be killed because used in fact
}

static FunctionResult DeleteCode(char* buffer) //   delete all facts in collection or named fact or named json object/array
{
	char* arg1 = ARGUMENT(1);
	if (!*arg1) return NOPROBLEM_BIT;
	else if (IsDigit(*arg1))
	{
		FACT* F = Index2Fact(atoi(arg1));
		if (F) KillFact(F);
	}
#ifndef DISCARDJSON
	else if (!strnicmp(arg1,"ja-",3) || !strnicmp(arg1,"jo-",3)) return JSONDeleteCode(buffer);
#endif
	else if (*arg1 == '@') // factset
	{
		int store = GetSetID(arg1);
		if (store == ILLEGAL_FACTSET) return FAILRULE_BIT;
		unsigned int count = FACTSET_COUNT(store);
		for (unsigned int i = 1; i <= count; ++i) 
		{
			FACT* F = factSet[store][i];
			KillFact(F);
		}
	}
	return NOPROBLEM_BIT;
}

static FunctionResult FlushFactsCode(char* buffer) // delete all facts after this one (presuming sentence failed)
{
	if (planning) return FAILRULE_BIT; // dont allow this in planner

	unsigned int f = atoi(ARGUMENT(1)); 
	FACT* F = factFree;
	if (f > Fact2Index(F)) return FAILRULE_BIT;
	while (Fact2Index(F) > f)
	{
		F->flags |= FACTDEAD;	// kill it. dont have to do it recursive (KillFact) because everything that might be using this is already killed by this loop
		--F;
	}
	return NOPROBLEM_BIT;
}

static FunctionResult FieldCode(char* buffer) 
{	
	FACT* F;
	char* word = ARGUMENT(1);
	char word1[MAX_WORD_SIZE];
	if (*word == '@') return FAILRULE_BIT;
	F = FactTextIndex2Fact(word); 
	if (!F || F > factFree) return FAILRULE_BIT;

	WORDP xxs = Meaning2Word(F->subject); // for debugging
	WORDP xxv = Meaning2Word(F->verb);  // for debugging
	WORDP xxo = Meaning2Word(F->object);  // for debugging
	char* arg2 = ARGUMENT(2);
	if (*arg2 == 's' || *arg2 == 'S') 
	{
		if (F->flags & FACTSUBJECT) 
		{
			if (*arg2 == 's') sprintf(buffer,(char*)"%d",F->subject);
			else strcpy(buffer,WriteFact(Index2Fact(F->subject),false,word1,false,false));
		}
		else strcpy(buffer,WriteMeaning(F->subject));
	}
	else if (*arg2 == 'v' || *arg2 == 'V') 
	{
		if (F->flags & FACTVERB) 
		{
			if (*arg2 == 'v') sprintf(buffer,(char*)"%d",F->verb);
			else strcpy(buffer,WriteFact(Index2Fact(F->verb),false,word1,false,false));
		}
		else strcpy(buffer,WriteMeaning(F->verb));
	}
	else if (*arg2 == 'o' || *arg2 == 'O') 
	{
		if (F->flags & FACTOBJECT) 
		{
			if (*arg2 == 'o') sprintf(buffer,(char*)"%d",F->object);
			else strcpy(buffer,WriteFact(Index2Fact(F->object),false,word1,false,false));
		}
		else strcpy(buffer,WriteMeaning(F->object));
	}
	else if (*arg2 == 'f' || *arg2 == 'F') 
	{
		sprintf(buffer,(char*)"%d",F->flags);
	}
	else if (*arg2 == 'a' || *arg2 == 'A') // all
	{
		char word[MAX_WORD_SIZE];
		if (impliedWild == ALREADY_HANDLED)  return FAILRULE_BIT;	// must spread them
		SetWildCardIndexStart(impliedWild); //   start of wildcards to spawn
		if (F->flags & FACTSUBJECT)  sprintf(word,(char*)"%d",F->subject);
		else  strcpy(word,Meaning2Word(F->subject)->word);
		if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" _%d = %s ",impliedWild,word);
		SetWildCard(word,word,0,0);
		if (F->flags & FACTVERB)  sprintf(word,(char*)"%d",F->verb);
		else  strcpy(word,Meaning2Word(F->verb)->word);
		if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" _%d = %s ",impliedWild+1,word);
		SetWildCard(word,word,0,0);
		if (F->flags & FACTOBJECT)  sprintf(word,(char*)"%d",F->object);
		else  strcpy(word,Meaning2Word(F->object)->word);
		if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" _%d = %s ",impliedWild+2,word);
		SetWildCard(word,word,0,0);
		impliedWild = ALREADY_HANDLED;	//   we did the assignment
	}
	else if (*arg2 == 'r' || *arg2 == 'R') // all raw
	{
		char word[MAX_WORD_SIZE];
		if (impliedWild == ALREADY_HANDLED)  return FAILRULE_BIT;	// must spread them
		SetWildCardIndexStart(impliedWild); //   start of wildcards to spawn
		if (F->flags & FACTSUBJECT)  sprintf(word,(char*)"%d",F->subject);
		else  strcpy(word,WriteMeaning(F->subject));
		if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" _%d = %s ",impliedWild,word);
		SetWildCard(word,word,0,0);
		if (F->flags & FACTVERB)  sprintf(word,(char*)"%d",F->verb);
		else  strcpy(word,WriteMeaning(F->verb));
		if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" _%d = %s ",impliedWild+1,word);
		SetWildCard(word,word,0,0);
		if (F->flags & FACTOBJECT)  sprintf(word,(char*)"%d",F->object);
		else  strcpy(word,WriteMeaning(F->object));
		if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" _%d = %s ",impliedWild+2,word);
		SetWildCard(word,word,0,0);
		impliedWild = ALREADY_HANDLED;	//   we did the assignment
	}
	else return FAILRULE_BIT;
	return NOPROBLEM_BIT;
}

static FunctionResult FindCode(char* buffer) // given a set, find the ordered position of the 2nd argument in it 
{   
	char word[MAX_WORD_SIZE];
	char* arg1 = ARGUMENT(1); // set
	char* arg2 = ARGUMENT(2); // item
	strcpy(word,JoinWords(BurstWord(arg2),false)); //  the value to find
	WORDP D = FindWord(arg1);
	if (*arg1 == '@')
	{
		FACT* F = Index2Fact(atoi(arg2));
		int set = GetSetID(arg1);
		if (set == ILLEGAL_FACTSET) return FAILRULE_BIT;
		unsigned int count =  FACTSET_COUNT(set);
		for (unsigned int i = 1; i <= count; ++i)
		{
			if (F == factSet[set][i]) 
			{
				sprintf(buffer,(char*)"%d",i);
				return NOPROBLEM_BIT;
			}
		}
		return FAILRULE_BIT;
	}
	else if (D && *arg1 == '~')
	{
		int n = -1;
		FACT* F = GetObjectNondeadHead(D);  
		while (F ) // walks set MOST recent (right to left)
		{
 			if (F->verb == Mmember) 
			{
				++n;
				WORDP item = Meaning2Word(F->subject);
				if (!stricmp(item->word,word))
				{
																										sprintf(buffer,(char*)"%d",n);
					return NOPROBLEM_BIT;
				}
			}
			F = GetObjectNondeadNext(F);
		}
	}

	return FAILRULE_BIT; 
}

static FunctionResult FindFactCode(char* buffer) // given a Simple fact
{   
	char* arg1 = ARGUMENT(1);
	char* arg2 = ARGUMENT(2);
	char* arg3 = ARGUMENT(3);
	FACT* F = FindFact(ReadMeaning(arg1,false),ReadMeaning(arg2,false),ReadMeaning(arg3,false),0); 
	if (!F) return FAILRULE_BIT;
	sprintf(buffer,(char*)"%d",Fact2Index(F));
	return NOPROBLEM_BIT;
}

static FACT* FindF(MEANING subject,WORDP verb,uint64 marker)
{ 
	FACT* F = GetSubjectNondeadHead(subject);
    while (F)
    {
		WORDP v = Meaning2Word(F->verb);
        if (v == verb) 
		{
			WORDP obj = Meaning2Word(F->object);
			if (marker != MARKED_WORD) // using a fact marking for find
			{
				if (F->flags & marker) return F;
			}
			else if (obj->systemFlags & marker) return F; // can use marked word flag as well
			FACT* G = FindF(F->object,verb,marker);
			if (G) return G;
		}
        F = GetSubjectNondeadNext(F);
    }

	return 0;
}

static FunctionResult FindMarkedFactCode(char* buffer)
{ 
	WORDP subject = FindWord(ARGUMENT(1));
	if (!subject) return FAILRULE_BIT;
	WORDP verb = FindWord(ARGUMENT(2));
	if (!verb) return FAILRULE_BIT;
	char* mark = ARGUMENT(3);
	int64 marker;
	if (IsDigit(*mark)) ReadInt64(mark,marker);
	else marker = FindValueByName(mark); // a fact marker like MARKED_FACT  or word systemflag marker like MARKED_WORD
	if (!marker) return FAILRULE_BIT;

	FACT* F = FindF(MakeMeaning(subject),verb,marker);
	if (trace & TRACE_INFER && CheckTopicTrace()) 
	{
		if (F) 
		{
			Log(STDTRACELOG,(char*)"FindMarkedFact found ");
			TraceFact(F);
		}
		else Log(STDTRACELOG,(char*)"FindMarkedFact not found ");
	}
	if (!F) return FAILRULE_BIT;

	sprintf(buffer,(char*)"%d",Fact2Index(F)); // return index
	return NOPROBLEM_BIT;
}

static FunctionResult FLRCodeF(char* buffer)
{
	return FLR(buffer,(char*)"f");
}

static FunctionResult IntersectFactsCode(char* buffer) 
{      
	char* word = ARGUMENT(1);
	char from[MAX_WORD_SIZE];
	char to[MAX_WORD_SIZE];
	FunctionResult result;
	word = ReadShortCommandArg(word,from,result,OUTPUT_KEEPQUERYSET|OUTPUT_NOTREALBUFFER);
	word = ReadShortCommandArg(word,to,result,OUTPUT_KEEPQUERYSET|OUTPUT_NOTREALBUFFER);
	int store = (impliedSet == ALREADY_HANDLED) ? 0 : impliedSet;
    SET_FACTSET_COUNT(store,0);

    WORDP D;
    FACT* F;
    unsigned int usedMark = NextInferMark();
    unsigned int i;
	char toKind = 's';
	int testSet = GetSetID(from); 
	if (testSet == ILLEGAL_FACTSET) return FAILRULE_BIT;

	if (*to != '@') // mark word
	{
		D = FindWord(to);
		if (D) D->inferMark = usedMark;
	}
	else //  mark 2nd set word or pieces
	{
		int toset = GetSetID(to);
		if (toset == ILLEGAL_FACTSET) return FAILRULE_BIT;
		toKind = GetLowercaseData(*GetSetType(to)); // s v o null
		unsigned int limit = FACTSET_COUNT(toset);
		for (i = 1; i <= limit; ++i)
		{
			WORDP D;
			F = factSet[toset][i];
			if (!F) continue;
			if (trace & TRACE_INFER && CheckTopicTrace())   TraceFact(F);
			if (toKind == 's') Meaning2Word(F->subject)->inferMark = usedMark;
 			else if (toKind == 'v') Meaning2Word(F->verb)->inferMark = usedMark;
 			else if (toKind == 'o') Meaning2Word(F->object)->inferMark = usedMark;
			else // mark all pieces
			{
				D = Meaning2Word(F->subject);
				D->inferMark = usedMark;
				D = Meaning2Word(F->verb);
				D->inferMark = usedMark;
				D = Meaning2Word(F->object);
				D->inferMark = usedMark;
				F->flags |= MARKED_FACT;
			}
		}
	}

    // look for all matches of second set
	char fromKind = GetLowercaseData(*GetSetType(from)); // s v o null
    unsigned int limit = FACTSET_COUNT(testSet);
  	if (trace & TRACE_INFER) Log(STDTRACELOG,(char*)" // ");
	for (i = 1; i <= limit; ++i)
    {
        F = factSet[testSet][i];
		if (!F) continue;
 		if (trace & TRACE_INFER && CheckTopicTrace())   TraceFact(F);
 		if (fromKind == 's') 
		{
			if (Meaning2Word(F->subject)->inferMark == usedMark) AddFact(store,F);
		}
 		else if (fromKind == 'v')
		{
			if (Meaning2Word(F->verb)->inferMark == usedMark) AddFact(store,F);
		}
		else if (fromKind == 'o')
		{
			if (Meaning2Word(F->object)->inferMark == usedMark) AddFact(store,F);
		}
		else 
		{
			// entire fact found
			if (toKind != 's' && toKind != 'v' && toKind != 'o' &&  F->flags & MARKED_FACT) AddFact(store,F);
			// some piece found
			else if (Meaning2Word(F->subject)->inferMark == usedMark || Meaning2Word(F->verb)->inferMark == usedMark || Meaning2Word(F->object)->inferMark == usedMark) AddFact(store,F);
		}
    }
 	unsigned int count = FACTSET_COUNT(store);
	if (trace & TRACE_INFER && CheckTopicTrace())
	{
		Log(STDTRACELOG,(char*)"Found %d in IntersectFact\r\n",count);
		for (i = 1; i <= count; ++i) TraceFact(factSet[store][i]);
	}
	if (impliedSet == ALREADY_HANDLED && !count) return FAILRULE_BIT;
	impliedSet = ALREADY_HANDLED;
    return NOPROBLEM_BIT;
}

static FunctionResult UniqueFactsCode(char* buffer) 
{      
	char* word = ARGUMENT(1);
	char from[MAX_WORD_SIZE];
	char to[MAX_WORD_SIZE];
	FunctionResult result;
	word = ReadShortCommandArg(word,from,result,OUTPUT_KEEPQUERYSET|OUTPUT_NOTREALBUFFER);
	word = ReadShortCommandArg(word,to,result,OUTPUT_KEEPQUERYSET|OUTPUT_NOTREALBUFFER);
	int store = (impliedSet == ALREADY_HANDLED) ? 0 : impliedSet;
    SET_FACTSET_COUNT(store,0);

    WORDP D;
    FACT* F;
    unsigned int usedMark = NextInferMark();
    unsigned int i;
	char toKind = 's';
	int where = GetSetID(from); 
	if (where == ILLEGAL_FACTSET) return FAILRULE_BIT;

	if (*to != '@') // mark word
	{
		D = FindWord(to);
		if (D) D->inferMark = usedMark;
	}
	else //  mark set
	{
		int toset = GetSetID(to);
		if (toset == ILLEGAL_FACTSET) return FAILRULE_BIT;
		toKind = GetLowercaseData(*GetSetType(to)); // s v o null
		unsigned int limit = FACTSET_COUNT(toset);
		for (i = 1; i <= limit; ++i)
		{
			WORDP D;
			F = factSet[toset][i];
			if (!F) continue;
			if (trace & TRACE_INFER && CheckTopicTrace())   TraceFact(F);
			if (toKind == 's') Meaning2Word(F->subject)->inferMark = usedMark;
 			else if (toKind == 'v') Meaning2Word(F->verb)->inferMark = usedMark;
 			else if (toKind == 'o') Meaning2Word(F->object)->inferMark = usedMark;
			else // mark all pieces
			{
				D = Meaning2Word(F->subject);
				D->inferMark = usedMark;
				D = Meaning2Word(F->verb);
				D->inferMark = usedMark;
				D = Meaning2Word(F->object);
				D->inferMark = usedMark;
				F->flags |= MARKED_FACT;
			}
		}
	}

    // look for non matches
	char fromKind = GetLowercaseData(*GetSetType(from)); // s v o null
    unsigned int limit = FACTSET_COUNT(where);
  	if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)" // ");
	for (i = 1; i <= limit; ++i)
    {
        F = factSet[where][i];
		if (!F) continue;
 		if (trace & TRACE_INFER && CheckTopicTrace())   TraceFact(F);
 		if (fromKind == 's' && Meaning2Word(F->subject)->inferMark != usedMark) AddFact(store,F);
 		else if (fromKind == 'v' && Meaning2Word(F->verb)->inferMark != usedMark) AddFact(store,F);
		else if (fromKind == 'o' && Meaning2Word(F->object)->inferMark != usedMark) AddFact(store,F);
		else 
		{
			// entire fact not found
			if (toKind != 's' && toKind != 'v' && toKind != 'o' &&  !(F->flags & MARKED_FACT)) AddFact(store,F);
			// some piece found
			else if (Meaning2Word(F->subject)->inferMark != usedMark && Meaning2Word(F->verb)->inferMark != usedMark && Meaning2Word(F->object)->inferMark != usedMark) AddFact(store,F);
		}
    }
 	unsigned int count = FACTSET_COUNT(store);
	if (trace & TRACE_INFER && CheckTopicTrace())
	{
		Log(STDTRACELOG,(char*)"Found %d in UniqueFacts\r\n",count);
		for (i = 1; i <= count; ++i) TraceFact(factSet[store][i]);
	}
	if (impliedSet == ALREADY_HANDLED && !count) return FAILRULE_BIT;
	impliedSet = ALREADY_HANDLED;
    return NOPROBLEM_BIT;
}

static FunctionResult IteratorCode(char* buffer)
{// ? is std iterator ?? is recursive
	char* arg1 = ARGUMENT(1);
	char* arg2 = ARGUMENT(2);
	char* arg3 = ARGUMENT(3);
	WORDP verb = FindWord(arg2);
	if (!verb) return FAILRULE_BIT;
	MEANING v = MakeMeaning(verb);
	FACT* F;
	WORDP D;
	FACT* holdIterator = NULL;
	if (currentIterator) // this is a return to iteration- either a normal fact or a special fact containing both hieararcy and normal fact data
	{
		F = Index2Fact(currentIterator);
		if (F->flags & ITERATOR_FACT) 
		{
			holdIterator = F;
			F = Index2Fact(F->object);
		}
		F = (*arg1 == '?') ?  GetObjectNondeadNext(F) : GetSubjectNondeadNext(F);
	}
	else // this is a start of iteration
	{
		if (*arg1 == '?') 
		{
			D = FindWord(arg3); // simple word, not meaning
			F = (D) ? GetObjectNondeadHead(D) : NULL;
		}
		else
		{
			D = FindWord(arg1); // simple word, not meaning
			F = (D) ? GetSubjectNondeadHead(D) : NULL;
		}
	}
	retry: // level return if any
	while (F)
	{
		if (F->verb == v)
		{
			if (arg1[1] == '?' || arg3[1] == '?') // recursive on concepts?
			{
				MEANING field = (*arg1 == '?') ? F->subject : F->object;
				WORDP E = Meaning2Word(field);
				if (*E->word == '~') // going to nest within
				{
					FACT* G = SpecialFact(holdIterator ? (holdIterator->verb) : 0,Fact2Index(F),ITERATOR_FACT); // remember where we were
					F = (*arg1 == '?') ? GetObjectNondeadHead(E) : GetSubjectNondeadHead(E);
					if (!holdIterator) holdIterator = SpecialFact(Fact2Index(G),Fact2Index(F),ITERATOR_FACT); // we return this as holding current level and prior level tree
					else holdIterator->verb = Fact2Index(G);
					continue;	// resume hunting at lower level
				}
			}
			break; // found one
		}
		F = (*arg1 == '?') ?  GetObjectNondeadNext(F) : GetSubjectNondeadNext(F);
	}
	if (!F) // ran dry
	{
		if (holdIterator) // back out of recursive on concepts?
		{
			F = Index2Fact(holdIterator->verb);  // this is a special fact also
			if (!F) return FAILRULE_BIT;		// levels ran dry
			holdIterator->verb = F->verb;		// hold now points higher
			F = Index2Fact(F->object);			// where we were at the higher level
			F = (*arg1 == '?') ?  GetObjectNondeadNext(F) : GetSubjectNondeadNext(F);
			goto retry;
		}
		return FAILRULE_BIT;
	}
	MEANING M = (*arg1 == '?') ? F->subject : F->object;
	sprintf(buffer,(char*)"%s",WriteMeaning(M));
	if (!withinLoop && planning && !backtrackable) backtrackable = true;

	if (holdIterator)
	{
		holdIterator->object = Fact2Index(F); // alter we are pair of hierarchy return and current
		F = holdIterator;
	}
	currentIterator = Fact2Index(F); 
	return NOPROBLEM_BIT;
}

static FunctionResult MakeRealCode(char* buffer)
{
	FACT* at = factFree+1;
	while (--at > factLocked) // user facts
	{
		if (at->flags & FACTTRANSIENT) at->flags ^= FACTTRANSIENT;
	}
	
	return NOPROBLEM_BIT;
}

static FunctionResult FLRCodeL(char* buffer)
{
	return FLR(buffer,(char*)"l");
}
extern int backtrackIndex;
static FunctionResult QueryCode(char* buffer)
{ //   kind, s, v, o, count,  from, to, propogate, mark 
	int count = 0;
	char* ptr = ARGUMENT(1);
	int argcount = 0;
	while (ptr && *ptr) // break apart arguments, but leave any quoted arg UNEVALED.
	{
		argcount++;
		char word[MAX_WORD_SIZE];
		ptr = ReadCompiledWord(ptr,word);
		if (*word != '\'' || word[1] == '_') // quoted var or such but not quoted matchvar
		{
			FunctionResult result = NOPROBLEM_BIT;
			if (*word == '@') ARGUMENT(argcount) = AllocateInverseString(word); // no eval of such
			else 
			{
				char* buf = AllocateBuffer(); // cannot use AllocateInverseString here
				ReadShortCommandArg(word,buf,result);
				ARGUMENT(argcount) = AllocateInverseString(buf);
				FreeBuffer();
			}
			if (result != NOPROBLEM_BIT) return result;
		}
		else ARGUMENT(argcount) = AllocateInverseString(word);
	}

	for (int i = argcount+1; i <= 9; ++i) ARGUMENT(i) = (char*)""; // default rest of args to ""
	if (IsDigit(ARGUMENT(5)[0])) ReadInt(ARGUMENT(5),count); // defaults to ? if not given
	if (count == 0) count = (unsigned int) -1; // infinite

	if (argcount < 9) while (++argcount <= 9) ARGUMENT(argcount) = (char*)"?"; //   default rest of calling Arguments
	char set[50];
	char* arg1 = ARGUMENT(1);
	char* subject = ARGUMENT(2);
	char* verb = ARGUMENT(3);
	char* object = ARGUMENT(4);
	char* from = ARGUMENT(6);
	char* to = ARGUMENT(7);
	char* arg8 = ARGUMENT(8);
	char* arg9 = ARGUMENT(9);

	if (impliedSet != ALREADY_HANDLED) 
	{
		sprintf(set,(char*)"@%d",impliedSet); 
		to = set;
	}

	char splitoff[MAX_WORD_SIZE];
	*splitoff = 0;
	char* at = to;
	if (*at == '@') // get any special assignment data
	{
		while (IsDigit(*++at));
		strcpy(splitoff,at);
	}
	clock_t start_time = ElapsedMilliseconds();

	count = Query(arg1, subject, verb, object, count, from, to,arg8, arg9);

	if (timing & TIME_QUERY && CheckTopicTime()) {
		int diff = ElapsedMilliseconds() - start_time;
		if (timing & TIME_ALWAYS || diff > 0) Log(STDTIMETABLOG, (char*)"Query(%s %s %s %s) time: %d ms\r\n", arg1, subject, verb, object, diff);
	}

	// result was a count. now convert to a fail code
	FunctionResult result;
	if (impliedSet != ALREADY_HANDLED) result = NOPROBLEM_BIT;
	else if (*splitoff) result = NOPROBLEM_BIT;
	else result = (count != 0) ? NOPROBLEM_BIT : FAILRULE_BIT; 
	impliedSet = ALREADY_HANDLED;

	// split off?
	if (*splitoff && count != 0)
	{
		int store = GetSetID(to);
		if (store == ILLEGAL_FACTSET) return FAILRULE_BIT;

		FACT* F = factSet[store][1];
		if (*splitoff == 'o' || *splitoff == 'O') sprintf(buffer,"%s",Meaning2Word(F->object)->word);
		else if (*splitoff == 'v' || *splitoff == 'V') sprintf(buffer,"%s",Meaning2Word(F->verb)->word);
		else if (*splitoff == 's' || *splitoff == 'S') sprintf(buffer,"%s",Meaning2Word(F->subject)->word);
		else if (*splitoff == 'f' || *splitoff == 'F') sprintf(buffer,"%d",Fact2Index(F));
		else if (*splitoff == 'a' || *splitoff == 'A') return FAILRULE_BIT;
	}
	return result;
}

static FunctionResult SortCode(char* buffer) // sorts low to high  sort(@factset @chainedfactset
{
	char* arg = ARGUMENT(1); // stream
	char word[MAX_WORD_SIZE];
	char word1[MAX_WORD_SIZE];
	int alpha = 0;
	char* start = arg;
	arg = SkipWhitespace(ReadCompiledWord(arg,word));
	if (!stricmp(word,(char*)"alpha")) // optional alpha
	{
		alpha = 1;
		start = arg;
		arg = ReadCompiledWord(arg,word);
	}
	else if (!stricmp(word,(char*)"age")) // optional age
	{
		alpha = 2;
		start = arg;
		arg = ReadCompiledWord(arg,word);
	}
	if (*word != '@') return FAILRULE_BIT;	
    int n = GetSetID(word);
	if (n == ILLEGAL_FACTSET) return FAILRULE_BIT;
	unsigned int count = FACTSET_COUNT(n);
	int startSet = n;
	if (count > 0x0000ffff) return FAILRULE_BIT;	// too many facts to count
	bool multiple = false;
	// if chained sets, number the facts of the original
	if (*arg == '@') // remaining sets
	{
		// verify they all have the same count
		char* at = arg;
		while (*at == '@') // sort the others to correspond
		{
			at = SkipWhitespace(ReadCompiledWord(at,word1));
			int a = GetSetID(word1);
			if (a == ILLEGAL_FACTSET) return FAILRULE_BIT;
			if (FACTSET_COUNT(a) != count) return FAILRULE_BIT;
		}

		multiple = true;
	}
	SET_FACTSET_COUNT(20,count);	
	for (unsigned int i = 1; i <= count; ++i) // mark original set we sort on
	{
		FACT* F = factSet[n][i];
		if (F) 
		{
			factFlags[i] = F->flags; // protect flags
			F->flags = i; // index of fact originally
			factSet[20][i] = F;
		}
		else return FAILRULE_BIT; // null value fact
	}

	SortFacts(word,alpha,20); // sort copy of the original
	bool once = false;
	while (*start == '@') // sort the others to correspond
	{
		start = SkipWhitespace(ReadCompiledWord(start,word));
		int a = GetSetID(word);
		memcpy(&factSet[21],&factSet[a],sizeof(FACT*) *  (count + 1)); // duplicate it
		for (unsigned int i = 1; i <= count; ++i)
		{
			// if (!factSet[n][i]) continue;
			unsigned int index = factSet[20][i]->flags;	// the new index at this position
			factSet[a][i] = factSet[21][index];
			if (!once) factIndex[i] = index;
		}
		once = true;
	}
	for (unsigned int i = 1; i <= count; ++i)
	{
		unsigned int index = factIndex[i];	// the new index at this position
		FACT* F = factSet[startSet][i];
		F->flags = factFlags[factIndex[i]];
	}

	return NOPROBLEM_BIT;
}

static FunctionResult UnduplicateCode(char* buffer)
{
	if (impliedSet == ALREADY_HANDLED) return FAILRULE_BIT;

	int from = GetSetID(ARGUMENT(1));
	if (from == ILLEGAL_FACTSET) return FAILRULE_BIT;
	if (impliedSet == from) return FAILRULE_BIT; // cant do in-place
	unsigned int count = FACTSET_COUNT(from);
	SET_FACTSET_COUNT(impliedSet,0);
	char type = 0;
	char* mod = GetSetType(ARGUMENT(1));
	if ((*mod == 's' || *mod == 'S' )) type = 's';
	else if ((*mod == 'v' || *mod == 'V' )) type = 'v';
	else if ((*mod == 'o' || *mod == 'O' )) type = 'o';

	// copy unmarked facts to to
	unsigned int i;
	for (i = 1; i <= count; ++i) 
	{
		FACT* F = factSet[from][i];
		if (!F) continue;
		if (type == 's' && Meaning2Word(F->subject)->internalBits & WORDNET_ID) continue;
		if (type == 'v' && Meaning2Word(F->verb)->internalBits & WORDNET_ID) continue;
		if (type == 'o' && Meaning2Word(F->object)->internalBits & WORDNET_ID) continue;
		if (!(F->flags & MARKED_FACT))
		{
			AddFact(impliedSet,F);
		}
		F->flags |= MARKED_FACT;
		WORDP D = Meaning2Word(F->subject);
		D->internalBits  |= WORDNET_ID;
		D = Meaning2Word(F->verb);
		D->internalBits |= WORDNET_ID;
		D = Meaning2Word(F->object);
		D->internalBits |= WORDNET_ID;
	}

	// erase marks
	count = FACTSET_COUNT(impliedSet);
	for (i = 1; i <= count; ++i) 
	{
		FACT* F = factSet[impliedSet][i];
		F->flags ^= MARKED_FACT;
		WORDP D = Meaning2Word(F->subject);
		D->internalBits &= -1 ^ WORDNET_ID;
		D = Meaning2Word(F->verb);
		D->internalBits &= -1 ^ WORDNET_ID;
		D = Meaning2Word(F->object);
		D->internalBits &= -1 ^ WORDNET_ID;
	}

	if (trace & TRACE_INFER && CheckTopicTrace()) Log(STDTRACELOG,(char*)"Unduplicated @%d[%d]\r\n",impliedSet,count);
	impliedSet = ALREADY_HANDLED;
	return NOPROBLEM_BIT;
}

static FunctionResult UnpackFactRefCode(char* buffer)
{
	if (impliedSet == ALREADY_HANDLED) return FAILRULE_BIT;
	char* arg1 = ARGUMENT(1);
	int from = GetSetID(arg1);
	if (from == ILLEGAL_FACTSET) return FAILRULE_BIT;
	int count = FACTSET_COUNT(from);
	char* type = GetSetType(arg1);
	SET_FACTSET_COUNT(impliedSet,0);
	FACT* G;
	for (int i = 1; i <= count; ++i)
	{
		FACT* F = factSet[from][i];
		if (!F) continue;
		if (F->flags & FACTSUBJECT && *type != 'v' && *type != 'o') 
		{
			G = Index2Fact(F->subject);
			if (trace & TRACE_INFER && CheckTopicTrace()) TraceFact(G);
			AddFact(impliedSet,G);
		}
		if (F->flags & FACTVERB && *type != 's' && *type != 'o') 
		{
			G = Index2Fact(F->verb);
			if (trace & TRACE_INFER && CheckTopicTrace()) TraceFact(G);
			AddFact(impliedSet,G);
		}
		if (F->flags & FACTOBJECT && *type != 's' && *type != 'v') 
		{
			 G = Index2Fact(F->object);
			if (trace & TRACE_INFER && CheckTopicTrace()) TraceFact(G);
			AddFact(impliedSet,G);
		}
	}
	impliedSet = ALREADY_HANDLED;
	currentFact = NULL;
	return NOPROBLEM_BIT;
}

static FunctionResult WriteFactCode(char* buffer)
{
	char* arg1 = ARGUMENT(1);
	unsigned int index = atoi(arg1);
	FACT* F = Index2Fact(index);
	if (!F) return FAILRULE_BIT;
	WriteFact(F,false,buffer,false,false);
	return NOPROBLEM_BIT;
}

static FunctionResult SerializeCode(char* buffer)
{
	int store = GetSetID(ARGUMENT(1));
	if (store == ILLEGAL_FACTSET) return FAILRULE_BIT;
	int n = FACTSET_COUNT(store);
	for (int i = 1; i <= n; ++i)
	{
		FACT* F = factSet[store][i];
		WriteFact(F,false,buffer);
		buffer += strlen(buffer);
		*buffer++ = ' ';
		*buffer = 0;
		unsigned int size = (buffer - currentOutputBase);
		if (size >= (currentOutputLimit - 400)) return FAILRULE_BIT; // too close
	}
	return NOPROBLEM_BIT;
}

static FunctionResult DeserializeCode(char* buffer)
{
	if (impliedSet == ALREADY_HANDLED) return FAILRULE_BIT;
	int store = impliedSet;
	if (store == ILLEGAL_FACTSET) return FAILRULE_BIT;
	SET_FACTSET_COUNT(store,0);
	char* arg = ARGUMENT(1);
	int n = 0;
	while (arg && *arg)
	{
		arg = SkipWhitespace(arg);
		if (*arg++ != '(') return FAILRULE_BIT;
		arg = EatFact(arg);
		factSet[store][++n] = currentFact;
	}
	SET_FACTSET_COUNT(store,n);
	impliedSet = ALREADY_HANDLED;
	return NOPROBLEM_BIT;
}

#ifdef PRIVATE_CODE
	#include "../privatecode/privatesrc.cpp"
#endif

SystemFunctionInfo systemFunctionSet[] =
{
	{ (char*)"",0,0,0,(char*)""},

	{ (char*)"\r\n---- Topic",0,0,0,(char*)""},
	{ (char*)"^addtopic",AddTopicCode,1,SAMELINE,(char*)"note a topic as interesting"}, //O
	{ (char*)"^available",AvailableCode,VARIABLE_ARG_COUNT,0,(char*)"is rule still available or has it been disabled"}, 
	{ (char*)"^cleartopics",ClearTopicsCode,0,SAMELINE,(char*)"remove all interesting topics in queue"},
	{ (char*)"^counttopic",CountTopicCode,2,SAMELINE,(char*)"provide topic and count requested: GAMBIT, AVAILABLE, RULE, USED"}, 
	{ (char*)"^gambit",GambitCode,VARIABLE_ARG_COUNT,0,(char*)"execute topic in gambit mode, naming ~ ~topicname PENDING or keyword or nothing"}, 
	{ (char*)"^getverify",GetVerifyCode,1,0,(char*)""}, 
	{ (char*)"^getrule",GetRuleCode,VARIABLE_ARG_COUNT,0,(char*)"get the requested data (TAG,TYPE,LABEL,PATTERN,OUTPUT,TOPIC,USABLE) for rule tag or label"},
	{ (char*)"^topicflags",TopicFlagsCode,1,SAMELINE,(char*)"Get topic control bits"}, 
	{ (char*)"^lastused",LastUsedCode,2,SAMELINE,(char*)"Get input count of last topic access - GAMBIT, RESPONDER, REJOINDER, ANY"}, 
	{ (char*)"^hasgambit",HasGambitCode,VARIABLE_ARG_COUNT,0,(char*)"name of topic to test for an unexpired gambit, LAST/ANY/"}, 
	{ (char*)"^keep",KeepCode,0,SAMELINE,(char*)"do not erase rule after use"}, 
	{ (char*)"^poptopic",PopTopicCode,VARIABLE_ARG_COUNT,0,(char*)"remove current or named topic from interesting set"}, 
	{ (char*)"^refine",RefineCode,VARIABLE_ARG_COUNT,0,(char*)"execute continuations until one matches"}, 
	{ (char*)"^rejoinder",RejoinderCode,VARIABLE_ARG_COUNT,0,(char*)"try to match a pending rejoinder - not legal in postprocessing"}, 
	{ (char*)"^respond",RespondCode,VARIABLE_ARG_COUNT,0,(char*)"execute a topic's responders"}, 
	{ (char*)"^reuse",ReuseCode,VARIABLE_ARG_COUNT,0,(char*)"jump to a rule label or tag and execute output section"}, 
	{ (char*)"^sequence",SequenceCode,VARIABLE_ARG_COUNT,0,(char*)"execute continuations until one fails in output"}, 
	{ (char*)"^setrejoinder",SetRejoinderCode,VARIABLE_ARG_COUNT,0,(char*)"Set rejoinder {INPUT OUTPUT} mark to this tag"}, 
// These can transfer control to another topic: gambit, responder, rejoinder, reuse, refine

	{ (char*)"\r\n---- Topic Lists",0,0,0,(char*)""},
	{ (char*)"^gambittopics",GetTopicsWithGambitsCode,0,0,(char*)"get all topics that have usable gambits that are not current topic"}, 
	{ (char*)"^keywordtopics",KeywordTopicsCode,VARIABLE_ARG_COUNT,0,(char*)"get facts of topics that cover keywords mentioned in input"}, 
	{ (char*)"^pendingtopics",PendingTopicsCode,1,0,(char*)"return list of currently pending topics as facts in 1st arg"}, 
	{ (char*)"^querytopics",QueryTopicsCode,1,0,(char*)"get topics of which 1st arg is a keyword?"}, 

	{ (char*)"\r\n---- Marking & Parser Info",0,0,0,(char*)""},
	{ (char*)"^mark",MarkCode,STREAM_ARG,SAMELINE,(char*)"mark word/concept in sentence"},
	{ (char*)"^marked",MarkedCode,1,SAMELINE,(char*)"BOOLEAN - is word/concept marked in sentence"}, 
	{ (char*)"^position",PositionCode,STREAM_ARG,SAMELINE,(char*)"get FIRST or LAST position of an _ var"}, 
	{ (char*)"^setposition",SetPositionCode,STREAM_ARG,SAMELINE,(char*)"set absolute match position"}, 
	{ (char*)"^setpronoun",SetPronounCode,STREAM_ARG,SAMELINE,(char*)"replace pronoun with word"}, 
	{ (char*)"^unmark",UnmarkCode,STREAM_ARG,SAMELINE,(char*)"remove a mark on a word/concept in the sentence"}, 
	{ (char*)"^setcanon",SetCanonCode,2,SAMELINE,(char*)"for word n, set its canonical value"}, 
	{ (char*)"^settag",SetTagCode,2,SAMELINE,(char*)"for word n, set its postag concept"}, 
	{ (char*)"^setoriginal",SetOriginalCode,2,SAMELINE,(char*)"for word n, set its original value"}, 
	{ (char*)"^setrole",SetRoleCode,2,SAMELINE,(char*)"for word n, set its parse role value"}, 

	{ (char*)"\r\n---- Input",0,0,0,(char*)""},
	{ (char*)"^analyze",AnalyzeCode,STREAM_ARG,0,(char*)"Take an output stream and do preparation on it like it was user input"}, 
	{ (char*)"^capitalized",CapitalizedCode,1,SAMELINE,(char*)"given index of word in sentence return 1 or 0 for whether user capitalized it"}, 
	{ (char*)"^decodepos",DecodePosCode,2,SAMELINE,(char*)"decode into text the pos bits of given pos (POS) or role (ROLE) "}, 
	{ (char*)"^decodeinputtoken",DecodeInputTokenCode,1,SAMELINE,(char*)"Display flags of a cs_token or %token value "}, 
	{ (char*)"^input",InputCode,STREAM_ARG,0,(char*)"submit stream as input immediately after current input"},
	{ (char*)"^original",OriginalCode,STREAM_ARG,0,(char*)"retrieve raw user input corresponding to this match variable"},
	{ (char*)"^actualinputrange",ActualInputRangeCode,2,0,(char*)"what range in actual input does this range in original input generate"},
	{ (char*)"^originalinputrange",OriginalInputRangeCode,2,0,(char*)"what range in original input does this range in actual input come from"},
	{ (char*)"^partofspeech",PartOfSpeechCode,STREAM_ARG,SAMELINE,(char*)"given index of word in sentence return 64-bit pos data from parsing"}, 
	{ (char*)"^phrase",PhraseCode,STREAM_ARG,0,(char*)"get noun or prep phrase at location, possibly canonical"},
	{ (char*)"^removetokenflags",RemoveTokenFlagsCode,1,SAMELINE,(char*)"remove value from tokenflags"}, 
	{ (char*)"^role",RoleCode,STREAM_ARG,SAMELINE,(char*)"given index of word in sentence return 32-bit role data from parsing"}, 
	{ (char*)"^settokenflags",SetTokenFlagsCode,1,SAMELINE,(char*)"add value to tokenflags"}, 
	{ (char*)"^setwildcardindex",SetWildcardIndexCode,STREAM_ARG,SAMELINE,(char*)"resume wildcard allocation at this number"}, 

	{ (char*)"\r\n---- Numbers",0,0,0,(char*)""},
	{ (char*)"^compute",ComputeCode,3,SAMELINE,(char*)"perform a numerical computation"}, 
	{ (char*)"^isnumber",IsNumberCode,1,SAMELINE,(char*)"is this an integer or float number or currency"}, 
	{ (char*)"^timefromseconds",TimeFromSecondsCode,VARIABLE_ARG_COUNT,SAMELINE,(char*)"given time/date in seconds, return the timeinfo string corresponding to it"}, 
	{ (char*)"^timeinfofromseconds",TimeInfoFromSecondsCode,1,SAMELINE,(char*)"given time/date in seconds, returning a sequence of 6 matchvariables (sec min hr date mo yr)"}, 
	{ (char*)"^timetoseconds",TimeToSecondsCode,VARIABLE_ARG_COUNT,SAMELINE,(char*)"given time/date a series of 6 values (sec min hr date mo yr), return the timeinfo string corresponding to it"}, 

	{ (char*)"\r\n---- Debugging",0,0,0,(char*)""},
	{ (char*)"^debug",DebugCode,VARIABLE_ARG_COUNT,SAMELINE,(char*)"only useful for debug code breakpoint"}, 
	{ (char*)"^identify",IdentifyCode,0,SAMELINE,(char*)"report CS version info"}, 
	{ (char*)"^log",LogCode,STREAM_ARG,0,(char*)"add to logfile"}, 

	{ (char*)"\r\n---- Output Generation - not legal in post processing",0,0,0,(char*)""},
	{ (char*)"^flushoutput",FlushOutputCode,0,SAMELINE,(char*)"force existing output out"}, 
	{ (char*)"^insertprint",InsertPrintCode,STREAM_ARG,0,(char*)"add output before named responseIndex"},
	{ (char*)"^keephistory",KeepHistoryCode,2,SAMELINE,(char*)"trim history of USER or BOT to number of entries given -- see also $cs_userhistorylimit"}, 
	{ (char*)"^print",PrintCode,STREAM_ARG,0,(char*)"isolated output message from current stream"}, 
	{ (char*)"^preprint",PrePrintCode,STREAM_ARG,0,(char*)"add output before existing output"}, 
	{ (char*)"^repeat",RepeatCode,0,SAMELINE,(char*)"set repeat flag so can repeat output"}, 
	{ (char*)"^reviseoutput",ReviseOutputCode,2,0,(char*)"takes index and output, replacing output at that index"}, 

	{ (char*)"\r\n---- Output Access",0,0,0,(char*)""},
	{ (char*)"^lastsaid",LastSaidCode,0,0,(char*)"get what chatbot said just before"},
	{ (char*)"^response",ResponseCode,1,0,(char*)"raw text for this response, including punctuation"},
	{ (char*)"^responsequestion",ResponseQuestionCode,1,SAMELINE,(char*)"BOOLEAN - 1 if response ends in ?  0 otherwise"}, 
	{ (char*)"^responseruleid",ResponseRuleIDCode,1,SAMELINE,(char*)"rule tag generating this response"},
	
	{ (char*)"\r\n---- Postprocessing functions - only available in postprocessing",0,0,0,(char*)""},
	{ (char*)"^postprintbefore",PostPrintBeforeCode,STREAM_ARG,0,(char*)"add to front of output stream"}, 
	{ (char*)"^postprintafter",PostPrintAfterCode,STREAM_ARG,0,(char*)"add to end of output stream"}, 

	{ (char*)"\r\n---- Control Flow",0,0,0,(char*)""},
	{ (char*)"^authorized",AuthorizedCode,0,0,(char*)"is current user authorized"},
	{ (char*)"^addcontext",AddContextCode,2,0,(char*)"set topic and label as a context"},
	{ (char*)"^clearcontext",ClearContextCode,2,0,(char*)"clear all context"},
	{ (char*)"^argument",ArgumentCode,VARIABLE_ARG_COUNT,0,(char*)"returns the calling scope's nth argument (given n and possible fn name)"},
	{ (char*)"^command",CommandCode,STREAM_ARG,0,(char*)"execute a : command"}, 
	{ (char*)"^end",EndCode,1,SAMELINE,(char*)"cease current processing thru this level"}, 
	{ (char*)"^eval",EvalCode,STREAM_ARG,0,(char*)"evaluate stream"}, 
	{ (char*)"^fail",FailCode,1,SAMELINE,(char*)"return a return code of some kind - allowed to erase facts on sentence fail"}, 
	{ (char*)"^incontext",InContextCode,1,0,(char*)"returns normally if given label or topic.label have output recently else fails"},
	{ (char*)"^load",LoadCode,1,0,(char*)"Dynamic load of a layer as layer 2"},
	{ (char*)"^match",MatchCode,STREAM_ARG,0,(char*)"Perform given pattern match"},
	{ (char*)"^matches",MatchesCode,0,0,(char*)"note the indexes of the words matched by the most recent pattern"},
	{ (char*)"^memoryfree",MemoryFreeCode,0,0,(char*)"release dict,fact,text allocated since last memorymark"},
	{ (char*)"^memorymark",MemoryMarkCode,0,0,(char*)"note memory information for later memory free"}, 
	{ (char*)"^norejoinder",NoRejoinderCode,0,0,(char*)"block assigning rejoinder from this rule"}, 
	{ (char*)"^nofail",NoFailCode,STREAM_ARG,0,(char*)"execute script but ignore all failures thru some level"}, 
	{ (char*)"^notnull",NotNullCode,STREAM_ARG,0,(char*)"tests that output of stream argument is not null, fails otherwise"}, 
	{ (char*)"^result",ResultCode,STREAM_ARG,0,(char*)"executes the stream and returns the result code (never fails) "}, 
	{ (char*)"^retry",RetryCode,VARIABLE_ARG_COUNT,SAMELINE,(char*)"reexecute a rule with a later match or retry  input"},
	{ (char*)"^return",ReturnCode,STREAM_ARG,SAMELINE,(char*)"return this value from current user function call"}, 
	{ (char*)"^notrace",NoTraceCode,STREAM_ARG,0,(char*)"execute code with trace off (except for topics and functions)"}, 
	{ (char*)"^savesentence",SaveSentenceCode,1,0,(char*)"memorize current sentence analysis given label"}, 
	{ (char*)"^restoresentence",RestoreSentenceCode,1,0,(char*)"recover prior saved sentence analysis given label"}, 
	{ (char*)"^sleep",SleepCode,1,0,(char*)"wait n milliseconds"}, 
	{ (char*)"^if",IfCode,STREAM_ARG,0,(char*)"the if statement"}, 
	{ (char*)"^loop",LoopCode,STREAM_ARG,0,(char*)"the loop statement"}, 
	{ (char*)"^environment",EnvironmentCode,1,0,(char*)"get os environment variable"}, 
	{ (char*)"^backtrace",BacktraceCode,0,0,(char*)"show callstack"}, 

	{ (char*)"\r\n---- External Databases",0,0,0,(char*)""},
#ifndef DISCARDPOSTGRES
	{ (char*)"\r\n---- Database",0,0,0,(char*)""},
	{ (char*)"^dbinit",DBInitCode,STREAM_ARG,0,(char*)"access a postgres database"}, 
	{ (char*)"^dbclose",DBCloseCode,0,0,(char*)"close current postgres database"}, 
	{ (char*)"^dbexecute",DBExecuteCode,STREAM_ARG,0,(char*)"perform postgres transactions"}, 
#endif
	
#ifndef DISCARDMONGO
	{ (char*)"\r\n---- Mongo Database",0,0,0,(char*)""},
	{ (char*)"^mongoinit",MongoInit,3,0,(char*)"establish connection to mongo database"}, 
	{ (char*)"^mongoclose",MongoClose,0,0,(char*)"destroy connection to mongo database"}, 
	{ (char*)"^mongoinsertdocument",mongoInsertDocument,2,0,(char*)"mongo upsert document"},
	{ (char*)"^mongodeletedocument",mongoDeleteDocument,VARIABLE_ARG_COUNT,0,(char*)"mongo delete document"},
	{ (char*)"^mongofinddocument",mongoFindDocument,VARIABLE_ARG_COUNT,0,(char*)"mongo find document"},
#endif

	{ (char*)"\r\n---- Word Manipulation",0,0,0,(char*)""},
	{ (char*)"^addproperty",AddPropertyCode,STREAM_ARG,0,(char*)"Add value to dictionary entry properies or systemFlags or facts of factset properties"}, 
	{ (char*)"^burst",BurstCode,VARIABLE_ARG_COUNT,0,(char*)"break a string into component words either to facts or matchvars - if 1st arg is count, returns number of words"}, 
	{ (char*)"^canon",CanonCode,2, 0, "Add word + canon to canon file while compiling a table"},
	{ (char*)"^define",DefineCode,VARIABLE_ARG_COUNT,0,(char*)"get dictionary gloss of  word"}, 
	{ (char*)"^extract",ExtractCode,3,0,(char*)"pull out text from given string starting at position and ending at position"}, 
	{ (char*)"^findtext",FindTextCode,VARIABLE_ARG_COUNT,0,(char*)"locate start position in target of given string starting at offset"}, 
	{ (char*)"^flags",FlagsCode,1,0,(char*)"get flag values of word"}, 
	{ (char*)"^hasanyproperty",HasAnyPropertyCode,VARIABLE_ARG_COUNT,0,(char*)"argument 1 has any of property or systemflags of argument2 .. argumentn"}, 
    { (char*)"^hasallproperty",HasAllPropertyCode,VARIABLE_ARG_COUNT,0,(char*)"argument 1 has all of the properties or systemflags of argument2 .. argumentn"}, 
	{ (char*)"^uppercase",UppercaseCode,1,0,(char*)"boolean return 1 if word was entered uppercase, 0 if not"}, 
	{ (char*)"^layer",LayerCode,1,0,(char*)"get layer of where word showed up"}, 
	{ (char*)"^properties",PropertiesCode,1,0,(char*)"get property values of word"}, 
	{ (char*)"^intersectwords",IntersectWordsCode,VARIABLE_ARG_COUNT,0,(char*)"see if words in arg 1 are in arg2"},
	{ (char*)"^join",JoinCode,STREAM_ARG,0,(char*)"merge words into one"}, 
	{ (char*)"^pos",POSCode,VARIABLE_ARG_COUNT,0,(char*)"compute some part of speech value"},
	{ (char*)"^removeinternalflag",RemoveInternalFlagCode,2,0,(char*)"Remove internal flag from word- currently only HAS_SUBSTITUTE"}, 
	{ (char*)"^removeproperty",RemovePropertyCode,STREAM_ARG,0,(char*)"remove value to dictionary entry properies or systemFlags or facts of factset properties"},
	{ (char*)"^rhyme",RhymeCode,1,0,(char*)"find a rhyming word"}, 
	{ (char*)"^substitute",SubstituteCode,VARIABLE_ARG_COUNT,0,(char*)"alter a string by substitution"}, 
	{ (char*)"^spell",SpellCode,1,0,(char*)"find words matching pattern and store as facts"}, 
	{ (char*)"^sexed",SexedCode,4,0,(char*)"pick a word based on sex of given word"}, 
	{ (char*)"^tally",TallyCode,VARIABLE_ARG_COUNT,0,(char*)"get or set a word value"},
	{ (char*)"^walkdictionary",WalkDictionaryCode,1,0,(char*)"call a function on every word in the dictionary"},
#ifndef DISCARDCOUNTER
	{ (char*)"^wordcount",WordCountCode,VARIABLE_ARG_COUNT,0,(char*)"get or set a word count value"},
#endif
	{ (char*)"^words",WordsCode,1,0,(char*)"given a word, get all words in dictionary with that spelling- case and space/_ independent"},
	{ (char*)"^wordinconcept",WordInConceptCode,VARIABLE_ARG_COUNT,0,(char*)"given a word and a concept (and fail option), get the word in dictionary with that spelling- case and space/_ independent in that concept"},
	{ (char*)"\r\n---- MultiPurpose Functions",0,0,0,(char*)""},
	{ (char*)"^disable",DisableCode,VARIABLE_ARG_COUNT,SAMELINE,(char*)"stop a RULE or TOPIC or INPUTREJOINDER or OUTPUTREJOINDER or SAVE"}, 
	{ (char*)"^enable",EnableCode,VARIABLE_ARG_COUNT,SAMELINE,(char*)"allow a rule or topic"}, 
	{ (char*)"^length",LengthCode,1,SAMELINE,(char*)"counts characters in a word or members of a fact set or top level concept members or elements in json array or object"}, 
	{ (char*)"^next",NextCode,STREAM_ARG,0,(char*)"FACT- walk a factset without erasing it  GAMBIT,RESPONDER,RULE,REJOINDER with tag or label for next one  INPUT to go to next sentence"}, 
	{ (char*)"^pick",FLRCodeR,STREAM_ARG,0,(char*)"randomly select and remove an element from a factset or randomly select from a concept or factid from a JSON object or array"}, 
	{ (char*)"^reset",ResetCode,VARIABLE_ARG_COUNT,0,(char*)"reset a topic or all topics or user or pending output back to initial state "}, 

	{ (char*)"\r\n---- Functions on facts",0,0,0,(char*)""},
	{ (char*)"^conceptlist",ConceptListCode,STREAM_ARG,0,(char*)"create facts of the concepts or topics or both triggers by word at position or overall"}, 
	{ (char*)"^createattribute",CreateAttributeCode,STREAM_ARG,0,(char*)"create a triple where the 3rd field is exclusive"}, 
	{ (char*)"^createfact",CreateFactCode,STREAM_ARG,0,(char*)"create a triple"}, 
	{ (char*)"^delete",DeleteCode,1,0,(char*)"delete all facts in factset or delete named fact or delete named json object or array"}, 
	{ (char*)"^deserialize", DeserializeCode, 1, 0, "transcribes a string into a factset" },
	{ (char*)"^field",FieldCode,2,0,(char*)"get a field of a fact"}, 
	{ (char*)"^find",FindCode,2,0,(char*)"Given set or factset, find ordinal position of item within it"},
	{ (char*)"^findfact",FindFactCode,3,0,(char*)"given simple non-facts subject verb object, see if fact exists of it"},
	{ (char*)"^findmarkedfact",FindMarkedFactCode,3,0,(char*)"given a subject,a verb, and a mark, return a marked fact that can be found propogating from subject using verb  or null"},
	{ (char*)"^first",FLRCodeF,STREAM_ARG,0,(char*)"get first element of a factset and remove it"},
	{ (char*)"^flushfacts",FlushFactsCode,1,0,(char*)"erase all facts created after here"}, 
	{ (char*)"^intersectfacts",IntersectFactsCode,STREAM_ARG,0,(char*)"find facts common to two factsets, based on fields"},
	{ (char*)"^iterator",IteratorCode,3,0,(char*)"walk facts of some thing"},
	{ (char*)"^makereal",MakeRealCode,0,0,(char*)"make all transient facts non-transient"},
	{ (char*)"^nth",NthCode,STREAM_ARG,0,(char*)"from factset get nth element (kept) or from set get nth element"},
	{ (char*)"^revisefact",ReviseFactCode,4,0,(char*)"revise a triple"}, 
	{ (char*)"^uniquefacts",UniqueFactsCode,STREAM_ARG,0,(char*)"find facts in first set not found in second"},
	{ (char*)"^last",FLRCodeL,STREAM_ARG,0,(char*)"get last element of a factset and remove it"},
	{ (char*)"^query",QueryCode,STREAM_ARG,0,(char*)"hunt for fact in fact database"},
	{ (char*)"^serialize", SerializeCode, 1, 0, "transcribes a factset into a string" },
	{ (char*)"^sort",SortCode,STREAM_ARG,0,(char*)"sort facts on named set-field (presumed number) low to high"},
	{ (char*)"^unduplicate",UnduplicateCode,1,0,(char*)"remove duplicate facts"},
	{ (char*)"^unpackfactref",UnpackFactRefCode,1,0,(char*)"copy out fields which are facts"}, 
	{ (char*)"^writefact",WriteFactCode,1,0,(char*)"take fact index and print out the fact suitable to be read again"}, 

	{ (char*)"\r\n---- External Access",0,0,0,(char*)""},
	{ (char*)"^export",ExportCode,VARIABLE_ARG_COUNT,SAMELINE,(char*)"write fact set to a file"},
	{ (char*)"^import",ImportCode,4,SAMELINE,(char*)"read fact set from a file"}, 
	{ (char*)"^system",SystemCode,STREAM_ARG,SAMELINE,(char*)"send command to the operating system"},
	{ (char*)"^popen",PopenCode,2,SAMELINE,(char*)"send command to the operating system and read reply strings"},
	{ (char*)"^tcpopen",TCPOpenCode,4,SAMELINE,(char*)"send command to website and read reply strings"},
	{ (char*)"^getremotefile",GetRemoteFileCode,VARIABLE_ARG_COUNT,SAMELINE,(char*)"retrieve data from external fileserver"},

#ifndef DISCARDJSON
	{ "\r\n---- JSON Related", 0, 0, 0, "" },
	{ "^jsoncopy", JSONCopyCode, VARIABLE_ARG_COUNT, 0, "given json array or json object, creates a duplicate copy" },
	{ "^jsoncreate", JSONCreateCode, VARIABLE_ARG_COUNT, 0, "given array or object, creates a new one" },
	{ "^jsondelete", JSONDeleteCode, 1, 0, "deprecated in favor of ^delete" },
	{ "^jsongather", JSONGatherCode, VARIABLE_ARG_COUNT, 0, "stores the json facts referred to by the name into a fact set" },
	{ "^jsonkind", JSONKindCode, 1, 0, "returns object, array, or fails depending on what is passed to it" },
	{ "^jsonarraysize", JSONArraySizeCode, 1, 0, "deprecated in favor of ^length" },
	{ "^jsonarrayinsert", JSONArrayInsertCode, VARIABLE_ARG_COUNT, 0, "given name of json array fact, adds given  value BEFORE or AFTER the given" },
	{ "^jsonarraydelete", JSONArrayDeleteCode, VARIABLE_ARG_COUNT, 0, "given name of json array and index of fact to remove, removes it and renumbers all after it down" },
	{ "^jsonobjectinsert", JSONObjectInsertCode, VARIABLE_ARG_COUNT, 0, "given name of json object, adds given key and value" },
	{ "^jsonparse", JSONParseCode, VARIABLE_ARG_COUNT, 0, "parses the provided string argument to a set of facts accessible from ChatScript code" },
	{ "^jsonparsefile", JSONParseFileCode, VARIABLE_ARG_COUNT, 0, "parses the provided filename to a set of facts accessible from ChatScript code" },
	{ "^jsonformat", JSONFormatCode, 1, 0, "given a json text string, makes all field names use doublequotes and proper escaping" },
	{ "^jsonpath", JSONPathCode, VARIABLE_ARG_COUNT, 0, "retrieves the json value corresponding to a path and a given fact presumed to be array or object" },
	{ "^jsontree", JSONTreeCode, VARIABLE_ARG_COUNT, 0, "prints the hierarchy represented by the json node to depth if requested" },
	{ "^jsonwrite", JSONWriteCode, 1, 0, "prints out json string corresponding to the facts of the root name given" },
	{ "^jsonlabel", JSONLabelCode, 1, 0, "use label when creating json arrays and objects" },
	{ "^jsonundecodestring", JSONUndecodeStringCode, 1, 0, "remove escapes from json data" },
	{ "^jsonopen", JSONOpenCode, VARIABLE_ARG_COUNT, SAMELINE, "send command to JSON server and parse reply strings" },
#endif

#ifdef PRIVATE_CODE
	#include "../privatecode/privatetable.cpp"
#endif

	{0,0,0,0,(char*)""}	
};
