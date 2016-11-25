#include "common.h"

#define LETTERMAX 40

#define ENGLISH 0
#define SPANISH 1

static unsigned char letterIndexData[256] = 
{
	0,0,0,0,0,0,0,0,0,0,	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,37,38,0,27,28,	29,30,31,32,33,34,35,36,0,0,  //37=- 38 = .  
	0,0,0,0,0,0,0,0,0,0,	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0, 	0,0,0,0,0,39,0,1,2,3, // 39 is _
	4,5,6,7,8,9,10,11,12,13, 	14,15,16,17,18,19,20,21,22,23,
	24,25,26,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0
};

static MEANING lengthLists[100];		// lists of valid words by length
bool fixedSpell = false;

typedef struct SUFFIX
{
    char* word;
	uint64 flags;
} SUFFIX;

static SUFFIX stems[] = 
{
	{ (char*)"less",NOUN},
	{ (char*)"ness",ADJECTIVE|NOUN},
	{ (char*)"est",ADJECTIVE},
	{ (char*)"en",ADJECTIVE},
	{ (char*)"er",ADJECTIVE},
	{ (char*)"ly",ADJECTIVE},
	{0},
};

void InitSpellCheck()
{
	memset(lengthLists,0,sizeof(MEANING) * 100);
	WORDP D = dictionaryBase;
	while (++D <= dictionaryFree)
	{
		if (!D->word || !IsAlphaUTF8(*D->word) || D->length >= 100 || strchr(D->word,'~') || strchr(D->word,USERVAR_PREFIX) || strchr(D->word,'^') || strchr(D->word,' ')  || strchr(D->word,'_')) continue; 
		if (D->properties & PART_OF_SPEECH || D->systemFlags & PATTERN_WORD)
		{
			D->spellNode = lengthLists[D->length];
			lengthLists[D->length] = MakeMeaning(D);
		}
	}
}

static int SplitWord(char* word)
{
	WORDP D2;
	bool good;
	int breakAt = 0;
	if (IsDigit(*word))
    {
		while (IsDigit(word[++breakAt]) || word[breakAt] == '.'){;} //   find end of number
        if (word[breakAt]) // found end of number
		{
			D2 = FindWord(word+breakAt,0,PRIMARY_CASE_ALLOWED);
			if (D2)
			{
				good = (D2->properties & (PART_OF_SPEECH|FOREIGN_WORD)) != 0 || (D2->internalBits & HAS_SUBSTITUTE) != 0; 
				if (good && (D2->systemFlags & AGE_LEARNED))// must be common words we find
				{
					char number[MAX_WORD_SIZE];
					strncpy(number,word,breakAt);
					number[breakAt] = 0;
					StoreWord(number,ADJECTIVE|NOUN|ADJECTIVE_NUMBER|NOUN_NUMBER); 
					return breakAt; // split here
				}
			}
		}
    }

	//  try all combinations of breaking the word into two known words
	breakAt = 0;
	size_t len = strlen(word);
    for (unsigned int k = 1; k < len-1; ++k)
    {
        if (k == 1 &&*word != 'a' &&*word != 'A' &&*word != 'i' &&*word != 'I') continue; //   only a and i are allowed single-letter words
		WORDP D1 = FindWord(word,k,PRIMARY_CASE_ALLOWED);
        if (!D1) continue;
		good = (D1->properties & (PART_OF_SPEECH|FOREIGN_WORD)) != 0 || (D1->internalBits & HAS_SUBSTITUTE) != 0; 
		if (!good || !(D1->systemFlags & AGE_LEARNED)) continue; // must be normal common words we find

        D2 = FindWord(word+k,len-k,PRIMARY_CASE_ALLOWED);
        if (!D2) continue;
        good = (D2->properties & (PART_OF_SPEECH|FOREIGN_WORD)) != 0 || (D2->internalBits & HAS_SUBSTITUTE) != 0;
		if (!good || !(D2->systemFlags & AGE_LEARNED) ) continue; // must be normal common words we find

        if (!breakAt) breakAt = k; // found a split
		else // found multiple places to split... dont know what to do
        {
           breakAt = -1; 
           break;
		}
    }
	return breakAt;
}

static char* SpellCheck( int i, int language)
{
    //   on entry we will have passed over words which are KnownWord (including bases) or isInitialWord (all initials)
    //   wordstarts from 1 ... wordCount is the incoming sentence words (original). We are processing the ith word here.
    char* word = wordStarts[i];
	if (!*word) return NULL;
	if (!stricmp(word,loginID) || !stricmp(word,computerID)) return word; //   dont change his/our name ever

	size_t len = strlen(word);
	if (len > 2 && word[len-2] == '\'') return word;	// dont do anything with ' words

    //   test for run togetherness like "talkabout fingers"
    int breakAt = SplitWord(word);
    if (breakAt > 0)//   we found a split, insert 2nd word into word stream
    {
		char* tokens[3];
		WORDP D = FindWord(word,breakAt,PRIMARY_CASE_ALLOWED);
		tokens[1] = D->word;
		tokens[2] = word+breakAt;
		ReplaceWords(i,1,2,tokens);
		fixedSpell = true;
		return NULL;
    }

	// now imagine partial runtogetherness, like "talkab out fingers"
	if (i < wordCount)
	{
		char tmp[MAX_WORD_SIZE*2];
		strcpy(tmp,word);
		strcat(tmp,wordStarts[i+1]);
		breakAt = SplitWord(tmp);
		if (breakAt > 0) // replace words with the dual pair
		{
			char* tokens[3];
			WORDP D = FindWord(tmp,breakAt,PRIMARY_CASE_ALLOWED);
			tokens[1] = D->word;
			tokens[2] = tmp+breakAt;
			ReplaceWords(i,2,2,tokens);
			fixedSpell = true;
			return NULL;
		}
	}

    //   remove any nondigit characters repeated more than once. Dont do this earlier, we want substitutions to have a chance at it first.  ammmmmmazing
	static char word1[MAX_WORD_SIZE];
    char* ptr = word-1; 
	char* ptr1 = word1;
    while (*++ptr)
    {
	   *ptr1 = *ptr;
	   while (ptr[1] == *ptr1 && ptr[2] == *ptr1 && (*ptr1 < '0' || *ptr1 > '9')) ++ptr; // skip double repeats
	   ++ptr1;
    }
	*ptr1 = 0;
	if (FindCanonical(word1,0,true) && !IsUpperCase(*word1)) return word1; // this is a different form of a canonical word so its ok

	//   now use word spell checker 
    char* d = SpellFix(word,i,PART_OF_SPEECH,language); 
	if (d) return d;

	// if is is a misspelled plural?
	char plural[MAX_WORD_SIZE];
	if (word[len-1] == 's')
	{
		strcpy(plural,word);
		plural[len-1] = 0;
		d = SpellFix(plural,i,PART_OF_SPEECH,language); 
		if (d) return d; // dont care that it is plural
	}

    return NULL;
}

