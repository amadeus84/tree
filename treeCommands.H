#ifndef _TREE_COMMANDS_H_
#define _TREE_COMMANDS_H_

// Tree commands: ls, pwd, cd, tree, etc. and command factory.
//
// Given a tree, we can listen in a loop for user input, get the first token
// and create the appropriate command instance for the token, then execute
// the command on the tree.
//
// These commands are named after the common unix commands and closely mimic
// their behavior.
//
// Example:
// cd /A/A1/A23
// will return a pointer to the tree node called A23.

#include "treeFunctors.H"

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <list>
#include <functional>
#include <memory>
#include <cassert>
#include <regex>
#include <unistd.h>

using namespace std;

template <TreeInfoConcept DataType>
class Command
{
public:

    using Node=TreeNode<DataType>;

    Command(const string& cmdLine, const string& name, const Node& r,
	    const Node* c=nullptr)
	: name_(name), root(&r), current(c) {

	// do we have a file output?
	size_t pos=cmdLine.find('>');
	if (pos!=string::npos) {

	    string str=cmdLine.substr(pos+1);

	    // strip white spaces
	    auto l=[](char c) {
		return !std::isspace<char>(c, std::locale::classic());
	    };
	    auto start=find_if(str.begin(), str.end(), l);
	    auto end=find_if(str.rbegin(), str.rend(), l);

	    ofile=string(start, end.base());
	}
	else
	    pos=cmdLine.size();

	makeArgs(cmdLine.substr(0, pos));
	if (current)
	    shellExpansion(*current);

	if (Argv.empty()) {
	    cerr << WhereMacro << ": empty command" << endl;
	    return;
	}

	cmd=Argv[0];
    }

    virtual const Node* exec(void* data=nullptr) = 0;
    virtual void help() const = 0;
    const string& name() const { return name_; }

protected:
    string cmd, name_;     // e.g. cmd=ls and name="list files or directories"
    static const size_t maxLen=1024;
    char buf[maxLen];

    const Node* root, * current;

    list<string> ArgvStr;
    vector<const char*> Argv;
    vector<Node*> pathSoFar;
    vector<string> paths;


    string ofile;          // output to file, if not empty

    enum WordType {invalid_, plain_, pattern_, quoted_};


    auto makeArgs(const string& cmdLine) {

	Argv.clear();

	enum State {none, token, quoted, endq};
	State state=none;

	vector<vector<char>> words;
	vector<char> word;

	for (auto c : cmdLine) {

	    switch(state) {

	    case none:
		if (isspace(c)) { }  // stay put
		else {
		    word.clear();
		    word.push_back(c);   // quote or not
		    state = c=='"' ? quoted : token;
		}
		break;

	    case token:               // stop on whitespace
		if (isspace(c)) {
		    words.push_back(word);
		    state=none;
		}
		else if (c=='"')
		    throw string("makeArgs: \" preceeded by alpha character in ") + cmdLine;

		else
		    word.push_back(c);
		break;

	    case quoted:              // accept whitespace

		// We keep the quotes, to let shellExpansion() know not to
		// expand this word. Quotes will be removed by shellExpansion().
		word.push_back(c);
		if (c=='"') {
		    words.push_back(word);
		    word.clear();
		    state=endq;       // don't go directly to none, wait for \s
		}
		break;
	    case endq:                // can only receive \s, then go to none
		if (isspace(c))
		    state=none;
		else
		    throw string("makeArgs: non-space after closing \" in ") +
			cmdLine;

		break;
	    }
	}

	if (state==quoted)
	    throw string("makeArgs: unmatched \" in ") + cmdLine;

	if (state==token && !word.empty())
	    words.push_back(word);

	ArgvStr.clear();
	for (const auto& w : words)
	    ArgvStr.emplace_back(w.begin(), w.end());

	// We can't point Argv to ArgvStr while we're growing ArgvStr
	for (const auto& wstr : ArgvStr)
	    Argv.push_back(wstr.c_str());

	return Argv;
    }

