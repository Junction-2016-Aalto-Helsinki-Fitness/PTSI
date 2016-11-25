#include "common.h"

#ifdef INFORMATION

Facts are added as layers. You remove facts by unpeeling a layer. You can "delete" a fact merely by
marking it dead.

Layer-1:  facts resulting from wordnet dictionary  (wordnetFacts)
Layer0:	 facts resulting from topic system build0
Layer1:	 facts resulting from topic system build1
Layer2:	 facts created by user

Layer 2 is always unpeeled after an interchange with the chatbot, in preparation for a new user who may chat with a different persona.
Layers 0 and 1 are unpeeled if you want to restart the topic system to read in new topic data on the fly or rebuild 0.
Layer 1 is unpeeled for a new build 1.

Layer -1 is never unpeeled. If you want to modify the dictionary, you either restart the chatbot entirely
or patch in data piggy backing on facts (like from the topic system).

Unpeeling a layer implies you will also reset dictionary/stringspace pointers back to levels at the
start of the layer since facts may have allocated dictionary and string items. 
This is ReturnToDictionaryFreeze for unpeeling 3/4 and ReturnDictionaryToWordNet for unpeeling layer 2.

#endif
char traceSubject[100];
char traceVerb[100];
char traceObject[100];

size_t maxFacts = MAX_FACT_NODES;	// how many facts we can create at max

FACT* factBase = NULL;			// start of all facts
FACT* factEnd = NULL;			// end of all facts
FACT* factsPreBuild[NUMBER_OF_LAYERS+1];		// end of build0 facts, start of build1 facts
FACT* factFree = NULL;			// currently next fact we can reuse for allocation
FACT* currentFact = NULL;		// current fact

//   values of verbs to compare against
MEANING Mmember;				// represents concept sets
MEANING Mis;					// represents wordnet hierarchy
MEANING Mexclude;				// represents restriction of word not allowed in set (blocking inheritance)

FACT* Index2Fact(FACTOID e)
{ 
	FACT* F = NULL;
	if (e)
	{
		F =  e + factBase;
		if (F > factFree)
		{
			ReportBug((char*)"Illegal fact index")
			F = NULL;
		}
	}
	return F;
}

FACT* GetSubjectNext(FACT* F) { return Index2Fact(F->subjectNext);}
FACT* GetVerbNext(FACT* F) {return Index2Fact(F->verbNext);}
FACT* GetObjectNext(FACT* F) {return Index2Fact(F->objectNext);}

FACT* GetSubjectNondeadNext(FACT* F) 
{ 
	while (F) 
	{
		F =  Index2Fact(F->subjectNext);
		if (F && ! (F->flags & FACTDEAD)) break;
	}
	return F;
}

FACT* GetVerbNondeadNext(FACT* F) 
{
	while (F) 
	{
		F =  Index2Fact(F->verbNext);
		if (F && ! (F->flags & FACTDEAD)) break;
	}
	return F;
}

FACT* GetObjectNondeadNext(FACT* F) 
{
	while (F) 
	{
		F =  Index2Fact(F->objectNext);
		if (F && ! (F->flags & FACTDEAD)) break;
	}
	return F;
}

FACT* GetSubjectNondeadHead(FACT* F) 
{
	F = Index2Fact(F->subjectHead);
	while (F && F->flags & FACTDEAD) 
	{
		F =  Index2Fact(F->subjectNext);
	}
	return F;
}

FACT* GetVerbNondeadHead(FACT* F) 
{
	F = Index2Fact(F->verbHead);
	while (F && F->flags & FACTDEAD) 
	{
		F =  Index2Fact(F->verbNext);
	}
	return F;
}

FACT* GetObjectNondeadHead(FACT* F) 
{
	F = Index2Fact(F->objectHead);
	while (F && F->flags & FACTDEAD) 
	{
		F =  Index2Fact(F->objectNext);
	}
	return F;
}

FACT* GetSubjectNondeadHead(WORDP D) 
{
	FACT* F = Index2Fact(D->subjectHead);
	while (F && F->flags & FACTDEAD) 
	{
		F =  Index2Fact(F->subjectNext);
	}

	return F;
}

FACT* GetVerbNondeadHead(WORDP D) 
{
	FACT* F = Index2Fact(D->verbHead);
	while (F && F->flags & FACTDEAD) 
	{
		F =  Index2Fact(F->verbNext);
	}
	return F;
}

FACT* GetObjectNondeadHead(WORDP D)  
{
	FACT* F = Index2Fact(D->objectHead);
	while (F && F->flags & FACTDEAD) 
	{
		F =  Index2Fact(F->objectNext);
	}
	return F;
}


void SetSubjectNext(FACT* F, FACT* value){ F->subjectNext = Fact2Index(value);}
void SetVerbNext(FACT* F, FACT* value) {F->verbNext = Fact2Index(value);}
void SetObjectNext(FACT* F, FACT* value){ F->objectNext = Fact2Index(value);}

FACTOID Fact2Index(FACT* F) 
{ 
	return  (F) ? ((FACTOID)(F - factBase)) : 0;
}

FACT* FactTextIndex2Fact(char* id) // given number word, get corresponding fact
{
	char word[MAX_WORD_SIZE];
	strcpy(word,id);
	char* comma = word;
	while ((comma = strchr(comma,','))) memmove(comma,comma+1,strlen(comma));  // maybe number has commas, like 100,000,300 . remove them
	unsigned int n = atoi(word);
	if (n <= 0) return NULL;
	return (n <= (unsigned int)(factFree-factBase)) ? Index2Fact(n) : NULL;
}

int GetSetID(char* x)
{
	if (*x != '@') return ILLEGAL_FACTSET;
	if (!IsDigit(*++x)) return ILLEGAL_FACTSET; 
	unsigned int n = *x - '0';
	if (IsDigit(*++x)) n = (n * 10) + *x - '0';
	// allow additional characters naming a field
	return (n > MAX_FIND_SETS) ? ILLEGAL_FACTSET : n;
}

