#include "common.h"

int impliedIf = ALREADY_HANDLED;	// testing context of an if
unsigned int withinLoop = 0;

static void TestIf(char* ptr,FunctionResult& result)
{ //   word is a stream terminated by )
	//   if (%rand == 5 ) example of a comparison
	// if (@14) {} nonempty factset
	//   if (^QuerySubject()) example of a call or if (!QuerySubject())
	//   if ($var) example of existence
	//   if ('_3 == 5)  quoted matchvar
	//   if (1) what an else does
	char* word1 = AllocateInverseString(NULL,MAX_BUFFER_SIZE);
	char* word2 = AllocateInverseString(NULL,MAX_BUFFER_SIZE);
	char op[MAX_WORD_SIZE];
	int id;
	impliedIf = 1;
	if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACETABLOG,(char*)"If ");
resume:
	ptr = ReadCompiledWord(ptr,word1);	//   the var or whatever
	bool invert = false;
	if (*word1 == '!') //   inverting, go get the actual
	{
		invert = true;
		// get the real token
		if (word1[1]) memmove(word1,word1+1,strlen(word1)); 
		else ptr = ReadCompiledWord(ptr,word1);	
	}
	ptr = ReadCompiledWord(ptr,op);		//   the test if any, or (for call or ) for closure or AND or OR
	bool call = false;
	if (*op == '(') // function call instead of a variable. but the function call might output a value which if not used, must be considered for failure
	{
		call = true;
		ptr -= strlen(word1) + 3; //   back up so output can see the fn name and space
		ptr = FreshOutput(ptr,word2,result,OUTPUT_ONCE|OUTPUT_KEEPSET); // word2 hold output value // skip past closing paren
		if (result & SUCCESSCODES) result = NOPROBLEM_BIT;	// legal way to terminate the piece with success at any  level
		if (trace & TRACE_OUTPUT && CheckTopicTrace()) 
		{
			if (result & ENDCODES) id = Log(STDTRACETABLOG,(char*)"%c%s ",(invert) ? '!' : ' ',word1);
			else if (*word1 == '1' && word1[1] == 0) id = Log(STDTRACETABLOG,(char*)"else ");
			else id = Log(STDTRACETABLOG,(char*)"%c%s ",(invert) ? '!' : ' ',word1);
		}
		ptr = ReadCompiledWord(ptr,op); // find out what happens next after function call
		if (!result && IsComparison(*op)) // didnt fail and followed by a relationship op, move output as though it was the variable
		{
			 strcpy(word1,word2); 
			 call = false;	// proceed normally
		}
		else if (result != NOPROBLEM_BIT) {;} // failed
		else if (!stricmp(word2,(char*)"0") || !stricmp(word2,(char*)"false")) result = FAILRULE_BIT;	// treat 0 and false as failure along with actual failures
	}
		
	if (call){;} // call happened
	else if (IsComparison(*op)) //   a comparison test
	{
		char word1val[MAX_WORD_SIZE];
		char word2val[MAX_WORD_SIZE];
		ptr = ReadCompiledWord(ptr,word2);	
		result = HandleRelation(word1,op,word2,true,id,word1val,word2val);
		ptr = ReadCompiledWord(ptr,op);	//   AND, OR, or ) 
	}
	else //   existence of non-failure or any content
	{
		if (*word1 == SYSVAR_PREFIX || *word1 == '_' || *word1 == USERVAR_PREFIX || *word1 == '@' || *word1 == '?' || *word1 == '^')
		{
			char remap[MAX_WORD_SIZE];
			strcpy(remap,word1); // for tracing
			if (*word1 == '^' && IsDigit(word1[1])) strcpy(word1,callArgumentList[atoi(word1+1)+fnVarBase]);  // simple function var, remap it
			char* found;
			if (word1[0] == LCLVARDATA_PREFIX && word1[1] == LCLVARDATA_PREFIX) 
				found = word1 + 2;	// preevaled function variable
			else if (*word1 == SYSVAR_PREFIX) found = SystemVariable(word1,NULL);
			else if (*word1 == '_') found = wildcardCanonicalText[GetWildcardID(word1)];
			else if (*word1 == USERVAR_PREFIX) found = GetUserVariable(word1);
			else if (*word1 == '?') found = (tokenFlags & QUESTIONMARK) ? (char*) "1" : (char*) "";
			else if (*word1 == '^' && word1[1] == USERVAR_PREFIX) // indirect var
			{
				found = GetUserVariable(word1+1);
				found = GetUserVariable(found,true);
			}
			else if (*word1 == '^' && word1[1] == '^' && IsDigit(word1[2])) found = ""; // indirect function var 
			else if (*word1 == '^' && word1[1] == '_') found = ""; // indirect var
			else if (*word1 == '^' && word1[1] == '\'' && word1[2] == '_') found = ""; // indirect var
			else if (*word1 == '@') found =  FACTSET_COUNT(GetSetID(word1)) ? (char*) "1" : (char*) "";
			else found = word1;

			if (trace & TRACE_OUTPUT && CheckTopicTrace()) 
			{
				char label[MAX_WORD_SIZE];
				strcpy(label,word1);
				if (*remap == '^') sprintf(label,"%s->%s",remap,word1);
				if (!*found) 
				{
					if (invert) id = Log(STDTRACELOG,(char*)"!%s (null) ",label);
					else id = Log(STDTRACELOG,(char*)"%s (null) ",label);
				}
				else 
				{
					if (invert) id = Log(STDTRACELOG,(char*)"!%s (%s) ",label,found);
					else id = Log(STDTRACELOG,(char*)"%s (%s) ",label,found);
				}
			}
			if (!*found) result = FAILRULE_BIT;
			else result = NOPROBLEM_BIT;
		}
		else  //  its a constant of some kind 
		{
			if (trace & TRACE_OUTPUT && CheckTopicTrace()) 
			{
				if (result & ENDCODES) id = Log(STDTRACELOG,(char*)"%c%s ",(invert) ? '!' : ' ',word1);
				else if (*word1 == '1' && word1[1] == 0) id = Log(STDTRACELOG,(char*)"else ");
				else id = Log(STDTRACELOG,(char*)"%c%s ",(invert) ? '!' : ' ',word1);
			}
			ptr -= strlen(word1) + 3; //   back up to process the word and space
			ptr = FreshOutput(ptr,word2,result,OUTPUT_ONCE|OUTPUT_KEEPSET) + 2; //   returns on the closer and we skip to accel
		}
	}
	if (invert) result = (result & ENDCODES) ? NOPROBLEM_BIT : FAILRULE_BIT; 
	
	if (*op == 'a') //   AND - compiler validated it
	{
		if (!(result & ENDCODES)) 
		{
			if (trace & TRACE_OUTPUT && CheckTopicTrace()) id = Log(STDTRACELOG,(char*)" AND ");
			goto resume;
			//   If he fails (result is one of ENDCODES), we fail
		}
		else 
		{
			if (trace & TRACE_OUTPUT && CheckTopicTrace()) id = Log(STDTRACELOG,(char*)" ... ");
		}
	}
	else if (*op == 'o') //  OR
	{
		if (!(result & ENDCODES)) 
		{
			if (trace & TRACE_OUTPUT && CheckTopicTrace()) id = Log(STDTRACELOG,(char*)" ... ");
			result = NOPROBLEM_BIT;
		}
		else 
		{
			if (trace & TRACE_OUTPUT && CheckTopicTrace()) id = Log(STDTRACELOG,(char*)" OR ");
			
			goto resume;
		}
	}

	ReleaseInverseString(word1);
	impliedIf = ALREADY_HANDLED;
}

