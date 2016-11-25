#ifndef _JSONH_
#define _JSONH_
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
#define JSON_LIMIT 8000
#ifndef DISCARDJSON
extern int jsonOpenSize; 
FunctionResult JSONTreeCode(char* buffer);
FunctionResult JSONKindCode(char* buffer);
FunctionResult JSONPathCode(char* buffer);
FunctionResult JSONFormatCode(char* buffer);
FunctionResult JSONParseFileCode(char* buffer);
FunctionResult JSONObjectInsertCode(char* buffer) ;
FunctionResult JSONVariableAssign(char* word,char* value);
FunctionResult JSONArrayInsertCode(char* buffer) ;
FunctionResult JSONOpenCode(char* buffer);
FunctionResult JSONLabelCode(char* buffer) ;
FunctionResult JSONUndecodeStringCode(char* buffer) ;
FunctionResult JSONWriteCode(char* buffer);
FunctionResult JSONParseCode(char* buffer);
FunctionResult JSONArrayDeleteCode(char* buffer);
FunctionResult JSONArraySizeCode(char* buffer);
FunctionResult JSONGatherCode(char* buffer);
FunctionResult ParseJson(char* buffer, char* message, size_t size,bool nofail);
FunctionResult JSONDeleteCode(char* buffer); 
FunctionResult JSONCopyCode(char* buffer);
FunctionResult JSONCreateCode(char* buffer);
MEANING jsonValue(char* value, unsigned int& flags);
void JsonRenumber(FACT* F);
void jkillfact(WORDP D);
void InitJSONNames();
void CurlShutdown();
#endif

#endif