bool GetSetMod(char* x)
{
	if (!IsDigit(*++x)) return false; // illegal set
	if (IsDigit(*++x)) ++x;
	if ((*x == 's' || *x == 'S' ) && strnicmp(x,(char*)"subject",7)) return false;
	if ((*x == 'v' || *x == 'V' )&& strnicmp(x,(char*)"verb",4)) return false;
	if ((*x == 'o' || *x == 'O' )&& strnicmp(x,(char*)"object",6)) return false;
	if ((*x == 'a' || *x == 'A' )&& strnicmp(x,(char*)"all",4)) return false;
	if ((*x == 'f'|| *x == 'F' ) && strnicmp(x,(char*)"fact",4)) return false;
	return true;
}

char* GetSetType(char* x)
{ // @13subject returns subject
	x += 2;
	if (IsDigit(*x)) ++x;
	return x;
}

char* GetSetEnd(char* x)
{ // @adagsubject returns subject
	char* start = x;
	while (IsAlphaUTF8(*++x)){;} // find natural end.
	size_t len = x - start;	 // this long
	if (len >= 9 && !strnicmp(x-7,(char*)"subject",7)) x -= 7;
	else if (len >= 6 && !strnicmp(x-4,(char*)"verb",4)) x -= 4;
	else if (len >= 8 && !strnicmp(x-6,(char*)"object",6)) x -= 6;
	else if (len >= 6 && !strnicmp(x-4,(char*)"fact",4)) x -= 4;
	else if (len >= 5 && !strnicmp(x-3,(char*)"all",3)) x -= 3;

	if (IsDigit(*x)) ++x;
	return x; 
}

void TraceFact(FACT* F,bool ignoreDead)
{
	char* word = AllocateInverseString(NULL,MAX_BUFFER_SIZE);
	Log(STDTRACELOG,(char*)"%d: %s\r\n",Fact2Index(F),WriteFact(F,false,word,ignoreDead,false));
	Log(STDTRACETABLOG,(char*)"");
	ReleaseInverseString(word);
}

void ClearUserFacts()
{
	while (factFree > factLocked)  FreeFact(factFree--); //   erase new facts
}

void InitFacts()
{
	*traceSubject = 0;
	*traceVerb = 0;
	*traceObject = 0;

	if ( factBase == 0) 
	{
		factBase = (FACT*) malloc(maxFacts * sizeof(FACT)); // only on 1st startup, not on reload
		if ( factBase == 0)
		{
			printf((char*)"%s",(char*)"failed to allocate fact space\r\n");
			myexit((char*)"failed to get fact space");
		}
	}
	memset(factBase,0,sizeof(FACT) *  maxFacts); // not strictly necessary
    factFree = factBase;
	factEnd = factBase + maxFacts;
}

void InitFactWords()
{
	//   special internal fact markers
	Mmember = MakeMeaning(StoreWord((char*)"member"));
	Mexclude = MakeMeaning(StoreWord((char*)"exclude"));
	Mis = MakeMeaning(StoreWord((char*)"is"));
}

void CloseFacts()
{
    free(factBase);
	factBase = 0;
}

void FreeFact(FACT* F)
{ //   most recent facts are always at the top of any xref list. Can only free facts sequentially backwards.
	if (!F->subject) // unindexed fact recording a fact delete that must be undeleted
	{
		if (!F->object) // death of fact fact (verb but no object)
		{
			F = Index2Fact(F->verb);
			F->flags &= -1 ^ FACTDEAD;
		}
		else if (F->flags & ITERATOR_FACT) {;} // holds iterator backtrack
		else // undo a variable assignment fact (verb and object)
		{
			WORDP D = Meaning2Word(F->verb); // the variable
			unsigned int offset = (unsigned int) F->object;
			D->w.userValue =  (offset == 1) ? NULL : (stringBase + offset);
		}
	}
    else // normal indexed fact
	{
		if (!(F->flags & FACTSUBJECT)) SetSubjectHead(F->subject,GetSubjectNext(F)); // dont use nondead
		else SetSubjectHead(Index2Fact(F->subject),GetSubjectNext(F)); // dont use nondead

		if (!(F->flags & FACTVERB)) SetVerbHead(F->verb,GetVerbNext(F)); // dont use nondead
		else  SetVerbHead(Index2Fact(F->verb),GetVerbNext(F)); // dont use nondead

		if (!(F->flags & FACTOBJECT)) SetObjectHead(F->object,GetObjectNext(F)); // dont use nondead
		else SetObjectHead(Index2Fact(F->object),GetObjectNext(F)); // dont use nondead
	}
 }

unsigned int AddFact(unsigned int set, FACT* F) // fact added to factset
{
	unsigned int count = FACTSET_COUNT(set);
	if (++count > MAX_FIND) --count; 
	factSet[set][count] = F;
	SET_FACTSET_COUNT(set,count);
	return count;
}

FACT* SpecialFact(FACTOID_OR_MEANING verb, FACTOID_OR_MEANING object,unsigned int flags)
{
	//   allocate a fact
	if (++factFree == factEnd) 
	{
		--factFree;
		ReportBug((char*)"out of fact space at %d",Fact2Index(factFree))
		printf((char*)"%s",(char*)"out of fact space");
		return factFree; // dont return null because we dont want to crash anywhere
	}
	//   init the basics
	memset(factFree,0,sizeof(FACT));
	factFree->verb = verb;
	factFree->object = object;
	factFree->flags = FACTTRANSIENT | FACTDEAD | flags;	// cannot be written or kept
	return factFree;
}

void KillFact(FACT* F,bool jsonrecurse)
{
	if (!F || F->flags & FACTDEAD) return; // already dead
	
	if (F <= factsPreBuild[2]) 
		return;	 // may not kill off facts built into world


	WORDP x = Meaning2Word(F->object);
	if (trace & TRACE_FACT && CheckTopicTrace()) 
	{
		Log(STDTRACELOG,(char*)"Kill: ");
		TraceFact(F);
	}
#ifndef DISCARDJSON
	// recurse on JSON datastructures below if they are being deleted on right side
	if (F->flags & JSON_ARRAY_VALUE && jsonrecurse)
	{
		WORDP jsonarray = Meaning2Word(F->object);
		jkillfact(jsonarray);
	}
	if (F->flags & JSON_OBJECT_VALUE && jsonrecurse)
	{
		WORDP jsonobject = Meaning2Word(F->object);
		jkillfact(jsonobject);
	}
	if (F->flags & JSON_ARRAY_FACT) JsonRenumber(F);// have to renumber this array
#endif
	F->flags |= FACTDEAD; 

	if (planning) SpecialFact(Fact2Index(F),0,0); // save to restore

	// if this fact has facts depending on it, they too must die
	FACT* G = GetSubjectNondeadHead(F);
	while (G)
	{
		KillFact(G);
		G = GetSubjectNondeadNext(G);
	}
	G = GetVerbNondeadHead(F);
	while (G)
	{
		KillFact(G);
		G = GetVerbNondeadNext(G);
	}
	G = GetObjectNondeadHead(F);
	while (G)
	{
		KillFact(G);
		G = GetObjectNondeadNext(G);
	}
}