char* ProbableKnownWord(char* word)
{
	if (strchr(word,' ') || strchr(word,'_')) return word; // not user input, is synthesized
	size_t len = strlen(word);

	// do we know the word as is?
	WORDP D = FindWord(word,0,PRIMARY_CASE_ALLOWED);
	if (D) 
	{
		if (D->properties & FOREIGN_WORD || *D->word == '~' || D->systemFlags & PATTERN_WORD) return D->word;	// we know this word clearly or its a concept set ref emotion
		if (D->properties & PART_OF_SPEECH && !IS_NEW_WORD(D)) return D->word; // old word we know
		if (IsConceptMember(D)) return D->word;
		// are there facts using this word? -- issue with facts because on seeing input second time, having made facts of original, we see original
//		if (GetSubjectNondeadHead(D) || GetObjectNondeadHead(D) || GetVerbNondeadHead(D)) return D->word;
	}
	
	char lower[MAX_WORD_SIZE];
	MakeLowerCopy(lower,word);

	// do we know the word in lower case?
	D = FindWord(word,0,LOWERCASE_LOOKUP);
	if (D) // direct recognition
	{
		if (D->properties & FOREIGN_WORD || *D->word == '~' || D->systemFlags & PATTERN_WORD) return D->word;	// we know this word clearly or its a concept set ref emotion
		if (D->properties & PART_OF_SPEECH && !IS_NEW_WORD(D)) return D->word; // old word we know
		if (IsConceptMember(D)) return D->word;

		// are there facts using this word?
//		if (GetSubjectNondeadHead(D) || GetObjectNondeadHead(D) || GetVerbNondeadHead(D)) return D->word;
	}

	// do we know the word in upper case?
	char upper[MAX_WORD_SIZE];
	MakeLowerCopy(upper,word);
	upper[0] = GetUppercaseData(upper[0]);
	D = FindWord(upper,0,UPPERCASE_LOOKUP);
	if (D) // direct recognition
	{
		if (D->properties & FOREIGN_WORD || *D->word == '~' || D->systemFlags & PATTERN_WORD) return D->word;	// we know this word clearly or its a concept set ref emotion
		if (D->properties & PART_OF_SPEECH && !IS_NEW_WORD(D)) return D->word; // old word we know
		if (IsConceptMember(D)) return D->word;

	// are there facts using this word?
//		if (GetSubjectNondeadHead(D) || GetObjectNondeadHead(D) || GetVerbNondeadHead(D)) return D->word;
	}

	// interpolate to lower case words 
	uint64 expectedBase = 0;
	if (ProbableAdjective(word,len,expectedBase) && expectedBase) return word;
	expectedBase = 0;
	if (ProbableAdverb(word,len,expectedBase) && expectedBase) return word;
	// is it a verb form
	char* verb = GetInfinitive(lower,true); // no new verbs
	if (verb) 
	{
		WORDP D =  StoreWord(lower,0); // verb form recognized
		return D->word;
	}
	
	// is it simple plural of a noun?
	if (word[len-1] == 's') 
	{
		WORDP E = FindWord(lower,len-1,LOWERCASE_LOOKUP);
		if (E && E->properties & NOUN) 
		{
			E = StoreWord(word,NOUN|NOUN_PLURAL);
			return E->word;	
		}
		E = FindWord(lower,len-1,UPPERCASE_LOOKUP);
		if (E && E->properties & NOUN) 
		{
			*word = toUppercaseData[*word];
			E = StoreWord(word,NOUN|NOUN_PROPER_PLURAL);
			return E->word;	
		}
	}

	return NULL;
}

