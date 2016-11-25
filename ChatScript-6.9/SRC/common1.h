#ifndef COMMON1H
#define COMMON1H

typedef unsigned long long int  uint64;
typedef signed long long  int64;
#define ALWAYS (1 == always)

#define MAX_ARGUMENT_COUNT 400 //  assume 10 args 40 nested calls max. 
extern char* callArgumentList[MAX_ARGUMENT_COUNT+1];    //   function callArgumentList
extern unsigned int callArgumentBase;

#define ARGUMENT(n) callArgumentList[callArgumentBase+n]
char* ReadCompiledWord(char* ptr, char* word,bool noquote = false,bool var = false);
char* ReadCompiledWordOrCall(char* ptr, char* word,bool noquote = false,bool var = false);

#define INPUT_BUFFER_SIZE   80000
#define MAX_BUFFER_SIZE		80000

#define NUMBER_OF_LAYERS 3

#ifdef BIG_DICTIONARY
typedef uint64 MEANING;							//   a flagged indexed dict ptr
#define MAX_DICTIONARY	0x000fffffffffffffULL  //   vocabulary limit 
#define NODEBITS		0x00ffffffffffffffULL
#define MULTIWORDHEADER_SHIFT 56
#define MULTIHEADERBITS 0xFF00000000000000ULL

#define SYNSET_MARKER		0x0800000000000000ULL  // this meaning is a synset head - on keyword import, its quote flag for binary read
#define INDEX_BITS          0x03F0000000000000ULL  //   6 bits of ontology meaning indexing ability  63 possible meanings allowed
#define INDEX_OFFSET        52          //   shift for ontoindex  (rang 0..63)  
#define MAX_MEANING			63			// limit
#define INDEX_MINUS			0x0010000000000000ULL  // what to decrement to decrement the meaning index
#define MEANING_BASE		0x000fffffffffffffULL	//   the index of the dictionary item
#define TYPE_RESTRICTION	0xf000000000000000ULL  // corresponds to basic pos
#define TYPE_RESTRICTION_SHIFT 32

#else
typedef unsigned int MEANING;					//   a flagged indexed dict ptr
#define MAX_DICTIONARY	 0x000fffff				//   1M word vocabulary limit (doubling this FAILS on amazon server)
#define NODEBITS 0x00ffffff
#define MULTIWORDHEADER_SHIFT 24
#define MULTIHEADERBITS 0xFF000000

#define SYNSET_MARKER		0x08000000  // this meaning is a synset head - on keyword import, its quote flag for binary read
#define INDEX_BITS          0x03F00000  //   6 bits of ontology meaning indexing ability  63 possible meanings allowed
#define INDEX_OFFSET        20          //   shift for ontoindex  (rang 0..63)  
#define MAX_MEANING			63			// limit
#define INDEX_MINUS			0x00100000  // what to decrement to decrement the meaning index
#define MEANING_BASE		0x000fffff	//   the index of the dictionary item
#define TYPE_RESTRICTION	0xf0000000  // corresponds to basic pos
#define TYPE_RESTRICTION_SHIFT 0
#endif

#define SYSVAR_PREFIX '%'
#define MATCHVAR_PREFIX '_'
#define USERVAR_PREFIX '$'
#define TRANSIENTVAR_PREFIX '$'
#define LOCALVAR_PREFIX '_'
#define FACTSET_PREFIX '@'
#define FUNCTIONVAR_PREFIX '^'
#define TOPICCONCEPT_PREFIX '~'

#define BIG_WORD_SIZE   10000
#define MAX_WORD_SIZE   3000   
#define SMALL_WORD_SIZE  500  

#undef WORDP //   remove windows version (unsigned short) for ours

//   DoFunction results
 enum FunctionResult {
	NOPROBLEM_BIT = 0,
	ENDRULE_BIT = 0x00000001,
	FAILRULE_BIT  = 0x00000002,

	RETRYRULE_BIT =  0x00000004,
	RETRYTOPRULE_BIT =  0x00000008,

	ENDTOPIC_BIT =  0x00000010,
	FAILTOPIC_BIT  = 0x00000020,
	RETRYTOPIC_BIT  = 0x00000040,

	ENDSENTENCE_BIT =  0x00000080,
	FAILSENTENCE_BIT =  0x00000100,
	RETRYSENTENCE_BIT =  0x00000200,

	ENDINPUT_BIT  = 0x00000400,
	FAILINPUT_BIT  = 0x00000800,
	RETRYINPUT_BIT = 0x00001000,

	FAILMATCH_BIT  = 0x00002000,			// transient result of TestRule, converts to FAILRULE_BIT
	FAILLOOP_BIT  = 0x00004000,
	ENDLOOP_BIT  = 0x00008000,

	UNDEFINED_FUNCTION = 0x00010000, //   potential function call has no definition so isnt
	ENDCALL_BIT  =		0x00020000,
	NEXTLOOP_BIT =		0x00040000,
	RESTART_BIT =	0x00080000
};
 FunctionResult JavascriptArgEval(unsigned int index, char* buffer);
#endif