void ResetFactSystem(FACT* locked)
{
	while (factFree > locked) FreeFact(factFree--); // restore to end of basic facts
	for (unsigned int i = 0; i <= MAX_FIND_SETS; ++i)  // clear any factset with contaminated data
	{
		unsigned int limit = FACTSET_COUNT(i);
		for (unsigned int k = 1; k <= limit; ++k)
		{
			if (factSet[i][k] > locked) 
			{
				SET_FACTSET_COUNT(i, 0); // empty all facts sets
				break;
			}
		}
	}
}

FACT* FindFact(FACTOID_OR_MEANING subject, FACTOID_OR_MEANING verb, FACTOID_OR_MEANING object, unsigned int properties)
{
    FACT* F;
	FACT* G;
	if (!subject || !verb || !object) return NULL;
	if (properties & FACTDUPLICATE) 
		return NULL;	// can never find this since we are allowed to duplicate and we do NOT want to share any existing fact

    //   see if  fact already exists. if so, just return it 
	if (properties & FACTSUBJECT)
	{
		F  = Index2Fact(subject);
		G = GetSubjectNondeadHead(F);
		while (G)
		{
			if (G->verb == verb &&  G->object == object && G->flags == properties) return G;
			G  = GetSubjectNondeadNext(G);
		}
		return NULL;
	}
	else if (properties & FACTVERB)
	{
		F  = Index2Fact(verb);
		G = GetVerbNondeadHead(F);
		while (G)
		{
			if (G->subject == subject && G->object == object &&  G->flags == properties) return G;
			G  = GetVerbNondeadNext(G);
		}
		return NULL;
	}   
 	else if (properties & FACTOBJECT)
	{
		F  = Index2Fact(object);
		G = GetObjectNondeadHead(F);
		while (G)
		{
			if (G->subject == subject && G->verb == verb &&  G->flags == properties) return G;
			G  = GetObjectNondeadNext(G);
		}
		return NULL;
	}
  	//   simple FACT* based on dictionary entries
	F = GetSubjectNondeadHead(Meaning2Word(subject));
    while (F)
    {
		if ( F->subject == subject &&  F->verb ==  verb && F->object == object && properties == F->flags)  return F;
		F = GetSubjectNondeadNext(F);
    }
    return NULL;
}

FACT* CreateFact(FACTOID_OR_MEANING subject, FACTOID_OR_MEANING verb, FACTOID_OR_MEANING object, unsigned int properties)
{
	currentFact = NULL; 
	if (!subject || !object || !verb)
	{
		if (!subject) ReportBug((char*)"Missing subject field in fact create at line %d of %s",currentFileLine,currentFilename)
		if (!verb) ReportBug((char*)"Missing verb field in fact create at line %d of %s",currentFileLine,currentFilename)
		if (!object) ReportBug((char*)"Missing object field in fact create at line %d of %s",currentFileLine,currentFilename)
		return NULL;
	}

	//   get correct field values
    WORDP s = (properties & FACTSUBJECT) ? NULL : Meaning2Word(subject);
    WORDP v = (properties & FACTVERB) ? NULL : Meaning2Word(verb);
	WORDP o = (properties & FACTOBJECT) ? NULL : Meaning2Word(object);
	if (s && *s->word == 0)
	{
		ReportBug((char*)"bad choice in fact subject")
		return NULL;
	}
	if (v && *v->word == 0)
	{
		ReportBug((char*)"bad choice in fact verb")
		return NULL;
	}
	if (o && *o->word == 0)
	{
		ReportBug((char*)"bad choice in fact object")
		return NULL;
	}

	// convert any restricted meaning
	MEANING other;
	char* word;
	if (s && (word = s->word) && *word != '"' && strchr(word+1,'~')) // has number or word after it? convert notation
	{
		other = ReadMeaning(word,true,true);
		if (Meaning2Index(other) || GETTYPERESTRICTION(other)) subject = other;
	}
	if (v && (word = v->word) && strchr(word+1,'~')) // has number or word after it? convert notation
	{
		other = ReadMeaning(word,true,true);
		if (Meaning2Index(other) || GETTYPERESTRICTION(other)) verb = other;
	}
	if (o && (word = o->word) &&  strchr(word+1,'~')) // has number or word after it? convert notation
	{
		other = ReadMeaning(word,true,true);
		if (Meaning2Index(other) || GETTYPERESTRICTION(other)) object = other;
	}
	if (*traceSubject)
	{
		if (s && !stricmp(s->word,traceSubject) && *traceVerb && *traceObject)
		{
			DebugCode(NULL);
		}
		else if (s && !stricmp(s->word,traceSubject) && v && !stricmp(s->word,traceVerb) && *traceObject)
		{
			DebugCode(NULL);
		}
		else if (s && !stricmp(s->word,traceSubject) && o && !stricmp(s->word,traceObject) && *traceVerb)
		{
			DebugCode(NULL);
		}
		else if (s && !stricmp(s->word,traceSubject) && v && !stricmp(s->word,traceVerb) && o && !stricmp(s->word,traceObject))
		{
			DebugCode(NULL);
		}
	}
	if (*traceVerb)
	{
		if (v && !stricmp(v->word,traceVerb) && *traceSubject && *traceObject)
		{
			DebugCode(NULL);
		}
		else if (v && !stricmp(s->word,traceVerb) && *traceSubject  && o && !stricmp(s->word,traceObject))
		{
			DebugCode(NULL);
		}
	}
	if (*traceObject)
	{
		if (o &&  !stricmp(o->word,traceObject)  && *traceSubject && *traceVerb)
		{
			DebugCode(NULL);
		}
	}

	//   insure fact is unique if requested
	currentFact =  (properties & FACTDUPLICATE) ? NULL : FindFact(subject,verb,object,properties); 
	if (trace & TRACE_FACT && currentFact && CheckTopicTrace())  Log(STDTRACELOG,(char*)" Found %d ", Fact2Index(currentFact));
	if (currentFact) return currentFact;
	currentFact = CreateFastFact(subject,verb,object,properties);
	if (trace & TRACE_FACT && currentFact && CheckTopicTrace())  
	{
		Log(STDTRACELOG,(char*)" Created %d\r\n", Fact2Index(currentFact));
		Log(STDTRACETABLOG,(char*)"");
	}

	return  currentFact;
}