    void shellExpansion(const Node& node) {

	auto argit=ArgvStr.begin();
	while (argit != ArgvStr.end()) {

	    const auto& word=argit->c_str();
	    assert(word && *word && "Bad word");

	    WordType wt=wordType(word);

	    switch(wt) {
	    case quoted_:
		// remove quotes
		assert(word[0]=='"' && "First char should be \"");
		*argit=string(word+1, word+argit->size()-1);
		break;

	    case invalid_:
		throw string("shellExpansion: invalid argument ") + *argit;

	    case pattern_:
		pathSoFar.clear();
		paths.clear();
		shellExpansion(node, word); // valid, non-empty pattern

		if (!paths.empty()) {
		    auto next=ArgvStr.erase(argit);
		    ArgvStr.insert(next, paths.begin(), paths.end());
		    argit=next;
		    argit--;      // back up one, so we can increase it below
		}

	    break;

	    default:
		// plain (quoted or not), nothing to do with this word.
		// Note that this includes options (-l, or -h, etc.).
		break;
	    }

	    argit++;
	}

	// Now put everything back in Argv:
	Argv.clear();
	for (const auto& a : ArgvStr)
	    Argv.push_back(a.c_str());
    }

    void shellExpansion(const Node& node, const char* w) {

	// The word might be a path like ./../../foo* so we can't just traverse
	// the tree top to bottom, as no node will be called "..", but rather
	// we have to follow the path.

	// PathExpander<DataType> pex(string(w, wend));
	// DFS<DataType> dfs;
	// Node* n=const_cast<Node*>(&node);
	// dfs(*n, pex);

	// // TODO: replace w from argv with paths found.
	// vector<string> paths=pex.get();
	// for (const auto& p : paths)
	//     cerr << WhereMacro << ": " << p << endl;

	char delim=DataType::delim;
	// assert(w && *w && *w!='"' && "Invalid word");



	// Base case: we are at the end of the word
	if (*w==0) {
	    makePath();
	    return;
	}

	// Normal case: get the token up to / (or 0) and search for it
	// among children.
	// The token can be plain, or pattern, match accordingly.
	auto [tok, wi, isPattern, hasDelim]=adjustPattern(w, delim);
	WordType type=isPattern ? pattern_ : plain_;

	if (!tok.empty()) {

	    // We have a non empty token, follow it, if we can
	    if (type==plain_) {

		string_view svkey(tok.begin(), tok.end());

		if (svkey==".") {
		    shellExpansion(node, wi+1);
		}
		else if (svkey=="..") {
		    if (node.data.parent)
			shellExpansion(*node.data.parent, (*wi ? wi+1 : wi));
		    else
			return;   // FAIL, no parent to go to
		}
		else if (auto it=node.children.find(string {svkey});
			 it!=node.children.end()) {
		    // we've found it, follow that node
		    const auto& [key, child]=*it;
		    pathSoFar.push_back(child);
		    shellExpansion(*child, (*wi ? wi+1 : wi));
		    pathSoFar.pop_back();
		}
		else {
		    return; // FAIL, token not found among children
		}

	    }
	    else {     // pattern, recursively visit all matching children

		// TODO: (double) escape the . if any, to make regex interpret
		// the . as dot, not as any.

		regex re(tok.begin(), tok.end());
		for (const auto& [key, child] : node.children) {
		    if (regex_match(key, re)) {
			pathSoFar.push_back(child);
			shellExpansion(*child, (*wi ? wi+1 : wi));
			pathSoFar.pop_back();
		    }
		}

	    }
	}
	else {
	    cerr << WhereMacro << ": empty token" << endl;  // repeated /
	    assert((*wi==0 || *wi=='/') && "Bad token");
	    shellExpansion(node, (*wi ? wi+1 : wi));
	}
    }

    // form the string and store it.
    void makePath() {

	if (pathSoFar.empty()) return;   // shouldn't happen

	auto nit=pathSoFar.begin();
	string path=(*nit)->data.name;

	while((++nit)!=pathSoFar.end())
	    path+=(DataType::delim + (*nit)->data.name);
	paths.emplace_back(path);
    }

