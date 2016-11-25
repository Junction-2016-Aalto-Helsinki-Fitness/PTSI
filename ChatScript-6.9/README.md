# ChatScript
Natural Language tool/dialog manager

[ChatScript Wiki (user guides, tutorials, papers)](https://github.com/bwilcox-1234/ChatScript/blob/master/WIKI/README.md)

ChatScript is the next generation chatbot engine that has won the Loebner's 4 times and is the basis for natural language company for a variety of tech startups.

See BrilligUnderstanding.com for our home website.

# Basic Features

Powerful pattern matching aimed at detecting meaning.

Simple rule layout combined with C-style general scripting.

Built-in WordNet dictionary for ontology and spell-checking.

Extensive extensible ontology of nouns, verbs, adjectives, adverbs.

Data as fact triples enables inferencing.

Rules can examine and alter engine and script behavior.

Planner capabilities allow a bot to act in real/virtual worlds.

Remembers user interactions across conversations.

Document mode allows you to scan documents for content.

Ability to control local machines via popen.

Ability to read structured JSON data from websites.

Postgres and Mongo support for big data or large-user-volume chatbots.



# OS Features

Runs on Windows or Linux or Mac or iOS or Android

Fast server performance supports a thousand simultaneous users.

Multiple bots can cohabit on the same server.



# Support Features

Mature technology in use by various parties around the world.

Integrated tools to support maintaining and testing large systems.

UTF8 support allows scripts written in any language

User support forum on Chatbots.org


#How to install

Take this project and put it into some directory on your machine (typically we call the directory ChatScript, but you can name it whatever). That takes care of installation.

#How to run locally
Go to the BINARIES directory and type `ChatScript` if you are on Windows, or type `./LinuxChatScript64 local` if you are on Linux. This will cause ChatScript to load and ask you for a username. Enter whatever you want. You are then talking to the default demo bot `Harry`.

#How to run as a server
Go to the BINARIES directory and type `ChatScript port=1024` if you are on Windows, or type `./LinuxChatScript64` if you are on Linux. This will cause ChatScript to load as a server. But you also need a client. You can run a separate command window and go to the BINARIES directory and type `ChatScript client=localhost:1024` if you are on Windows, or type `./LinuxChatScript64 client=localhost:1024` if you are on Linux. This will cause ChatScript to load as a client and you can talk to the server. 

#How to compile the engine.
On windows if you have Visual Studio installed, launch VS2010/chatscript.sln or VS2015/chatscript.sln and do a build. The result will go in the BINARIES directory.

On Linux, go stand in the SRC directory and type `make server` (assuming you have make and g++ installed). This creates BINARIES/ChatScript, which can run as a server or locally. There are other make choices for installing PostGres or Mongo.

# how to compile a bot
Run ChatScript locally. From the command prompt, type ':build Harry' or whatever other preinstalled bot exists. If you have revised basic data, you can first do ':build 0' .