bool ExportFacts(char* name, int set,char* append)
{ 
	if (set < 0 || set > MAX_FIND_SETS) return false;
	if ( *name == '"')
	{
		++name;
		size_t len = strlen(name);
		if (name[len-1] == '"') name[len-1] = 0;
	}

	char* buffer = GetFreeCache();
	char* base = buffer;

	char* word = AllocateInverseString(NULL,MAX_BUFFER_SIZE);
	unsigned int count = FACTSET_COUNT(set);
	for (unsigned int i = 1; i <= count; ++i)
	{
		FACT* F = factSet[set][i];
		if (!F) strcpy(buffer,(char*)"null ");
		else if ( !(F->flags & FACTDEAD))
		{
			unsigned int original = F->flags;
			F->flags &= -1 ^ FACTTRANSIENT;	// dont pass transient flag out
			strcpy(buffer,WriteFact(F,false,word,false,true));
			F->flags = original;
		}
		buffer += strlen(buffer);
	}
	FILE* out;
	if (strstr(name,"ltm")) out = userFileSystem.userCreate(name); // user ltm file
	else out = (append && !stricmp(append,"append")) ? FopenUTF8WriteAppend(name) : FopenUTF8Write(name);
	if (!out) 
	{
		ReleaseInverseString(word);
		return false;
	}
	if (strstr(name,"ltm")) 
	{
		EncryptableFileWrite(base,1,(buffer-base),out); // can change content of buffer
		userFileSystem.userClose(out);
	}
	else
	{
		fprintf(out,(char*)"%s",base);
		fclose(out);
	}
	FreeUserCache();
	ReleaseInverseString(word);
	return true;
}

static char* ExportJson1(char* jsonitem, char* buffer)
{
	if (!*jsonitem)
	{
		strcpy(buffer,(char*)"null\r\n");
		return buffer + strlen(buffer);
	}

	WORDP D = FindWord(jsonitem);
	if (!D) return buffer;
	FACT* F = GetSubjectNondeadHead(D);
	while (F)
	{
		if ( !(F->flags & FACTDEAD))
		{
			unsigned int original = F->flags;
			F->flags &= -1 ^ FACTTRANSIENT;	// dont pass transient flag out
			WriteFact(F,false,buffer,false,true);
			buffer += strlen(buffer);
			F->flags = original;
		}
		if (F->flags & (JSON_ARRAY_VALUE|JSON_OBJECT_VALUE)) buffer = ExportJson1(Meaning2Word(F->object)->word,buffer); // on object side of triple

		F = GetSubjectNondeadNext(F);
	}
	return buffer + strlen(buffer);
}

FunctionResult ExportJson(char* name, char* jsonitem, char* append)
{
	WORDP D;
	if (*jsonitem) 
	{
		D = FindWord(jsonitem);
		if (!D) return FAILRULE_BIT;
	}
	if ( *name == '"')
	{
		++name;
		size_t len = strlen(name);
		if (name[len-1] == '"') name[len-1] = 0;
	}

	FILE* out;
	if (strstr(name,"ltm")) out = userFileSystem.userCreate(name); // user ltm file
	else out = (append && !stricmp(append,"append")) ? FopenUTF8WriteAppend(name) : FopenUTF8Write(name);
	if (!out) return FAILRULE_BIT;

	char* buffer = GetFreeCache(); // filesize limit
	ExportJson1(jsonitem, buffer);
	if (strstr(name,"ltm"))
	{
		EncryptableFileWrite(buffer,1,strlen(buffer),out);
		userFileSystem.userClose(out);
	}
	else
	{
		fprintf(out,(char*)"%s",buffer);
		fclose(out);
	}
	FreeUserCache();
	return NOPROBLEM_BIT;
}