char* HandleIf(char* ptr, char* buffer,FunctionResult& result)
{
	// If looks like this: ^^if ( _0 == null ) 00m{ this is it } 00G else ( 1 ) 00q { this is not it } 004 
	// a nested if would be ^^if ( _0 == null ) 00m{ ^^if ( _0 == null ) 00m{ this is it } 00G else ( 1 ) 00q { this is not it } 004 } 00G else ( 1 ) 00q { this is not it } 004
	// after a test condition is code to jump to next test branch when condition fails
	// after { }  chosen branch is offset to jump to end of if
	ChangeDepth(1,"if");
	bool executed = false;
	while (ALWAYS) //   do test conditions until match
	{
		char* endptr;

		if (*ptr == '(') // old format - std if internals
		{
			endptr = strchr(ptr,'{') - 3; // offset to jump past pattern
		}
		else // new format, can use std if or pattern match
		{
			endptr = ptr + Decode(ptr);
			ptr += 3; // skip jump to end of pattern and point to pattern
		}

		//   Perform TEST condition
		if (!strnicmp(ptr,(char*)"( pattern ",10)) // pattern if
		{
			int start = 0;
			int end = 0;
			unsigned int gap = 0;
			wildcardIndex = 0;
			bool uppercasem = false;
			int whenmatched = 0;
			bool failed = false;
			if (!Match(ptr+10,0,start,(char*)"(",1,0,start,end,uppercasem,whenmatched,0,0)) failed = true;  // skip paren and blank, returns start as the location for retry if appropriate
			if (clearUnmarks) // remove transient global disables.
			{
				clearUnmarks = false;
				for (int i = 1; i <= wordCount; ++i) unmarked[i] = 1;
			}
			ShowMatchResult((failed) ? FAILRULE_BIT : NOPROBLEM_BIT, ptr+10,NULL);

			if (!failed) 
			{
				if (trace & (TRACE_PATTERN|TRACE_MATCH|TRACE_SAMPLE)  && CheckTopicTrace() ) //   display the entire matching responder and maybe wildcard bindings
				{
					Log(STDTRACETABLOG,(char*)"  **  Match: ");
					if (wildcardIndex)
					{
						Log(STDTRACETABLOG,(char*)"  Wildcards: (");
						for (int i = 0; i < wildcardIndex; ++i)
						{
							if (*wildcardOriginalText[i]) Log(STDTRACELOG,(char*)"_%d=%s / %s (%d-%d)   ",i,wildcardOriginalText[i],wildcardCanonicalText[i],wildcardPosition[i] & 0x0000ffff,wildcardPosition[i]>>16);
							else Log(STDTRACELOG,(char*)"_%d=  ",i);
						}
					}
					Log(STDTRACELOG,(char*)"\r\n");
				}
			}
			result = (failed) ? FAILRULE_BIT : NOPROBLEM_BIT;
		}
		else TestIf(ptr+2,result); 
		if (trace & TRACE_OUTPUT  && CheckTopicTrace()) 
		{
			if (result & ENDCODES) Log(STDTRACELOG,(char*)"%s\r\n", "FAIL-if");
			else Log(STDTRACELOG,(char*)"%s\r\n", "PASS-if");
		}

		ptr = endptr; // now after pattern, pointing to the skip data to go past body.
						
		//   perform SUCCESS branch and then end if
		if (!(result & ENDCODES)) //   IF test success - we choose this branch
		{
			executed = true;
			ptr = Output(ptr+5,buffer,result); //   skip accelerator-3 and space and { and space - returns on next useful token
			ptr += Decode(ptr);	//   offset to end of if entirely
			if (*(ptr-1) == 0)  --ptr;// no space after?
			break;
		}
		else 
			result = NOPROBLEM_BIT;		//   test result is not the IF result
	
		//   On fail, move to next test
		ptr +=  Decode(ptr);
		if (*(ptr-1) == 0) 
		{
			--ptr;
			break; // the if ended abruptly as a stream like in NOTRACE, and there is no space here
		}
		if (strncmp(ptr,(char*)"else ",5))  break; //   not an ELSE, the IF is over. 
		ptr += 5; //   skip over ELSE space, aiming at the ( of the next condition condition
	}
	if (executed && trace & TRACE_OUTPUT  && CheckTopicTrace()) Log(STDTRACETABLOG,"End If\r\n");

	ChangeDepth(-1,"if");
	
	return ptr;
} 