bool SpellCheckSentence()
{
	WORDP D,E;
	fixedSpell = false;
	bool lowercase = false;
	int language = ENGLISH;
	char* lang = GetUserVariable((char*)"$cs_language");
	if (lang && !stricmp(lang,(char*)"spanish")) language = SPANISH;
	
	// check for all uppercase
	for (int i = FindOOBEnd(1) + 1; i <= wordCount; ++i) // skip start of sentence
	{
		char* word = wordStarts[i];
		size_t len = strlen(word);
		for (int j = 0; j < (int)len; ++j) 
		{
			if (IsLowerCase(word[j])) 
			{
				lowercase = true;
				i = j = 1000;
			}
		}
	}
	if (!lowercase && wordCount > 2) // must have several words in uppercase
	{
		for (int i = FindOOBEnd(1); i <= wordCount; ++i)
		{
			char* word = wordStarts[i];
			MakeLowerCase(word);
		}
	}

	int startWord = FindOOBEnd(1);
	for (int i = startWord; i <= wordCount; ++i)
	{
		char* word = wordStarts[i];
		if (!word || !word[1] || *word == '"' ) continue; // illegal or single char or quoted thingy 
		size_t len = strlen(word);

		// dont spell check uppercase not at start or joined word
		if (IsUpperCase(word[0]) && (i != startWord || strchr(word,'_')) && tokenControl & NO_PROPER_SPELLCHECK) continue; 
		//  dont  spell check email or other things with @ or . in them
		if (strchr(word,'@') || strchr(word,'.') || strchr(word,'$')) continue;

		// dont spell check names of json objects or arrays
		if (!strnicmp(word,"ja-",3) || !strnicmp(word,"jo-",3)) continue;

		// dont spell check web addresses
		if (!strnicmp(word,"http",4) || !strnicmp(word,"www",3)) continue;

		char* known = ProbableKnownWord(word);
		if (known && !strcmp(known,word)) continue;	 // we know it
		if (known && strcmp(known,word)) 
		{
			char* tokens[2];
			if (!IsUpperCase(*known)) // revised the word to lower case (avoid to upper case like "fields" to "Fields"
			{
				WORDP D = FindWord(known,0,LOWERCASE_LOOKUP);
				if (D) 
				{
					tokens[1] = D->word;
					ReplaceWords(i,1,1,tokens);
					fixedSpell = true;
					continue;
				}
			}
			else // is uppercase a concept member? then revise upwards
			{
				WORDP D = FindWord(known,0,UPPERCASE_LOOKUP);
				if (IsConceptMember(D))
				{
					tokens[1] = D->word;
					ReplaceWords(i,1,1,tokens);
					fixedSpell = true;		
					continue;
				}
			}
		}

		char* p = word -1;
		unsigned char c;
		char* hyphen = 0;
		while ((c = *++p) != 0)
		{ 
			++len;
			if (c == '-') hyphen = p; // note is hyphenated - use trailing
		}
		if (len == 0 || GetTemperatureLetter(word)) continue;	// bad ignore utf word or llegal length - also no composite words
		if (c && c != '@' && c != '.') // illegal word character
		{
			if (IsDigit(word[0]) || len == 1){;} // probable numeric?
			// accidental junk on end of word we do know immedately?
			else if (i > 1 && !IsAlphaUTF8OrDigit(wordStarts[i][len-1]) )
			{
				WORDP entry,canonical;
				char word[MAX_WORD_SIZE];
				strcpy(word,wordStarts[i]);
				word[len-1] = 0;
				uint64 sysflags = 0;
				uint64 cansysflags = 0;
				WORDP revise;
				GetPosData(i,word,revise,entry,canonical,sysflags,cansysflags,true,true); // dont create a non-existent word
				if (entry && entry->properties & PART_OF_SPEECH)
				{
					wordStarts[i] = reuseAllocation(wordStarts[i],entry->word);
					fixedSpell = true;
					continue;	// not a legal word character, leave it alone
				}
			}
		}

		// see if we know the other case
		if (!(tokenControl & (ONLY_LOWERCASE|STRICT_CASING)) || (i == startSentence && !(tokenControl & ONLY_LOWERCASE)))
		{
			WORDP E = FindWord(word,0,SECONDARY_CASE_ALLOWED);
			bool useAlternateCase = false;
			if (E && E->systemFlags & PATTERN_WORD) useAlternateCase = true;
			if (E && E->properties & (PART_OF_SPEECH|FOREIGN_WORD))
			{
				// if the word we find is UPPER case, and this might be a lower case noun plural, don't change case.
				size_t len = strlen(word);
				if (word[len-1] == 's' ) 
				{
					WORDP F = FindWord(word,len-1);
					if (!F || !(F->properties & (PART_OF_SPEECH|FOREIGN_WORD))) useAlternateCase = true;
					else continue;
				}
				else useAlternateCase = true;
			}
			else if (E) // does it have a member concept fact
			{
				if (IsConceptMember(E)) 
				{
					useAlternateCase = true;
					break;
				}
			}
			if (useAlternateCase)
			{
				char* tokens[2];
				tokens[1] = E->word;
				ReplaceWords(i,1,1,tokens);
				fixedSpell = true;
				continue;	
			}
		}
		
		// merge with next token?
		char join[MAX_WORD_SIZE * 3];
		if (i != wordCount && *wordStarts[i+1] != '"' )
		{
			// direct merge as a single word
			strcpy(join,word);
			strcat(join,wordStarts[i+1]);
			WORDP D = FindWord(join,0,(tokenControl & ONLY_LOWERCASE) ?  PRIMARY_CASE_ALLOWED : STANDARD_LOOKUP);

			strcpy(join,word);
			if (!D || !(D->properties & PART_OF_SPEECH) ) // merge these two, except "going to" or wordnet composites of normal words  // merge as a compound word
			{
				strcat(join,(char*)"_");
				strcat(join,wordStarts[i+1]);
				D = FindWord(join,0,(tokenControl & ONLY_LOWERCASE) ?  PRIMARY_CASE_ALLOWED : STANDARD_LOOKUP);
			}

			if (D && D->properties & PART_OF_SPEECH && !(D->properties & AUX_VERB)) // merge these two, except "going to" or wordnet composites of normal words
			{
				WORDP P1 = FindWord(word,0,LOWERCASE_LOOKUP);
				WORDP P2 = FindWord(wordStarts[i+1],0,LOWERCASE_LOOKUP);
				if (!P1 || !P2 || !(P1->properties & PART_OF_SPEECH) || !(P2->properties & PART_OF_SPEECH)) 
				{
					char* tokens[2];
					tokens[1] = D->word;
					ReplaceWords(i,2,1,tokens);
					fixedSpell = true;
					continue;
				}
			}
		}   

		// break apart slashed pair like eat/feed
		char* slash = strchr(word,'/');
		if (slash && slash != word && slash[1]) //   break apart word/word
		{
			if ((wordCount + 2 ) >= REAL_SENTENCE_LIMIT) continue;	// no room
			*slash = 0;
			D = StoreWord(word);
			*slash = '/';
			E = StoreWord(slash+1);
			char* tokens[4];
			tokens[1] = D->word;
			tokens[2] = "/";
			tokens[3] = E->word;
			ReplaceWords(i,1,3,tokens);
			fixedSpell = true;
			--i;
			continue;
		}

		// see if hypenated word should be separate or joined (ignore obvious adjective suffix)
		if (hyphen &&  !stricmp(hyphen,(char*)"-like"))
		{
			StoreWord(word,ADJECTIVE_NORMAL|ADJECTIVE); // accept it as a word
			continue;
		}
		else if (hyphen && (hyphen-word) > 1)
		{
			char test[MAX_WORD_SIZE];
			char first[MAX_WORD_SIZE];

			// test for split
			*hyphen = 0;
			strcpy(test,hyphen+1);
			strcpy(first,word);
			*hyphen = '-';

			WORDP E = FindWord(test,0,LOWERCASE_LOOKUP);
			WORDP D = FindWord(first,0,LOWERCASE_LOOKUP);
			if (*first == 0) 
			{
				wordStarts[i] = AllocateString(wordStarts[i] + 1); // -pieces  want to lose the leading hypen  (2-pieces)
				fixedSpell = true;
			}
			else if (D && E) //   1st word gets replaced, we added another word after
			{
				if ((wordCount + 1 ) >= REAL_SENTENCE_LIMIT) continue;	// no room
				char* tokens[3];
				tokens[1] = D->word;
				tokens[2] = E->word;
				ReplaceWords(i,1,2,tokens);
				fixedSpell = true;
				--i;
			}
			else if (!stricmp(test,(char*)"old") || !stricmp(test,(char*)"olds")) //   break apart 5-year-old
			{
				if ((wordCount + 1 ) >= REAL_SENTENCE_LIMIT) continue;	// no room
				D = StoreWord(first);
				E = StoreWord(test);
				char* tokens[3];
				tokens[1] = D->word;
				tokens[2] = E->word;
				ReplaceWords(i,1,2,tokens);
				fixedSpell = true;
				--i;
			}
			else // remove hyphen entirely?
			{
				strcpy(test,first);
				strcat(test,hyphen+1);
				D = FindWord(test,0,(tokenControl & ONLY_LOWERCASE) ?  PRIMARY_CASE_ALLOWED : STANDARD_LOOKUP);
				if (D) 
				{
					wordStarts[i] = D->word;
					fixedSpell = true;
					--i;
				}
			}
			continue; // ignore hypenated errors that we couldnt solve, because no one mistypes a hypen
		}
		
		// leave uppercase in first position if not adjusted yet... but check for lower case spell error
		if (IsUpperCase(word[0])  && tokenControl & NO_PROPER_SPELLCHECK) 
		{
			char lower[MAX_WORD_SIZE];
			MakeLowerCopy(lower,word);
			WORDP D = FindWord(lower,0,LOWERCASE_LOOKUP);
			if (!D && i == startWord)
			{
				char* okword = SpellFix(lower,i,PART_OF_SPEECH,language); 
				if (okword)
				{
					char* tokens[2];
					WORDP E = StoreWord(okword);
					tokens[1] = E->word;
					ReplaceWords(i,1,1,tokens);
					fixedSpell = true;
				}
			}
			continue; 
		}

		if (*word != '\'' && (!FindCanonical(word, i,true) || IsUpperCase(word[0]))) // dont check quoted or findable words unless they are capitalized
		{
			word = SpellCheck(i,language);

			// dont spell check proper names to improper, if word before or after is lower case originally
			if (word && i != 1 && originalCapState[i] && !IsUpperCase(*word))
			{
				if (!originalCapState[i-1]) return false;
				else if (i != wordCount && !originalCapState[i+1]) return false;
			}

			if (word && !*word) // performed substitution on prior word, restart this one
			{
				fixedSpell = true;
				--i;
				continue;
			}
			if (word) 
			{
				char* tokens[2];
				tokens[1] = word;
				ReplaceWords(i,1,1,tokens);
				fixedSpell = true;
				continue;
			}
		}
    }
	return fixedSpell;
}