char* EatFact(char* ptr,unsigned int flags,bool attribute)
{
	char word[MAX_WORD_SIZE];
	char word1[MAX_WORD_SIZE];
	char word2[MAX_WORD_SIZE];
	ptr = SkipWhitespace(ptr); // could be user-formateed, dont trust
	FunctionResult result = NOPROBLEM_BIT;
	//   subject
	if (*ptr == '(') //   nested fact
	{
		ptr = EatFact(ptr+1); //   returns after the closing paren
		flags |= FACTSUBJECT;
		sprintf(word,(char*)"%d",currentFactIndex() ); //   created OR e found instead of created
	}
	else  ptr = ReadShortCommandArg(ptr,word,result,OUTPUT_FACTREAD); //   subject
	ptr = SkipWhitespace(ptr); // could be user-formateed, dont trust
	if (!ptr) return NULL;
	if (result & ENDCODES) return ptr;
	if (!*word) 
	{
		if (!ptr) return NULL;
		if (compiling) BADSCRIPT((char*)"FACT-1 Missing subject for fact create")
		char* end = strchr(ptr,')');
		return (end) ? (end + 2) : ptr; 
	}

	//verb
	if (*ptr == '(') //   nested fact
	{
		ptr = EatFact(ptr+1);
		flags |= FACTVERB;
		sprintf(word1,(char*)"%d",currentFactIndex() );
	}
	else  ptr = ReadShortCommandArg(ptr,word1,result,OUTPUT_FACTREAD); //verb
	if (!ptr) return NULL;
	ptr = SkipWhitespace(ptr); // could be user-formateed, dont trust
	if (result & ENDCODES) return ptr;
	if (!*word1) 
	{
		if (!ptr) return NULL;
		if (compiling) BADSCRIPT((char*)"FACT-2 Missing verb for fact create")
		char* end = strchr(ptr,')');
		return (end) ? (end + 2) : ptr; 
	}

	//   object
	if (*ptr == '(') //   nested fact
	{
		ptr = EatFact(ptr+1);
		flags |= FACTOBJECT;
		sprintf(word2,(char*)"%d",currentFactIndex() );
	}
	else  ptr = ReadShortCommandArg(ptr,word2,result,OUTPUT_FACTREAD); 
	ptr = SkipWhitespace(ptr); // could be user-formateed, dont trust
	if (result & ENDCODES) return ptr;
	if (!*word2) 
	{
		if (compiling) BADSCRIPT((char*)"FACT-3 Missing object for fact create - %s",readBuffer)
		if (!ptr) return NULL;
		char* end = strchr(ptr,')');
		return (end) ? (end + 2) : ptr; 
	}
	uint64 fullflags;
	bool bad = false;
	bool response = false;
	char strip[MAX_WORD_SIZE];
	char* at = ReadCompiledWord(ptr,strip);
	bool stripquote = false;
	if (!strnicmp(strip,(char*)"STRIP_QUOTE",11)) 
	{
		stripquote = true;
		ptr = at;
	}
	ptr = ReadFlags(ptr,fullflags,bad,response);
	flags |= (unsigned int) fullflags;
	//if (bad) 
	//	return (*ptr) ? (ptr + 2) : ptr; //   returns after the closing ) if there is one - creates no fact

	MEANING subject;
	if ( flags & FACTSUBJECT)
	{
		subject = atoi(word);
		if (!subject) subject = 1; // make a phony fact
	}
	else if (stripquote)
	{
		size_t len = strlen(word);
		if (*word == '"' && word[len-1] == '"') 
		{
			word[len-1] = 0;
			memmove(word,word+1,strlen(word));
		}
		subject =  MakeMeaning(StoreWord(word,AS_IS),0);
	}
	else subject =  MakeMeaning(StoreWord(word,AS_IS),0);
	MEANING verb;
	if ( flags & FACTVERB)
	{
		verb = atoi(word1);
		if (!verb) verb = 1; // make a phony fact
	}
	else if (stripquote)
	{
		size_t len = strlen(word1);
		if (*word1 == '"' && word1[len-1] == '"') 
		{
			word1[len-1] = 0;
			memmove(word1,word1+1,strlen(word1));
		}
		verb =  MakeMeaning(StoreWord(word1,AS_IS),0);
	}
	else verb =  MakeMeaning(StoreWord(word1,AS_IS),0);
	MEANING object;
	if ( flags & FACTOBJECT)
	{
		object = atoi(word2);
		if (!object) object = 1; // make a phony fact
	}
	else if (stripquote)
	{
		size_t len = strlen(word2);
		if (*word2 == '"' && word2[len-1] == '"') 
		{
			word2[len-1] = 0;
			memmove(word2,word2+1,strlen(word2));
		}
		object =  MakeMeaning(StoreWord(word2,AS_IS),0);
	}
	else object =  MakeMeaning(StoreWord(word2,AS_IS),0);

	if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACELOG,(char*)"%s %s %s %x) = ",word,word1,word2,flags);

	FACT* F = FindFact(subject,verb,object,flags);
	if (!attribute || (F && object == F->object)) {;}  // not making an attribute or already made
	else // remove any facts with same subject and verb, UNlESS part of some other fact
	{
		if (flags & FACTSUBJECT) F = GetSubjectNondeadHead(Index2Fact(subject));
		else F = GetSubjectNondeadHead(subject);
		while (F)
		{
			if (F->subject == subject && F->verb == verb) 
			{
				if (!(F->flags & FACTATTRIBUTE)) // this is a script failure. Should ONLY be an attribute
				{
					char word[MAX_WORD_SIZE];
					WriteFact(F,false,word,false,true);
					Log(STDTRACELOG,(char*)"Fact created is not an attribute. There already exists %s",word); 
					printf((char*)"Fact created is not an attribute. There already exists %s",word); 
					currentFact = F;
					return (*ptr) ? (ptr + 2) : ptr; 
				}
				KillFact(F); 
			}
			F = GetSubjectNondeadNext(F);
		}
	}

	F = CreateFact(subject,verb,object,flags);
	if (attribute) 	F->flags |= FACTATTRIBUTE;
	return (*ptr) ? (ptr + 2) : ptr; //   returns after the closing ) if there is one
}

bool ImportFacts(char* buffer,char* name, char* set, char* erase, char* transient)
{ // if no set is given, return the subject of the 1st fact we read (Json entity name)
	int store = -1;
	if (*set && *set != '"') // able to pass empty string
	{
		store = GetSetID(set);
		if (store == ILLEGAL_FACTSET) return false;
		SET_FACTSET_COUNT(store,0);
	}
	if ( *name == '"')
	{
		++name;
		size_t len = strlen(name);
		if (name[len-1] == '"') name[len-1] = 0;
	}
	FILE* in;
	if (strstr(name,"ltm")) in = userFileSystem.userOpen(name);
	else in = in = FopenReadWritten(name);
	if (!in) return false;

	ChangeDepth(1, (char*)"ImportFacts");
	char* filebuffer = GetFreeCache();
	size_t readit;
	if (strstr(name,"ltm"))
	{
		readit = DecryptableFileRead(filebuffer,1,userCacheSize,in);	// LTM file read, read it all in, including BOM
		userFileSystem.userClose(in);
	}
	else
	{
		readit = fread(filebuffer,1,userCacheSize,in);	
		fclose(in);
	}
	filebuffer[readit] = 0;
	filebuffer[readit+1] = 0; // insure fully closed off

	// set bom
	maxFileLine = currentFileLine = 0;
	BOM = BOMSET; // will be utf8
	userRecordSourceBuffer = filebuffer;

	unsigned int flags = 0;
	if (!stricmp(erase,(char*)"transient") || !stricmp(transient,(char*)"transient")) flags |= FACTTRANSIENT; // make facts transient
	while (ReadALine(readBuffer, 0) >= 0)
    {
        if (*readBuffer == 0 || *readBuffer == '#') continue; //   empty or comment
		char word[MAX_WORD_SIZE];
		ReadCompiledWord(readBuffer,word);
		if (!stricmp(word,(char*)"null")) 
		{
			if (store >= 0) AddFact(store,NULL);
		}
		else if (*word == '(')
		{
			ChangeDepth(1, readBuffer);
			char* ptr = readBuffer;
			FACT* G = ReadFact(ptr,0);
			ChangeDepth(-1, readBuffer);
			if (!G) 
			{
				ReportBug("Import data: %s",readBuffer);
				continue;
			}
			G->flags |= flags;
			if (store >= 0) AddFact(store,G);
			else if (!*buffer) strcpy(buffer,Meaning2Word(G->subject)->word); // to return the name
		}
	}
	FreeUserCache();
	userRecordSourceBuffer = NULL;

	if (!stricmp(erase,(char*)"erase") || !stricmp(transient,(char*)"erase")) remove(name); // erase file after reading
	if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACELOG,(char*)"[%d] => ",FACTSET_COUNT(store));
	currentFact = NULL; // we have used up the facts
	ChangeDepth(-1, (char*)"ImportFacts");
	return true;
}