    // FIXME
    // in foo.bar, the dot matches itself, but foo.b* will be sent to regex
    // because of the *, which treats the dot as "any", so we
    // must escape the dot in this case: foo.b* => foo\.b.*
    // Hence, we must distiguish if the token will be matched as string,
    // or if we send it to regex.
    // It will be regex iff it contains *, or ? or [, and implicitly ].

    // -1 if invalid (e.g. unmatched closing " or unmatched [
    // 0 if valid and not a pattern (e.g. "foo bar" or foobar)
    // 1 valid pattern (contains * or ? or [ and matching ])
    static WordType wordType(const char* w) {

	WordType ret=invalid_;

	if (!w) return ret;

	if (w[0]=='"') {  // check for matching " at the very end.
	    while (++w && *w)
		if (*w=='"' && *(w+1)) return invalid_; // quote before the end
	    return *(w-1)=='"' ? quoted_ : invalid_;
	}

	bool isPat=false;
	bool hasRange=false;
	while (w && *w) {
	    if (*w=='*' || *w=='?' || *w=='[' || *w==']') isPat=true;
	    if (*w=='[') {
		if (hasRange) return invalid_; // range within range
		else hasRange=true;
	    }
	    if (*w==']') {
		if (!hasRange) return invalid_; // non-matching ]
		else hasRange=false;
	    }
	    w++;
	}

	if (hasRange) return invalid_;

	return isPat ? pattern_ : plain_;
    }

    // w is null terminated, but upon return, tok is not null-terminated.
    static auto adjustPattern(const char* w, char delim='/') {
	vector<char> tok;
	bool isPattern=false, hasDelim=false;
	const char* wi=w-1;
	while (++wi && *wi && *wi!=delim) {

	    if (*wi=='*') tok.push_back('.');  // the . before *
	    if (*wi==DataType::delim) hasDelim=true;
	    // if (*wi=='.') {
	    // 	tok.push_back('\\');
	    // }
	    tok.push_back(*wi);
	    if (*wi=='*' || *wi=='?' || *wi=='[' || *wi==']')
		isPattern=true;
	}
	return make_tuple(tok, wi, isPattern, hasDelim);
    }
};

template <TreeInfoConcept DataType>
class List : public Command<DataType>
{
public:

    using Base=Command<DataType>;
    using Node=Base::Node;

    List(const string& cmdLine, const Node& r, const Node* current)
	: Base(cmdLine, "list nodes", r, current) {}

    static Base* create(const string& cmdLine,
			const Node& root, const Node* current) {
	return new List(cmdLine, root, current);
    }

    const Node* exec(void* data=nullptr) override {

	const Node* current=Base::current;

	size_t ac=Base::Argv.size();
	char* const * av=(char* const*) Base::Argv.data();

	PrintNode<DataType> printNode;          // stdout
	typename PrintNode<DataType>::Config& config=printNode.config;

	bool lsSelf=false;
	int opt=-1;
	optind=0;
	while ((opt=getopt(ac, av, "lhd"))!=-1) {
	    switch (opt) {
	    case 'l':
		config.long_=true;
		break;
	    case 'd':
		lsSelf=true;
		break;
	    case 'h':
	    default:
		this->help();
		return current;
	    }
	}

	config.indentChar='\0';                 // no tabs

	const Node* cwd=current;
	string path;

	if (optind==(int)ac) {
	    // no path, list children of current node
	    if (cwd->children.empty() || lsSelf)
		printNode(*(const_cast<Node*>(cwd)));
	    else
		for (const auto& [name, child] : cwd->children)
		    printNode(*(const_cast<Node*>(child)));
	}
	else {
	    // one or more paths, list those
	    // PrintNode<DataType> printNode;
	    while (optind < (int) ac) {
		const Node* cwd=current;
		path=av[optind];

		cwd=follow(*Base::root, path, current);
		if (cwd==nullptr) {
		    cerr << "ls: cannot access " << path << ": No such node."
			 << endl;
		    return current;
		}

		// ls proper
		if (cwd->children.empty() || lsSelf)
		    printNode(*(const_cast<Node*>(cwd)));
		else
		    for (const auto& [name, child] : cwd->children)
			printNode(*(const_cast<Node*>(child)));
		optind++;
	    }
	}

	return current;
    }