static int EditDistance(WORDP D, unsigned int size, unsigned int inputLen, unsigned char* inputSet, int min,
		unsigned char realWordLetterCounts[LETTERMAX], int language)
{//   dictword has no underscores, inputSet is already lower case
    unsigned char dictw[MAX_WORD_SIZE];
    MakeLowerCopy((char*)dictw,D->word);
    unsigned char* dictinfo = dictw;
    unsigned char* dictstart = dictinfo;
	unsigned char* inputstart = (unsigned char*) inputSet;
    int val = 0; //   a difference in length will manifest as a difference in letter count
    //   how many changes  (change a letter, transpose adj letters, insert letter, drop letter)
    if (size != inputLen) 
	{
		val += (size < inputLen) ? 5 : 2;	// real word is shorter than what they typed, not so likely as longer
		if (size < 7) val += 3;	
	}
    if (val > min) return 60;	// fast abort
	
	// match off how many letter counts are correct between the two, need to be close enough to bother with
	unsigned char dictWordLetterSet[LETTERMAX];
 	memset(dictWordLetterSet,0,LETTERMAX); 
	for (unsigned int  i = 0; i < size; ++i) 
	{
		int index = letterIndexData[(unsigned char)dictinfo[i]];
		++dictWordLetterSet[index]; // computer number of each kind of letter
	}
	unsigned int count = 0;
	for (unsigned int  i = 0; i < LETTERMAX; ++i) // count how many letters are the same in both words
	{
		if (dictWordLetterSet[i]) // revised word has these many
		{
			int diff = dictWordLetterSet[i] - realWordLetterCounts[i]; // how many of ours does real have?
			if (diff < 0) count += dictWordLetterSet[i]; // he has more than we have, he gets credit for ours he does have
			else count += dictWordLetterSet[i] - diff; // he has <= what we have, count them
		}
	}
	unsigned int countVariation = size - ((size > 7) ? 3 : 2); // since size >= 2, this is always >= 0
	if (count < countVariation  && language == ENGLISH)  return 60;	// need most letters be in common
	if (count == size && language == ENGLISH)  // same letters (though he may have excess) --  how many transposes
	{
		unsigned int bad = 0;
		for (unsigned int i = 0; i < size; ++i) if (dictinfo[i] != inputSet[i]) ++bad;
		if (size != inputLen){;}
		else if (bad <= 2) return val + 3; // 1 transpose
		else if (bad <= 4) return val + 9; // 2 transpose
		else return val + 38; // many transpose
    }
	
	// now look at specific letter errors
    unsigned char* dictend = dictinfo+size;
    unsigned char* inputend = inputSet+inputLen;
	count = 0;
    while (ALWAYS)
    {
		++count;
        if (*dictinfo == *inputSet) // match
        {
            if (inputSet == inputend && dictinfo == dictend) break;    // ended
            ++inputSet;
            ++dictinfo;
            continue;
        }
        if (inputSet == inputend || dictinfo == dictend) // one ending, other has to catch up by adding a letter
        {
            if (inputSet == inputend) ++dictinfo;
            else ++inputSet;
            val += 6;
            continue;
        }

        //   letter match failed
		
        // can we change an accented letter forward to another similar letter without accent
		if (*dictinfo == 0xc3)
		{
			bool accent = false;
			if (*inputSet == 'a' && (dictinfo[1] >= 0xa0 && dictinfo[1] <= 0xa5 )) accent = true;
			else if (*inputSet == 'e' && (dictinfo[1] >= 0xa8 && dictinfo[1] <= 0xab )) accent = true;
			else if (*inputSet == 'i' &&  (dictinfo[1] >= 0xac && dictinfo[1] <= 0xaf )) accent = true;
			else if (*inputSet == 'o' && (dictinfo[1] >= 0xb2 && dictinfo[1] <= 0xb6 )) accent = true;
			else if (*inputSet == 'u' && (dictinfo[1] >= 0xb9 && dictinfo[1] <= 0xbc )) accent = true;
			if (accent)
			{
				++dictinfo;
				++dictinfo; // double unicode
				++inputSet;
				continue;
			}
		}
		  //   first and last letter errors are rare, more likely to get them right
		if (dictinfo == dictstart && *dictstart != *inputstart && language == ENGLISH) val += 6; // costs a lot  to change first letter, odds are he types that right 
		if (dictinfo[1] == 0 &&  inputSet[1] == 0 &&  *dictinfo != *inputSet) val += 6; // costs more to change last letter, odds are he types that right or sees its wrong
  
        //   try to resynch series and reduce cost of a transposition of adj letters  
        if (*dictinfo == inputSet[1] && dictinfo[1] == *inputSet) // transpose 
        {
			if (dictinfo[2] == inputSet[2]) // they match after, so transpose is pretty likely
			{
				val += 4;  
				if (dictinfo[2]) // not at end, skip the letter in synch for speed
				{
					++dictinfo;
					++inputSet;
				}
			}
			else val += 8;  // transposed maybe good, assume it is
   			dictinfo += 2;
			inputSet += 2;
		}
        else if (*dictinfo == inputSet[1]) // current dict letter matches matches his next input letter, so maybe his input inserted a char here and need to delete it 
        {
            char* prior = (char*) inputSet-1; // potential extraneous letter
            if (*prior == *inputSet) val += 5; // low cost for dropping an excess repeated letter - start of word is prepadded with 0 for prior char
            else if (*inputSet == '-') val += 3; //   very low cost for removing a hypen 
            else if (inputSet+1 == inputend && *inputSet == 's') val += 30;    // losing a trailing s is almost not acceptable
            else val += 9; //  high cost removing an extra letter, but not as much as having to change it
            ++inputSet;
		}
        else if (dictinfo[1] == *inputSet) // next dict leter matches current input letter, so maybe his input deleted a char here and needs to insert  it
        {
            char* prior = (dictinfo == dictstart) ? (char*)" " : ((char*)(dictinfo-1));
            if (*dictinfo == *prior  && !IsVowel(*dictinfo )) val += 5; 
            else if (IsVowel(*dictinfo ))  val += 1; //  low cost for missing a vowel ( already charged for short input), might be a texting abbreviation
            else val += 9; // high cost for deleting a character, but not as much as changing it
            ++dictinfo;
       }
       else //   this has no valid neighbors.  alter it to be the correct, but charge for multiple occurences
       {
			if (count == 1 && *dictinfo != *inputSet && language == ENGLISH) val += 30; //costs a lot to change the first letter, odds are he types that right or sees its wrong
			//  2 in a row are bad, check for a substituted vowel sound
			bool swap = false;
			int oldval = val;
			if (dictinfo[1] != inputSet[1]) // do multicharacter transformations
			{
				if (language == SPANISH) // ch-x | qu-k | c-k | do-o | b-v | bue-w | vue-w | z-s | s-c | h- | y-i | y-ll | m-n  1st is valid
				{
					if (*inputSet == 'c' && *dictinfo == 'k') 
					{
						dictinfo += 1;
						inputSet += 1;
						continue;
					}
					if (*inputSet == 'b' && *dictinfo == 'v') 
					{
						dictinfo += 1;
						inputSet += 1;
						continue;
					}
					if (*inputSet == 'v' && *dictinfo == 'b') 
					{
						dictinfo += 1;
						inputSet += 1;
						continue;
					}
					if (*inputSet == 'z' && *dictinfo == 's') 
					{
						dictinfo += 1;
						inputSet += 1;
						continue;
					}
					if (*inputSet == 's' && *dictinfo == 'c') 
					{
						dictinfo += 1;
						inputSet += 1;
						continue;
					}
					if (*inputSet == 'y' && *dictinfo == 'i') 
					{
						dictinfo += 1;
						inputSet += 1;
						continue;
					}
					if (*inputSet == 'm' && *dictinfo == 'n') 
					{
						dictinfo += 1;
						inputSet += 1;
						continue;
					}
					if (*inputSet == 'n' && *dictinfo == 'm') 
					{
						dictinfo += 1;
						inputSet += 1;
						continue;
					}
					if (*dictinfo == 'h') 
					{
						dictinfo += 1;
						continue;
					}
					if (*inputSet == 'x' && !strncmp((char*)dictinfo,(char*)"ch",2)) 
					{
						dictinfo += 2;
						inputSet += 1;
						val -= (size < inputLen) ? 5 : 2;
						if (size < 7) val -= 3;	
						if (val < 0) val = 0;
						continue;
					}
					if (*inputSet == 'k' && !strncmp((char*)dictinfo,(char*)"qu",2)) 
					{
						dictinfo += 2;
						inputSet += 1;
						val -= (size < inputLen) ? 5 : 2;
						if (size < 7) val -= 3;	
						if (val < 0) val = 0;
						continue;
					}
					if (*inputSet == 'o' && !strncmp((char*)dictinfo,(char*)"do",2) && !inputSet[1] && !dictinfo[2]) // at end
					{
						dictinfo += 2;
						inputSet += 1;
						val -= (size < inputLen) ? 5 : 2;
						if (size < 7) val -= 3;	
						if (val < 0) val = 0;
						continue;
					}
					if (*inputSet == 'w' && !strncmp((char*)dictinfo,(char*)"bue",3)) 
					{
						dictinfo += 3;
						inputSet += 1;
						val -= (size < inputLen) ? 5 : 2;
						if (size < 7) val -= 3;	
						if (val < 0) val = 0;
						continue;
					}
					if (*inputSet == 'w' && !strncmp((char*)dictinfo,(char*)"vue",3)) 
					{
						dictinfo += 3;
						inputSet += 1;
						val -= (size < inputLen) ? 5 : 2;
						if (size < 7) val -= 3;	
						if (val < 0) val = 0;
						continue;
					}
					if (!strncmp((char*)inputSet,(char*)"ll",2) && *dictinfo == 'y') 
					{
						inputSet += 2;
						dictinfo += 1;
						val -= (size < inputLen) ? 5 : 2;
						if (size < 7) val -= 3;	
						if (val < 0) val = 0;
						continue;
					}
					if (*inputSet == 'y' && *dictinfo == 'l' && dictinfo[1] == 'l') 
					{
						inputSet += 1;
						dictinfo += 2;
						val -= (size < inputLen) ? 5 : 2;
						if (size < 7) val -= 3;	
						if (val < 0) val = 0;
						continue;
					}
				}

				if (*inputSet == 't' && !strncmp((char*)dictinfo,(char*)"ght",3)) 
				{
                    dictinfo += 3;
                    inputSet += 1;
                    val += 5;  
				}
				else if (!strncmp((char*)inputSet,(char*)"ci",2) && !strncmp((char*)dictinfo,(char*)"cki",3)) 
				{
                    dictinfo += 3;
                    inputSet += 2;
                    val += 5;
				}
				else if (*(dictinfo-1) == 'a' && !strcmp((char*)dictinfo,(char*)"ir") && !strcmp((char*)inputSet,(char*)"re")) // prepair prepare as terminal sound
				{
                    dictinfo += 2;
                    inputSet += 2;
                    val += 3;
				}
				else if (!strncmp((char*)inputSet,(char*)"ous",3) && !strncmp((char*)dictinfo,(char*)"eous",4)) 
				{
                    dictinfo += 4;
                    inputSet += 3;
                    val += 5; 
               }
              else if (!strncmp((char*)inputSet,(char*)"of",2) && !strncmp((char*)dictinfo,(char*)"oph",3)) 
               {
                    dictinfo += 3;
                    inputSet += 2;
                    val += 5; 
               }
			else if (*dictinfo == 'x' && !strncmp((char*)inputSet,(char*)"cks",3)) 
               {
                    dictinfo += 1;
                    inputSet += 3;
                    val += 5; 
               }
               else if (*inputSet == 'k' && !strncmp((char*)dictinfo,(char*)"qu",2)) 
               {
                    dictinfo += 2;
                    inputSet += 1;
                    val += 5;  
               }
			   if (oldval != val){;} // swallowed a multiple letter sound change
               else if (!strncmp((char*)dictinfo,(char*)"able",4) && !strncmp((char*)inputSet,(char*)"ible",4)) swap = true;
               else if (!strncmp((char*)dictinfo,(char*)"ible",4) && !strncmp((char*)inputSet,(char*)"able",4)) swap = true;
               else if (*dictinfo == 'a' && dictinfo[1] == 'y'     && *inputSet == 'e' && inputSet[1] == 'i') swap = true;
               else if (*dictinfo == 'e' && dictinfo[1] == 'a'     && *inputSet == 'e' && inputSet[1] == 'e') swap = true;
               else if (*dictinfo == 'e' && dictinfo[1] == 'e'     && *inputSet == 'e' && inputSet[1] == 'a') swap = true;
               else if (*dictinfo == 'e' && dictinfo[1] == 'e'     && *inputSet == 'i' && inputSet[1] == 'e') swap = true;
               else if (*dictinfo == 'e' && dictinfo[1] == 'i'     && *inputSet == 'a' && inputSet[1] == 'y') swap = true;
               else if (*dictinfo == 'e' && dictinfo[1] == 'u'     && *inputSet == 'o' && inputSet[1] == 'o') swap = true;
               else if (*dictinfo == 'e' && dictinfo[1] == 'u'     && *inputSet == 'o' && inputSet[1] == 'u') swap = true;
               else if (*dictinfo == 'i' && dictinfo[1] == 'e'     && *inputSet == 'e' && inputSet[1] == 'e') swap = true;
               else if (*dictinfo == 'o' && dictinfo[1] == 'o'     && *inputSet == 'e' && inputSet[1] == 'u') swap = true;
               else if (*dictinfo == 'o' && dictinfo[1] == 'o'     && *inputSet == 'o' && inputSet[1] == 'u') swap = true;
               else if (*dictinfo == 'o' && dictinfo[1] == 'o'     && *inputSet == 'u' && inputSet[1] == 'i') swap = true;
               else if (*dictinfo == 'o' && dictinfo[1] == 'u'     && *inputSet == 'e' && inputSet[1] == 'u') swap = true;
               else if (*dictinfo == 'o' && dictinfo[1] == 'u'     && *inputSet == 'o' && inputSet[1] == 'o') swap = true;
               else if (*dictinfo == 'u' && dictinfo[1] == 'i'     && *inputSet == 'o' && inputSet[1] == 'o') swap = true;
               if (swap)
               {
                    dictinfo += 2;
                    inputSet += 2;
                    val += 5; 
               }
            } 

            // can we change a letter to another similar letter
            if (oldval == val) 
            {
				bool convert = false;
                if (*dictinfo == 'i' && *inputSet== 'y' && count > 1) convert = true;//   but not as first letter
                else if ((*dictinfo == 's' && *inputSet == 'z') || (*dictinfo == 'z' && *inputSet == 's')) convert = true;
                else if (*dictinfo == 'y' && *inputSet == 'i' && count > 1) convert = true; //   but not as first letter
                else if (*dictinfo == '/' && *inputSet == '-') convert = true;
                else if (inputSet+1 == inputend && *inputSet == 's') val += 30;    //   changing a trailing s is almost not acceptable
                if (convert) val += 5;	// low cost for exchange of similar letter, but dont do it often
                else val += 12;			// changing a letter is expensive, since it destroys the visual image
                ++dictinfo;
                ++inputSet;
            }
       } 
       if (val > min) return val; // too costly, ignore it
    }
    return val;
}