char* HandleLoop(char* ptr, char* buffer, FunctionResult &result)
{
	unsigned int oldIterator = currentIterator;
	currentIterator = 0;
	char* buffer1 = AllocateInverseString(NULL,MAX_BUFFER_SIZE);
	ptr = ReadCommandArg(ptr+2,buffer1,result)+2; //   get the loop counter value and skip closing ) space 
	char* endofloop = ptr + (size_t) Decode(ptr);
	int counter;
	if (*buffer1 == '@')
	{
		int set = GetSetID(buffer1);
		if (set < 0) 
		{
			result = FAILRULE_BIT; // illegal id
			ReleaseInverseString(buffer1);
			return ptr;
		}
		counter = FACTSET_COUNT(set);
	}
	else counter = atoi(buffer1);
	ReleaseInverseString(buffer1);
	if (result & ENDCODES) return endofloop;

	++withinLoop;

	ptr += 5;	//   skip jump + space + { + space
	char* value = GetUserVariable((char*)"$cs_looplimit");
	int limit = atoi(value);
	if (limit == 0) limit = 1000;

	bool infinite = false;
	if (counter > limit || counter < 0) 
	{
		counter = limit; //   LIMITED
		infinite = true; // loop is bounded by defaults
	}
	ChangeDepth(1,(char*)"HandleLoop");
	while (counter-- > 0)
	{
		if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACETABLOG,(char*)"loop(%d)\r\n",counter+1);
		FunctionResult result1;
		Output(ptr,buffer,result1,OUTPUT_LOOP);
		buffer += strlen(buffer);
		if (result1 & ENDCODES) 
		{
			if (result1 == NEXTLOOP_BIT) continue;
			result = (FunctionResult)( (result1 & (ENDRULE_BIT | FAILRULE_BIT)) ? NOPROBLEM_BIT : (result1 & ENDCODES) );  // rule level terminates only the loop
			if (result == FAILLOOP_BIT) result = FAILRULE_BIT; // propagate failure outside of loop
			if (result == ENDLOOP_BIT) result = NOPROBLEM_BIT; // propagate ok outside of loop
			break;//   potential failure if didnt add anything to buffer
		}
	}
	if (trace & TRACE_OUTPUT && CheckTopicTrace()) Log(STDTRACETABLOG,(char*)"end of loop\r\n");
	if (counter < 0 && infinite) ReportBug("Loop ran to limit");
	ChangeDepth(-1,(char*)"HandleLoop");
	--withinLoop;

	currentIterator = oldIterator;
	return endofloop;
}  