    void help() const override {
	cerr << "List node" << endl;
	cerr << "Usage: " << endl
	     << "\t" << Base::cmd << " [opt] [path]" << endl;
	cerr << "Options: " << endl
	     << "\t-l\t\tlong list" << endl
	     << "\t-h\t\thelp" << endl
	     << endl;
    }

};

template <TreeInfoConcept DataType>
class ChgDir : public Command<DataType>
{
public:

    using Base=Command<DataType>;
    using Node=Base::Node;

    ChgDir(const string& cmdLine, const Node& root, const Node* current)
	: Base(cmdLine, "change directory", root, current) {}

    static Base* create(const string& arg,
			const Node& root, const Node* current) {
	return new ChgDir(arg, root, current);
    }

    const Node* exec(void* data=nullptr) override {

	const Node* current=Base::current;

	size_t ac=Base::Argv.size();
	char* const * av=(char* const*) Base::Argv.data();

	int opt=-1;
	optind=0;
	while ((opt=getopt(ac, av, "h"))!=-1) {
	    switch (opt) {
	    case 'h':
	    default:
		this->help();
		return current;
	    }
	}

	// there must be 0 or 1 non-option argument
	if ( !(optind==(int)ac || optind+1==(int)ac) ) {
	    cerr << WhereMacro << ": cd: too many arguments" << endl;
	    return current;
	}

	string path;
	if (optind<(int) ac)
	    path=av[optind];

	// if empty path, cd root (home)
	if (path.empty())
	    current=Base::root;
	else {
	    // else, follow the path from the current node
	    const Node* cwd=follow(*Base::root, path, current);
	    if (cwd)
		current=cwd;
	    else
		cerr << "cd: " << path << ": No such node" << endl;
	}

	return current;
    }

    void help() const override {
	cerr << "Change directory" << endl;
	cerr << "Usage: " << endl
	     << "\t" << Base::cmd << " [path]" << endl << endl;
	cerr << "\tIf path is empty, change to root node." << endl
	     << endl;
    }

};

// don't print current path to stdout, cache it in a string
template <TreeInfoConcept DataType>
class PWD : public Command<DataType>
{
public:

    using Base=Command<DataType>;
    using Node=Base::Node;

    PWD(const string& arg, const Node& root, const Node* current)
	: Base("pwd", "print working directory", root, current) {}

    static Base* create(const string& arg,
			const Node& root, const Node* current) {
	return new PWD(arg, root, current);
    }

    const Node* exec(void* data=nullptr) override {

	const Node* current=Base::current;

	size_t ac=Base::Argv.size();
	char* const * av=(char* const*) Base::Argv.data();

	int opt=-1;
	optind=0;
	while ((opt=getopt(ac, av, "h"))!=-1) {
	    switch (opt) {
	    case 'h':
	    default:
		this->help();
		return current;
	    }
	}

	get(current);
	cout << path << endl;

	return current;
    }

    const string& get(const Node* current) const {

	const Node* node=current;

	path=node->data.name;
	node=node->data.parent;
	while (node) {
	    path = DataType::delim + path;
	    if (node->data.name[0]!=DataType::delim) // avoid multiple delim
		path = node->data.name + path;
	    node=node->data.parent;
	}

	return path;
    }

    void help() const override {
	cerr << "Print working directory." << endl;
	cerr << "Usage: " << endl
	     << "\t" << Base::cmd << "[-h]" << endl;
    }

protected:
    mutable string path;
};

template <TreeInfoConcept DataType>
class TreeCmd : public Command<DataType>
{
public:

    using Base=Command<DataType>;
    using Node=Base::Node;

    TreeCmd(const string& cmdLine, const Node& root, const Node* current)
	: Base(cmdLine, "print tree", root, current) {}

    static Base* create(const string& arg,
			const Node& root, const Node* current) {
	return new TreeCmd(arg, root, current);
    }

    const Node* exec(void* data=nullptr) override {

	const Node* current=Base::current;

	size_t ac=Base::Argv.size();
	char* const * av=(char* const*) Base::Argv.data();

	int opt=-1;
	typename PrintNode<DataType>::Config config;

	optind=0;
	while ((opt=getopt(ac, av, "lL:t:h"))!=-1) {
	    switch (opt) {
	    case 'l':
		config.long_=true;
		break;
	    case 'L':
		config.maxLevel=strtol(optarg,NULL, 10);
		break;
	    case 't':
		config.indentChar=*optarg;
		break;
	    case 'h':
	    default:
		this->help();
		return current;
	    }
	}

	// there must be 0 or 1 non-option argument
	if ( !(optind==(int)ac || optind+1==(int)ac) ) {
	    cerr << WhereMacro << ": tree: too many arguments" << endl;
	    return current;
	}
	string path;
	if (optind<(int) ac)
	    path=av[optind];

	const Node* cwd=current;
	if (!path.empty()) {
	    cwd=follow(*Base::root, path, current);
	    if (cwd==nullptr) {
		cerr << "ls: cannot access " << path << ": No such node"
		     << endl;
		return current;
	    }
	}

	// if maxLevel was set, interpret it as relative to cwd, and convert
	// it to absolute level
	if (config.maxLevel<-1ul) config.maxLevel+=cwd->data.level;

	// no color if output to file
	if (!Base::ofile.empty())
	    config.nonLeafColor=config.endColor="";
	printTree(*cwd, Base::ofile, config);

	return current;
    }

    void help() const override {
	cerr << "Print tree below current node or below path." << endl;
	cerr << "Usage: " << endl
	     << "\t" << Base::cmd << " [opt] [path]" << endl;
	cerr << "Options: " << endl
	     << "\t-l\t\tlong print (detailed)" << endl
	     << "\t-L maxLevel\tdescend no more than maxLevel" << endl
	     << "\t-t indentChar\tcharacter used for indentation ('\\t')\n"
	     << "\t-h\t\thelp" << endl;
    }
};

template <TreeInfoConcept DataType>
class FindCmd : public Command<DataType>
{
public:

    using Base=Command<DataType>;
    using Node=Base::Node;

    FindCmd(const string& arg, const Node& root, const Node* current)
	: Base(arg, "find node", root, nullptr) { // don't glob in Find
	Base::current=current;
    }

    static Base* create(const string& arg,
			const Node& root, const Node* current) {
	return new FindCmd(arg, root, current);
    }

    const Node* exec(void* data=nullptr) override {

	const Node* current=Base::current;

	bool exact=false;
	size_t level=DataType::anyLevel;

	size_t ac=Base::Argv.size();
	char* const * av=(char* const*) Base::Argv.data();

	int opt=-1;
	optind=0;
	while ((opt=getopt(ac, av, "el:h"))!=-1) {
	    switch (opt) {
	    case 'l':
		level=strtol(optarg, NULL, 10);
		break;
	    case 'e':
		exact=true;
		break;
	    case 'h':
	    default:
		this->help();
		return current;
	    }
	}

	if (optind+2<(int) ac) {
	    // too many arguments
	    cerr << WhereMacro << ": find: too many arguments" << endl;
	    return current;
	}

	// Synopsis:
	//
	// 1. find               // where = .
	// 2. find what          // where = . and what = argv[argc-1]
	// 3. find where what    // self explanatory
	//
	// Anthying else is an error.

	string where=current->data.name;
	const Node* node=current;
	if (optind+2==(int)ac) {
	    where=av[optind++];
	    node=follow(*Base::root, where, node);
	    cerr << "find: \'" << where << "\': No such node" << endl;
	    return current;
	}

	string what=".*";
	if (optind<(int)ac) {
	    const char* w=av[optind++];          // last argv

	    // scan word to the end (0, not /)
	    auto [tok, wi, isPattern, hasDelim]=Base::adjustPattern(w, 0);
	    what=string(tok.begin(), tok.end());

	    if (hasDelim) {
		cerr << WhereMacro << ": "
		     << "find matches against basenames only, but " << what
		     << " contains node separator (\'"
		     << DataType::delim
		     << "\')" << endl;
		return current;
	    }
	}

	auto nodes=find(*node, what.c_str(), exact, level);
	for (const auto* n : nodes) {
	    PWD<DataType> pwd("pwd", *Base::root, n); // FIXME: move outside?
	    pwd.exec();
	}

	return current;
    }