void WriteFacts(FILE* out,FACT* F, int flags) //   write out from here to end
{ 
	char word[MAX_WORD_SIZE];
	if (!out) return;
    while (++F <= factFree) 
	{
		if (!(F->flags & (FACTTRANSIENT|FACTDEAD))) 
		{
			F->flags |= flags; // used to pass along build2 flag
			fprintf(out,(char*)"%s",WriteFact(F,true,word,false,true));
			F->flags ^= flags;
		}
	}
    FClose(out);
}

void WriteBinaryFacts(FILE* out,FACT* F) //   write out from here to end
{ 
	if (!out) return;
    while (++F <= factFree) 
	{
		unsigned int index = Fact2Index(F);
		if (F->flags & FACTSUBJECT && F->subject >= index) ReportBug((char*)"subject fact index too high")
		if (F->flags & FACTVERB && F->verb >= index) ReportBug((char*)"verb fact index too high")
		if (F->flags & FACTOBJECT && F->object >= index) ReportBug((char*)"object fact index too high")
		Write32(F->subject,out);
		Write32(F->verb,out);
		Write32(F->object,out);
		Write32(F->flags,out);
	}
    FClose(out);
}
	
FACT* CreateFastFact(FACTOID_OR_MEANING subject, FACTOID_OR_MEANING verb, FACTOID_OR_MEANING object, unsigned int properties)
{
	//   get correct field values
	WORDP s = (properties & FACTSUBJECT) ? NULL : Meaning2Word(subject);
	WORDP v = (properties & FACTVERB) ? NULL : Meaning2Word(verb);
	WORDP o = (properties & FACTOBJECT) ? NULL : Meaning2Word(object);
	if (properties & FACTDEAD) // except for special internal system facts, this shouldnt happen - unless user intends it
	{
		int xx = 0;
	}
	// DICTIONARY should never be build with any but simple meanings and Mis
	// No fact meaning should ever have a synset marker on it. And member facts may have type restrictions on them
	if (s && ((subject & (-1 ^ SIMPLEMEANING) && verb == Mis) || subject&SYNSET_MARKER))
	{
		int xx = 0;
	}
	if (o && ((object & (-1 ^ SIMPLEMEANING) && verb == Mis)|| subject&SYNSET_MARKER))
	{
		int xx = 0;
	}

	//   allocate a fact
	FACT* F;
	if (++factFree == factEnd) 
	{
		--factFree;
		ReportBug((char*)"out of fact space at %d",Fact2Index(factFree))
		printf((char*)"%s",(char*)"out of fact space");
		return NULL;
	}
	currentFact = factFree;
	int index = Fact2Index(currentFact); // for debugging
	if (index == 416)
	{
		int xx = 0;
	}

	//   init the basics
	memset(currentFact,0,sizeof(FACT));
	currentFact->subject = subject;
	currentFact->verb = verb; 
	currentFact->object = object;
	currentFact->flags = properties;

	//   crossreference
	if (s) 
	{
		SetSubjectNext(currentFact,GetSubjectHead(subject)); // dont use nondead
		SetSubjectHead(subject,currentFact);
	}
	else 
	{
		F = Index2Fact(currentFact->subject);
		if (F)
		{	
			SetSubjectNext(currentFact,GetSubjectHead(F));   // dont use nondead
			SetSubjectHead(F,currentFact);
		}
		else
		{
			--factFree;
			return NULL;
		}
	}
	if (v) 
	{
		SetVerbNext(currentFact, GetVerbHead(verb));  // dont use nondead
		SetVerbHead(verb,currentFact);
	}
	else 
	{
		F = Index2Fact(currentFact->verb);
		if (F)
		{
			SetVerbNext(currentFact,GetVerbHead(F));  // dont use nondead
			SetVerbHead(F,currentFact);
		}
		else
		{
			--factFree;
			return NULL;
		}
	}
	if (o) 
	{
		SetObjectNext(currentFact, GetObjectHead(object));  // dont use nondead
		SetObjectHead(object,currentFact);
	}
	else
	{
		F = Index2Fact(currentFact->object);
		if (F)
		{
			SetObjectNext(currentFact,GetObjectNext(F));   // dont use nondead
			SetObjectHead(F,currentFact);
		}
		else
		{
			--factFree;
			return NULL;
		}
	}

	if (planning) currentFact->flags |= FACTTRANSIENT;

	if (trace & TRACE_FACT && CheckTopicTrace())
	{
		char* buffer = AllocateInverseString(NULL,MAX_BUFFER_SIZE); // fact might be big, cant use mere WORD_SIZE
		buffer = WriteFact(currentFact,false,buffer,true,false);
		Log(STDTRACELOG,(char*)"create %s",buffer);
		ReleaseInverseString(buffer);
	}	
	return currentFact;
}

bool ReadBinaryFacts(FILE* in) //   read binary facts
{ 
	if (!in) return false;
	while (ALWAYS)
	{
		MEANING subject = Read32(in);
		if (!subject) break;
 		MEANING verb = Read32(in);
 		MEANING object = Read32(in);
  		unsigned int properties = Read32(in);
		CreateFastFact(subject,verb,object,properties);
	}
    FClose(in);
	return true;
}