static char* StemSpell(char* word,unsigned int i)
{
    static char word1[MAX_WORD_SIZE];
    strcpy(word1,word);
    size_t len = strlen(word);

	char* ending = NULL;
    char* best = NULL;
    
	//   suffixes
	if (len < 5){;} // too small to have a suffix we care about (suffix == 2 at min)
    else if (!strnicmp(word+len-3,(char*)"ing",3))
    {
        word1[len-3] = 0;
        best = SpellFix(word1,0,VERB,ENGLISH); 
        if (best && FindWord(best,0,LOWERCASE_LOOKUP)) return GetPresentParticiple(best);
	}
    else if (!strnicmp(word+len-2,(char*)"ed",2))
    {
        word1[len-2] = 0;
        best = SpellFix(word1,0,VERB,ENGLISH); 
        if (best)
        {
			char* past = GetPastTense(best);
			if (!past) return NULL;
			size_t pastlen = strlen(past);
			if (past[pastlen-1] == 'd') return past;
			ending = "ed";
        }
    }
	else
	{
		unsigned int i = 0;
		char* suffix;
		while ((suffix = stems[i].word))
		{
			uint64 kind = stems[i++].flags;
			size_t suffixlen = strlen(suffix);
			if (!strnicmp(word+len-suffixlen,suffix,suffixlen))
			{
				word1[len-suffixlen] = 0;
				best = SpellFix(word1,0,kind,ENGLISH); 
				if (best) 
				{
					ending = suffix;
					break;
				}
			}
		}
	}
	if (!ending && word[len-1] == 's')
    {
        word1[len-1] = 0;
        best = SpellFix(word1,0,VERB|NOUN,ENGLISH); 
        if (best)
        {
			WORDP F = FindWord(best,0,(tokenControl & ONLY_LOWERCASE) ?  PRIMARY_CASE_ALLOWED : STANDARD_LOOKUP);
			if (F && F->properties & NOUN) return GetPluralNoun(F);
			ending = "s";
        }
   }
   if (ending)
   {
		strcpy(word1,best);
		strcat(word1,ending);
		return word1;
   }
   return NULL;
}