    void help() const override {
	cerr << "Find node by name." << endl;
	cerr << "Usage: " << endl
	     << "\t" << Base::cmd << " [opt] where what" << endl;
	cerr << "Options: " << endl
	     << "\t-e\t\texact match" << endl
	     << "\t-l level\tsearch specified level only" << endl
	     << "\t-h\t\thelp" << endl;
    }
};


// FIXME or delete
template <TreeInfoConcept DataType>
class Man : public Command<DataType>
{
public:

    using Base=Command<DataType>;
    using Node=Base::Node;

    Man(const string& arg, const Node& root) : Base("man", "man page") {}

    static Base* create(const string& arg, const Node& root, const Node*) {
	return new Man(arg, root);
    }

    const Node* exec(void* data=nullptr) override {
	return nullptr;
    }

    void help() const override {
	cerr << WhereMacro << ": " << Base::cmd << endl;
    }
};

template <TreeInfoConcept DataType>
class Quit : public Command<DataType>
{
public:

    using Base=Command<DataType>;
    using Node=Base::Node;

    Quit(const string& arg, const Node& root) : Base("q", "quit", root) {}

    static Base* create(const string& arg,
			const Node& root, const Node*) {
	return new Quit(arg, root);
    }

    const Node* exec(void* data=nullptr) override {
	return nullptr;
    }

    void help() const override { /* you won't get to see any help */ }
};


template <TreeInfoConcept DataType>
class CommandFactory
{
public:

    using Node=TreeNode<DataType>;
    using Cmd=Command<DataType>;
    using Func=Cmd*(*)(const string&, const Node&, const Node*);

    void add(const string& cmd, Func func) {
	apiMap[cmd]=func;
    }

    auto create(const string& cmdLine,
		const Node& root, const Node* current) {

	if (cmdLine.empty()) return std::shared_ptr<Cmd>(nullptr);

	string cmd;
	istringstream InStr(cmdLine);
	InStr >> cmd;

	// Get the constructor for the derived command from the api map
	// and if found, call it and return the pointer to the new instance.
	if (const auto it=apiMap.find(cmd); it!=apiMap.end()) {
	    auto& [name, func]=*it;
	    Cmd* c=nullptr;
	    c=func(cmdLine, root, current);
	    return std::shared_ptr<Command<DataType>>(c);
	}

	// Boost has its own boost:shared_ptr so we have to be specific here.
	return std::shared_ptr<Cmd>(nullptr);
    }

    void help(const Node& root) {
	cerr << endl << WhereMacro << ": Available commands: " << endl << endl;
	for (const auto& [key, func] : apiMap) {
	    auto cmd=this->create(key, root, nullptr);
	    cerr << "\t" << key << "\t\t" << cmd->name() << endl;
	}
	cerr << endl << WhereMacro << ": For any cmd run \"cmd -h\" for usage."
	     << endl << endl;
    }

private:
    static map<string, typename CommandFactory<DataType>::Func> apiMap;
};

template <TreeInfoConcept DataType>
map<string, typename CommandFactory<DataType>::Func> CommandFactory<DataType>::apiMap;

#endif