static char* WriteField(MEANING T, uint64 flags,char* buffer,bool ignoreDead)
{
	char* start = buffer;
	// a field is either a contiguous mass of non-blank tokens, or a user string "xxx" or an internal string `xxx`  (internal removes its ends, user doesnt)
    if (flags ) //   fact reference
    {
		FACT* G = Index2Fact(T);
		if (!*WriteFact(G,false,buffer,ignoreDead)) 
		{
			*buffer = 0;
			return buffer;
		}
		buffer += strlen(buffer);
    }
	else if (!T) 
	{
		ReportBug((char*)"Missing fact field")
		*buffer++ = '?';
	}
    else 
	{
		WORDP D = Meaning2Word(T);
		if (D->internalBits & (INTERNAL_MARK|DELETED_MARK) && !ignoreDead) // a deleted field
		{
			*buffer = 0;
			return buffer; //   cancels print
		}
		char* answer = WriteMeaning(T,true);

		bool quoted = false;
		if (*answer == '"') // if it starts with ", does it end with "
		{
			size_t len = strlen(answer);
			if (answer[len-1] == '"') quoted = true;
		}
		bool embeddedbacktick = strchr(answer,'`') ? true : false;
		bool embeddedspace = !quoted && (strchr(answer,' ')  || strchr(answer,'(') || strchr(answer,')')); // does this need protection? blanks or function call maybe
		bool safe = true; 
		if (strchr(answer,'\n') || strchr(answer,'\r') || strchr(answer,'\t') || strchr(answer,'\\') || strchr(answer,'/')) safe = false;
		// json must protect: " \ /  nl cr tab  we are not currently protecting bs ff
		if (embeddedbacktick || !safe) // uses our own marker and can escape data
		{
			if (embeddedbacktick) strcpy(buffer,(char*)"`*^"); 
			else strcpy(buffer,(char*)"`"); // has blanks or paren or just starts with ", use internal string notation
			buffer += strlen(buffer);
			AddEscapes(buffer,answer,false); // facts are not normal
			buffer += strlen(buffer);
			if (embeddedbacktick) strcpy(buffer,(char*)"`*^"); // add closer marker
			else strcpy(buffer,(char*)"`");
		}
		else if (embeddedspace) sprintf(buffer,(char*)"`%s`",answer); // has blanks, use internal string notation
		else  strcpy(buffer,answer); // use normal notation
		buffer += strlen(buffer);
	}
	*buffer++ = ' ';
	*buffer = 0;
	return buffer;
}

char* WriteFact(FACT* F,bool comments,char* buffer,bool ignoreDead,bool eol)
{ //   if fact is junk, return the null string
	char* start = buffer;
	*buffer = 0;
	if (!F || !F->subject) return start; // never write special facts out
	int index = Fact2Index(F); // for debugging
	if (F->flags & FACTDEAD) // except for user display THIS shouldnt happen to real fact writes
	{
		if (ignoreDead)
		{
			strcpy(buffer,(char*)"DEAD ");
			buffer += strlen(buffer);
		}
		else 
			return ""; // illegal - only happens with facts (nondead) that refer to dead facts?
	}

	//   fact opener
	*buffer++ = '(';
	*buffer++ = ' ';

	//   do subject
	char* base = buffer;
 	buffer = WriteField(F->subject,F->flags & FACTSUBJECT,buffer,ignoreDead);
	if (base == buffer ) 
	{
		*start = 0;
		return start; //    word itself was removed from dictionary
	}
	base = buffer;
	buffer = WriteField(F->verb,F->flags & FACTVERB,buffer,ignoreDead);
	if (base == buffer ) 
	{
		*start = 0;
		return start; //    word itself was removed from dictionary
	}

	base = buffer;
	buffer = WriteField(F->object,F->flags & FACTOBJECT,buffer,ignoreDead);
	if (base == buffer ) 
	{
		*start = 0;
		return start; //    word itself was removed from dictionary
	}

	//   add properties
    if (F->flags)  
	{
		sprintf(buffer,(char*)"x%x ",F->flags & (-1 ^ (MARKED_FACT|MARKED_FACT2) ));  // dont show markers
		buffer += strlen(buffer);
	}

	//   close fact
	*buffer++ = ')';
	*buffer = 0;
	if (eol) strcat(buffer,(char*)"\r\n");
	return start;
}

char* ReadField(char* ptr,char* field,char fieldkind, unsigned int& flags)
{
	if (*ptr == '(')
	{
		FACT* G = ReadFact(ptr,(flags & FACTBUILD2) ? BUILD2 : 0);
		if (fieldkind == 's') flags |= FACTSUBJECT;
		else if (fieldkind == 'v') flags |= FACTVERB;
		else if (fieldkind == 'o') flags |= FACTOBJECT;
		if (!G)
		{
			ReportBug((char*)"Missing fact field")
			return NULL;
		}
		sprintf(field,(char*)"%d",Fact2Index(G)); 
	}
	else if (*ptr == ENDUNIT) // internal string token (fact read)
	{
		char* end;
		char* start;
		if (ptr[1] == '*' && ptr[2] == '^') // they use our ` in their field - we use `*^%s`*^
		{
			start = end = ptr+3;
			while ((end = strchr(end,'^')))
			{
				if (*(end-1) == '*' && *(end-2) == '`') break;  // found our close
				++end;
			}
			if (!end)
			{
				ReportBug("No end found");
				return NULL;
			}
			*(end-2) = 0; // terminate string with null
		}
		else
		{
			end = strchr(ptr+1,ENDUNIT); // find corresponding end
			start = ptr + 1;
			if (!*end)
			{
				ReportBug("No end found");
				return NULL;
			}
			*end = 0;
		}
		
		CopyRemoveEscapes(field,start,90000);	// we only remove ones we added
		return end+2; // point AFTER the space after the closer
	}
    else  ptr = ReadCompiledWord(ptr,field); // no escaping or anything weird needed
	if (field[0] == '~') MakeLowerCase(field);	// all concepts/topics are lower case
	return ptr; //   return at new token
}