FunctionResult HandleRelation(char* word1,char* op, char* word2,bool output,int& id, char* word1val, char* word2val)
{ //   word1 and word2 are RAW, ready to be evaluated.
	*word1val = 0;
	*word2val = 0;
	char* val1 = AllocateInverseString(NULL,MAX_BUFFER_SIZE);
	char* val2 = AllocateInverseString(NULL,MAX_BUFFER_SIZE);
	WORDP D;
	WORDP D1;
	WORDP D2;
	int64 v1 = 0;
	int64 v2 = 0;
	FunctionResult result,val1Result;
	FreshOutput(word1,val1,result,OUTPUT_ONCE|OUTPUT_KEEPSET|OUTPUT_NOCOMMANUMBER); // 1st arg
	val1Result = result;
	if (word2 && *word2) FreshOutput(word2,val2,result,OUTPUT_ONCE|OUTPUT_KEEPSET|OUTPUT_NOCOMMANUMBER); // 2nd arg
	result = FAILRULE_BIT;
	if (!stricmp(val1,(char*)"null") ) *val1 = 0;
	if (!stricmp(val2,(char*)"null") ) *val2 = 0;
	if (!op)
	{
		if (val1Result & ENDCODES); //   explicitly failed
		else if (*val1) result = NOPROBLEM_BIT;	//   has some value
	}
	else if (*op == '?' || op[1] == '?') // ? and !? 
	{
		if (*word1 == '\'') ++word1; // ignore the quote since we are doing positional
		if (*word1 == '_') // try for precomputed match from memorization
		{
			unsigned int index = GetWildcardID(word1);
			index = WILDCARD_START(wildcardPosition[index]);
			D = FindWord(val2);
			if (index && D)
			{
				int junk;
				if (GetNextSpot(D,index-1,junk,junk) == index) result = NOPROBLEM_BIT; // otherwise failed and we would have known
			}
			char* was = wildcardOriginalText[GetWildcardID(word1)];
			if (result == FAILRULE_BIT && (!index || strchr(was,'_') || strchr(was,' '))) // laborious match try now
			{
				D1 = FindWord(val1);
				if (D1 && D)
				{
					NextInferMark();
					if (SetContains(MakeMeaning(D),MakeMeaning(D1))) result = NOPROBLEM_BIT;
				}
			}
			if (*op == '!') result = (result != NOPROBLEM_BIT) ? NOPROBLEM_BIT : FAILRULE_BIT;
		}
		else // laborious.
		{
			D1 = FindWord(val1);
			D2 = FindWord(val2);
			if (D1 && D2)
			{
				NextInferMark();
				if (SetContains(MakeMeaning(D2),MakeMeaning(D1))) result = NOPROBLEM_BIT;
			}
			if (result != NOPROBLEM_BIT)
			{
				char* verb = GetInfinitive(val1,true);
				if (verb && stricmp(verb,val1))
				{
					D1 = FindWord(verb);
					if (D1 && D2)
					{
						NextInferMark();
						if (SetContains(MakeMeaning(D2),MakeMeaning(D1))) result = NOPROBLEM_BIT;
					}
				}
			}
			if (result != NOPROBLEM_BIT)
			{
				char* noun = GetSingularNoun(val1,true,true);
				if (noun && stricmp(noun,val1))
				{
					D1 = FindWord(noun);
					if (D1 && D2)
					{
						NextInferMark();
						if (SetContains(MakeMeaning(D2),MakeMeaning(D1))) result = NOPROBLEM_BIT;
					}
				}
			}
			if (*op == '!') result = (result != NOPROBLEM_BIT) ? NOPROBLEM_BIT : FAILRULE_BIT;
		}
	}
	else //   boolean tests
	{
		// convert null to numeric operator for < or >  -- but not for equality
		if (!*val1 && IsDigit(*val2) && IsNumber(val2) && (*op == '<' || *op == '>')) strcpy(val1,(char*)"0");
		else if (!*val2 && IsDigit(*val1) && IsNumber(val1) && (*op == '<' || *op == '>')) strcpy(val2,(char*)"0");
		// treat #123 as string but +21545 and -12345 as numbers
		if (*val1 == '#' || !IsDigitWord(val1,true) || *val2 == '#' ||  !IsDigitWord(val2,true)) //   non-numeric string compare - bug, can be confused if digit starts text string
		{
			char* arg1 = val1;
			char* arg2 = val2;
			if (*arg1 == '"')
			{
				size_t len = strlen(arg1);
				if (arg1[len-1] == '"') // remove STRING markers
				{
					arg1[len-1] = 0;
					++arg1;
				}
			}
			if (*arg2 == '"')
			{
				size_t len = strlen(arg2);
				if (arg2[len-1] == '"') // remove STRING markers
				{
					arg2[len-1] = 0;
					++arg2;
				}
			}			
			if (*op == '!') result = (stricmp(arg1,arg2)) ? NOPROBLEM_BIT : FAILRULE_BIT;
			else if (*op == '=') result = (!stricmp(arg1,arg2)) ? NOPROBLEM_BIT: FAILRULE_BIT;
			else result = FAILRULE_BIT;
		}
		//   handle float ops
		else if ((strchr(val1,'.') && val1[1]) || (strchr(val2,'.') && val2[1])) // at least one arg is float
		{
			char* comma = 0; 
			while ((comma = strchr(val1,',')))  memmove(comma,comma+1,strlen(comma+1)); // remove embedded commas
			while ((comma = strchr(val2,',')))  memmove(comma,comma+1,strlen(comma+1)); // remove embedded commas
			float v1f = (float) atof(val1);
			float v2f = (float) atof(val2);
			if (*op == '=') result = (v1f == v2f) ? NOPROBLEM_BIT : FAILRULE_BIT;
			else if (*op == '<') 
			{
				if (!op[1]) result =  (v1f < v2f) ? NOPROBLEM_BIT : FAILRULE_BIT;
				else result =  (v1f <= v2f) ? NOPROBLEM_BIT : FAILRULE_BIT;
			}
			else if (*op == '>') 
			{
				if (!op[1]) result =  (v1f > v2f) ? NOPROBLEM_BIT : FAILRULE_BIT;
				else result =  (v1f >= v2f) ? NOPROBLEM_BIT : FAILRULE_BIT;
			}
			else if (*op == '!') result = (v1f != v2f) ? NOPROBLEM_BIT : FAILRULE_BIT;
			else result = FAILRULE_BIT;
		}
		else //   int compare
		{
			char* comma =  0; // pretty number?
			while ((comma = strchr(val1,',')))  memmove(comma,comma+1,strlen(comma+1));
			while ((comma = strchr(val2,',')))  memmove(comma,comma+1,strlen(comma+1));
			ReadInt64(val1,v1);
			ReadInt64(val2,v2);
			if (strchr(val1,',') && strchr(val2,',')) // comma numbers
			{
				size_t len1 = strlen(val1);
				size_t len2 = strlen(val2);
				if (len1 != len2) result = FAILRULE_BIT;
				else if (strcmp(val1,val2)) result = FAILRULE_BIT;
				else result = NOPROBLEM_BIT;
				if (*op == '!') result =  (result == NOPROBLEM_BIT) ? FAILRULE_BIT : NOPROBLEM_BIT;
				else if (*op != '=') ReportBug((char*)"Op not implemented for comma numbers %s",op)
			}
			else if (*op == '=') result =  (v1 == v2) ? NOPROBLEM_BIT : FAILRULE_BIT;
			else if (*op == '<') 
			{
				if (!op[1]) result =  (v1 < v2) ? NOPROBLEM_BIT : FAILRULE_BIT;
				else result =  (v1 <= v2) ? NOPROBLEM_BIT : FAILRULE_BIT;
			}
			else if (*op == '>') 
			{
				if (!op[1]) result =  (v1 > v2) ? NOPROBLEM_BIT : FAILRULE_BIT;
				else result =  (v1 >= v2) ? NOPROBLEM_BIT : FAILRULE_BIT;
			}
			else if (*op == '!') result =  (v1 != v2) ? NOPROBLEM_BIT : FAILRULE_BIT;
			else if (*op == '&') result =  (v1 & v2) ? NOPROBLEM_BIT : FAILRULE_BIT;
			else result =  FAILRULE_BIT;
		}
	}
	if (trace & TRACE_OUTPUT && output && CheckTopicTrace()) 
	{
		char x[MAX_WORD_SIZE];
		char y[MAX_WORD_SIZE];
		*x = *y = 0;
		if (*op == '&') 
		{
#ifdef WIN32
			sprintf(x,(char*)"0x%016I64x",v1);
			sprintf(y,(char*)"0x%016I64x",v2);
#else
			sprintf(x,(char*)"0x%016llx",v1);
			sprintf(y,(char*)"0x%016llx",v2); 
#endif		
		}
		if (!stricmp(word1,val1)) 
		{
			if (*word1) Log(STDTRACELOG,(char*)"%s %s ",(*x) ? x : word1,op); // no need to show value
			else Log(STDTRACELOG,(char*)"null %s ",op);
		}
		else if (!*val1) Log(STDTRACELOG,(char*)"%s(null) %s ",word1,op);
		else if (*op == '&')  Log(STDTRACELOG,(char*)"%s(%s) %s ",word1,x,op);
		else Log(STDTRACELOG,(char*)"%s(%s) %s ",word1,val1,op);

		if (word2  && !strcmp(word2,val2)) 
		{
			if (*val2) id = Log(STDTRACELOG,(char*)" %s ",(*y) ? y : word2); // no need to show value
			else id = Log(STDTRACELOG,(char*)" null "); 
		}
		else if (!*val2)  id = Log(STDTRACELOG,(char*)" %s(null) ",word2);
		else if (*op == '&') id = Log(STDTRACELOG,(char*)" %s(%s) ",word2,y);
		else id = Log(STDTRACELOG,(char*)" %s(%s) ",word2,val2);
	}
	else if (trace & TRACE_PATTERN && !output && CheckTopicTrace()) 
	{
		if (strlen(val1) >=( MAX_WORD_SIZE-1)) val1[MAX_WORD_SIZE-1] = 0;
		if (strlen(val2) >=( MAX_WORD_SIZE-1)) val2[MAX_WORD_SIZE-1] = 0;
		strcpy(word1val,val1);
		strcpy(word2val,val2);
	}

	ReleaseInverseString(val1);
	return result;
}