char* SpellFix(char* originalWord,int start,uint64 posflags,int language)
{
    size_t len = strlen(originalWord);
	if (len >= 100 || len == 0) return NULL;
	if (IsDigit(*originalWord)) return NULL; // number-based words and numbers must be treated elsewhere
	char letterLow = GetLowercaseData(*originalWord);
	char letterHigh = GetUppercaseData(*originalWord);
	bool hasUnderscore = (strchr(originalWord,'_')) ? true : false;
	bool isUpper = IsUpperCase(originalWord[0]);
	if (IsUpperCase(originalWord[1])) isUpper = false;	// not if all caps
	if (trace == TRACE_SPELLING) Log(STDTRACELOG,(char*)"Spell: %s\r\n",originalWord);

	char word[MAX_WORD_SIZE];
	MakeLowerCopy(word,originalWord);

	// mark positions of the letters and make lower case
    char base[257];
    memset(base,0,257);
    char* ptr = word - 1;
    char c;
    int position = 0;
    while ((c = *++ptr) && position < 255)
    {
        base[position++ + 1] = GetLowercaseData(c);
   }

	//   Priority is to a word that looks like what the user typed, because the user probably would have noticed if it didnt and changed it. So add/delete  has priority over tranform
    WORDP choices[4000];
    WORDP bestGuess[4000];
    unsigned int index = 0;
    unsigned int bestGuessindex = 0;
    int min = 30;
	unsigned char realWordLetterCounts[LETTERMAX];
	memset(realWordLetterCounts,0,LETTERMAX); 
	for (int  i = 0; i < (int)len; ++i)  ++realWordLetterCounts[(unsigned char)letterIndexData[(unsigned char)word[i]]]; // compute number of each kind of character
	
	uint64  pos = PART_OF_SPEECH;  // all pos allowed
    WORDP D;
    if (posflags == PART_OF_SPEECH && start < wordCount) // see if we can restrict word based on next word
    {
        D = FindWord(wordStarts[start+1],0,PRIMARY_CASE_ALLOWED);
        uint64 flags = (D) ? D->properties : (-1); //   if we dont know the word, it could be anything
        if (flags & PREPOSITION) pos &= -1 ^ (PREPOSITION|NOUN);   //   prep cannot be preceeded by noun or prep
        if (!(flags & (PREPOSITION|VERB|CONJUNCTION|ADVERB)) && flags & DETERMINER) pos &= -1 ^ (DETERMINER|ADJECTIVE|NOUN|ADJECTIVE_NUMBER|NOUN_NUMBER); //   determiner cannot be preceeded by noun determiner adjective
        if (!(flags & (PREPOSITION|VERB|CONJUNCTION|DETERMINER|ADVERB)) && flags & ADJECTIVE) pos &= -1 ^ (NOUN); 
        if (!(flags & (PREPOSITION|NOUN|CONJUNCTION|DETERMINER|ADVERB|ADJECTIVE)) && flags & VERB) pos &= -1 ^ (VERB); //   we know all helper verbs we might be
        if (D && *D->word == '\'' && D->word[1] == 's' ) pos &= NOUN;    //   we can only be a noun if possessive - contracted 's should already be removed by now
    }
    if (posflags == PART_OF_SPEECH && start > 1)
    {
        D = FindWord(wordStarts[start-1],0,PRIMARY_CASE_ALLOWED);
        uint64 flags = (D) ? D->properties : (-1); // if we dont know the word, it could be anything
        if (flags & DETERMINER) pos &= -1 ^ (VERB|CONJUNCTION|PREPOSITION|DETERMINER);  
    }
    posflags &= pos; //   if pos types are known and restricted and dont match
	static int range[] = {0,-1,1,-2,2};
	for (unsigned int i = 0; i < 5; ++i)
	{
		if (language == ENGLISH && i >= 3) break;	// only allow +-2 for spanish
		MEANING offset = lengthLists[len + range[i]];
		if (trace == TRACE_SPELLING) Log(STDTRACELOG,(char*)"\r\n  Begin offset %d\r\n",i);
		while (offset)
		{
			D = Meaning2Word(offset);
			offset = D->spellNode;
			if (PART_OF_SPEECH == posflags  && D->systemFlags & PATTERN_WORD){;} // legal generic match
			else if (!(D->properties & posflags)) continue; // wrong kind of word
			if (*D->word != letterLow && *D->word != letterHigh && language == ENGLISH) continue;	// we assume no one misspells starting letter
			char* under = strchr(D->word,'_');
			// SPELLING lists have no underscore or space words in them
			if (hasUnderscore && !under) continue;	 // require keep any underscore
			if (!hasUnderscore && under) continue;	 // require not have any underscore
			if (isUpper && !(D->internalBits & UPPERCASE_HASH) && start != 1) continue;	// dont spell check to lower a word in upper
			int val = EditDistance(D, D->length, len, (unsigned char*)base+1,min,realWordLetterCounts,language);
			if (val <= min) // as good or better
			{
				if (val < min)
				{
					if (trace == TRACE_SPELLING) Log(STDTRACELOG,(char*)"    Better: %s against %s value: %d\r\n",D->word,originalWord,val);
					index = 0;
					min = val;
				}
				else if ( val == min && trace == TRACE_SPELLING) Log(STDTRACELOG,(char*)"    Equal: %s against %s value: %d\r\n",D->word,originalWord,val);

				if (!(D->internalBits & BEEN_HERE)) 
				{
					choices[index++] = D;
					if (index > 3998) break; 
					AddInternalFlag(D,BEEN_HERE);
				}
			}
		}
	}
	// try endings ing, s, etc
	if (start) // no stem spell if COMING from a stem spell attempt (start == 0)
	{
		char* stem = StemSpell(word,start);
		if (stem) 
		{
			WORDP D = FindWord(stem,0,(tokenControl & ONLY_LOWERCASE) ?  PRIMARY_CASE_ALLOWED : STANDARD_LOOKUP);
			if (D) 
			{
				for (unsigned int j = 0; j < index; ++j) 
				{
					if (choices[j] == D) // already in our list
					{
						D = NULL; 
						break;
					}
				}
			}
			if (D) choices[index++] = D;
		}
	}

    if (!index)  return NULL; 

	// take our guesses, and pick the most common (earliest learned or most frequently used) word
    uint64 commonmin = 0;
    bestGuess[0] = NULL;
	for (unsigned int j = 0; j < index; ++j) RemoveInternalFlag(choices[j],BEEN_HERE);
    if (index == 1) 
	{
		if (trace == TRACE_SPELLING) Log(STDTRACELOG,(char*)"    Single best spell: %s\r\n",choices[0]->word);
		return choices[0]->word;	// pick the one
	}
    for (unsigned int j = 0; j < index; ++j) 
    {
        uint64 common = choices[j]->systemFlags & COMMONNESS;
        if (common < commonmin) continue;
		if (choices[j]->internalBits & UPPERCASE_HASH && index > 1) continue;	// ignore proper names for spell better when some other choice exists
        if (common > commonmin)
        {
            commonmin = common;
            bestGuessindex = 0;
        }
        bestGuess[bestGuessindex++] = choices[j];
    }
	if (bestGuessindex) 
	{
		if (trace == TRACE_SPELLING) Log(STDTRACELOG,(char*)"    Pick spell: %s\r\n",bestGuess[0]->word);
		return bestGuess[0]->word; 
	}
	return NULL;
}