FACT* ReadFact(char* &ptr, unsigned int build)
{
	char word[MAX_WORD_SIZE];
    MEANING subject = 0;
	MEANING verb = 0;
    MEANING object = 0;
 	//   fact may start indented.  Will start with (or be 0 for a null fact
    ptr = ReadCompiledWord(ptr,word);
    if (*word == '0') return 0; 
	unsigned int flags = 0;
	if (build == BUILD2) flags |= FACTBUILD2;
	else if (build == BUILD1) flags |= FACTBUILD1;
	char subjectname[MAX_WORD_SIZE];
	ptr = ReadField(ptr,subjectname,'s',flags);
    char verbname[MAX_WORD_SIZE];
	ptr = ReadField(ptr,verbname,'v',flags);
    char objectname[MAX_WORD_SIZE];
	ptr = ReadField(ptr,objectname,'o',flags);

	if (!ptr) return NULL;
	
    //   handle the flags on the fact
    uint64 properties = 0;
	bool bad = false;
	bool response = false;
    if (!*ptr || *ptr == ')' ); // end of fact
	else ptr = ReadFlags(ptr,properties,bad,response);
	flags |= (unsigned int) properties;

    if (flags & FACTSUBJECT) subject = (MEANING) atoi(subjectname);
    else  subject = ReadMeaning(subjectname,true,true);
	if (flags & FACTVERB) verb = (MEANING) atoi(verbname);
	else  verb = ReadMeaning(verbname,true,true);
	if (flags & FACTOBJECT) object = (MEANING) atoi(objectname);
	else  object = ReadMeaning(objectname,true,true);

    FACT* F = FindFact(subject, verb,object,flags);
    if (!F)   F = CreateFact(subject,verb,object,flags); 
	if (*ptr == ')') ++ptr;	// skip over ending )
	ptr = SkipWhitespace(ptr);
    return F;
}

void ReadFacts(const char* name,const char* layer,unsigned int build,bool user) //   a facts file may have dictionary augmentations and variable also
{
  	char word[MAX_WORD_SIZE];
	if (layer) sprintf(word,"TOPIC/%s",name);
	else strcpy(word,name);
    FILE* in = (user) ? FopenReadWritten(word) : FopenReadOnly(word); //  TOPIC fact/DICT files
	if (!in && layer) 
	{
		sprintf(word,"TOPIC/BUILD%s/%s",layer,name);
		in = FopenReadOnly(word);
	}
	if (!in) return;
	StartFile(name);
    while (ReadALine(readBuffer, in) >= 0)
    {
		char* ptr = ReadCompiledWord(readBuffer,word);
		if (*word == 0 || (*word == '#' && word[1] != '#')); //   empty or comment
		else if (*word == '+') //   dictionary entry
		{
			if (!stricmp(word,(char*)"+query")) // defining a private query
			{
				ptr = ReadCompiledWord(ptr,word); // name
				WORDP D = StoreWord(word);
				AddInternalFlag(D,(unsigned int)(QUERY_KIND|build));
				ReadCompiledWord(ptr,word);
				ptr = strchr(word+1,'"');
				*ptr = 0;
				D->w.userValue = AllocateString(word+1);
				continue;
			}

			char word[MAX_WORD_SIZE];
			char* at = ReadCompiledWord(readBuffer+2,word); //   start at valid token past space
			WORDP D = FindWord(word,0,PRIMARY_CASE_ALLOWED);
			if (!D)
			{
				D = StoreWord(word);
				AddInternalFlag(D,(unsigned int)build);
			}
			bool sign = false;
			if (*at == '-' && IsDigit(at[1])) 
			{
				sign = true;
				++at;
			}
			if (IsDigit(*at)) // # has numeric value, is a rename number
			{
				int64 n;
				ReadInt64(at,n);
				AddProperty(D,(uint64) n);// rename value
				AddInternalFlag(D,RENAMED);
				if (sign) AddSystemFlag(D,CONSTANT_IS_NEGATIVE);
			}
			else ReadDictionaryFlags(D,at);
		}
		else if (*word == USERVAR_PREFIX) // variable
		{
			char* eq = strchr(word,'=');
			if (!eq) ReportBug((char*)"Bad fact file user var assignment %s",word)
			else 
			{
				*eq = 0;
				SetUserVariable(word,eq+1);
			}
		}
        else 
		{
			char* ptr = readBuffer;
			ReadFact(ptr,build); // will write on top of ptr... must not be readBuffer variable
		}
    }
   FClose(in);
}

void SortFacts(char* set, int alpha, int setpass) //   sort low to high ^sort(@1subject) which field we sort on (subject or verb or object)
{
    int n = (setpass >= 0) ? setpass : GetSetID(set);
	if (n == ILLEGAL_FACTSET) return;
	char kind = GetLowercaseData(*GetSetType(set));
	if (!kind) kind = 's';
    int i;
	int size = FACTSET_COUNT(n);
	float floatValues[MAX_FIND+1];
	char* wordValues[MAX_FIND+1];

	// load the actual array to compare
    for (i = 1; i <= size; ++i)
    {
        FACT* F =  factSet[n][i];
		char* word;
		if (kind == 's') word = Meaning2Word(F->subject)->word; 
		else if (kind == 'v') word = Meaning2Word(F->verb)->word; 
		else word = Meaning2Word(F->object)->word;
		if (alpha == 1) wordValues[i] = word;
		else if (alpha == 0) floatValues[i] = (float) atof(word);
    }

	// do bubble sort 
    bool swap = true;
    while (swap)
    {
        swap = false;
        for (i = 1; i <= size-1; ++i) 
        {
			char* word = wordValues[i];
            float tmp = floatValues[i];
			if (alpha == 1 && strcmp(word,wordValues[i+1]) > 0) {;}
			else if (alpha == 2 && (char*)factSet[n] > (char*)factSet[n+1]) {;}
			else if (alpha == 0 && tmp > floatValues[i+1]) {;}
			else continue;
	        wordValues[i] = wordValues[i+1];
            wordValues[i+1] = word;
            floatValues[i] = floatValues[i+1];
            floatValues[i+1] = tmp;
            FACT* F = factSet[n][i];
			factSet[n][i] = factSet[n][i+1];
			factSet[n][i+1] = F;
			swap = true;
		}
        --size;
    }
}
