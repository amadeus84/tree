//
// Driver for tree loading and tree navigation commands (ls, pwd, cd, tree, q).
//
// g++ -std=c++20 -g -Wall -o treeNav treeNav.C
// g++ -std=c++20 -O3 -Wall -o treeNav treeNav.C
//

#include "treeCommands.H"

#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <getopt.h>
#include <cmath>
#include <cctype>
#include <regex>
#include <sys/types.h>

#include "../../wk/COMMON/where.H"

using namespace std;

void usage(char * prog)
{
    cerr << endl << "\e[1;31mUsage: \e[0m"
	 << prog << " [Options]" << endl
	 << endl;

    cerr << "\e[1;33mOptions: \e[0m" << endl
	 << "\t-i tree.txt   read tree from file tree.txt" << endl
	 << "\t-d delim      expected delimiter in tree.txt" << endl
	 << "\t-h            help" << endl
	 << endl;

    cerr << "\e[1;34mPurpose: \e[0m" << endl
	 << "\tNavigate a tree using unix-like commands (ls, pwd, cd, tree)."
	 << endl << endl;

    cerr << "\e[1;32mExample usage: \e[0m" << endl
	 << "\t" << prog << endl
	 << "\t" << prog << " -i tree.txt" << endl
	 << endl;

    cerr << "\e[1;35mPre-requisites: \e[0m" << endl
	 << endl;

    cerr << "\e[1;35mSource file: \e[0m" << endl
	 << "\t" << __FILE__ << endl
	 << endl;

    exit(-1);
}

template <TreeInfoConcept DataType>
bool insert(TreeNode<DataType>& root, const string& path, char pdelim)
{
    using Node=TreeNode<DataType>;
    char ddelim=DataType::delim;   // node delim

    if (path.empty()) return false;

    istringstream InStr(path);
    string tok;
    Node* node=&root;

    // Set the root name if not set:
    getline(InStr, tok, pdelim);   // use path delim to parse apth
    if (root.data.name.empty())
	root.data.name = tok.empty() ? string(1, ddelim) : tok;
    else if (!tok.empty() && root.data.name!=tok) {
	// root has a name
	// if tok is not empty, then it must match the root name.
	cerr << WhereMacro << ": root name missmatch, skipping " << path
	     << endl;
	return 0;
    }

    while (getline(InStr, tok, pdelim) && !tok.empty()) {
	Node*& child=node->children[tok];
	if (child==nullptr) {
	    child=new Node;
	    child->data.parent=node;
	    child->data.name=tok;
	    child->data.level=node->data.level+1;
	    node->children[tok]=child;
	}

	node=child;
    }

    return true;
}

TreeNode<TreeInfo> makeTree()
{
    using Node=TreeNode<TreeInfo>;

    Node root({"/"});

    Node* l=new Node({"L", &root});
    Node* r=new Node({"R", &root});
    root.children["L"]=l;
    root.children["R"]=r;

    // l's children
    l->children["LL"]=new Node({"LL", l});
    l->children["LR"]=new Node({"LR", l});

    // r's children
    r->children["RL"]=new Node({"RL", r});
    r->children["RR"]=new Node({"RR", r});

    setLevel(root);
    setIndex(root);

    return root;
}

// TreeNode<TreeInfo> readTree(const char* ifile)
TreeNode<TreeInfo> makeTree(const vector<string>& paths, char delim)
{
    using Node=TreeNode<TreeInfo>;

    Node root;
    string path;
    for (const auto& path : paths)
	insert(root, path, delim);

    setLevel(root);
    setIndex(root);

    return root;
}

// experimental
template <TreeInfoConcept DataType>
const TreeNode<DataType>* rxfollow(const TreeNode<DataType>& root,
				   const string& arg,
				   const TreeNode<DataType>* node=nullptr)
{
    using Node=TreeNode<DataType>;

    char delim=DataType::delim;
    const char* path=arg.c_str();


    // skip over leading white spaces
    while(path && *path && isspace(*path)) path++;

    // we are at the first non space char
    string_view orig(path);
    const Node* cwd=node ? node : &root;

    // start from the root if path is absolute
    if (*path==delim)
	cwd=&root;

    while(path && *path) {

	while (path && *path==delim) path++;
	if (!path || *path==0)
	    break;

	string_view pv(path);
	size_t pos=pv.find_first_of(delim);
	if (pos==string::npos)
	    pos=pv.size();

	string tok(path, path+pos);
	if (tok==".") { }         // no op
	else if (tok=="..") {     // move up
	    if (cwd->data.parent)
		cwd=cwd->data.parent;
	}
	else {

	    // match pattern against all cwd children
	    regex pattern(path);
	    smatch m;
	    size_t count=0;
	    for (const auto& [key, c] : cwd->children) {

		// Notes:
		// - regex_search() returns true if it finds a substring
		//   in the target string that matches the pattern.
		//
		// - regex_match() returns true if the entire target string
		//   matches the pattern (appropriately named "match").
		//   This is the one we need.
		//
		// Asterisk (*) does not mean match any character, but rather
		// means match the previous atom 0 or more times.
		// Thus regex COM* will not match COMMON, but COM, COMM, COMMM,
		// etc.
		// To match COMMON, we would need COM.* .
		//
		if (regex_match(c->data.name, m, regex("COM.*"))) {
		    count++;
		    cerr << WhereMacro << ": found match: " << c->data.name
			 << endl;
		}
	    }

	    // In makeArguments (shell expansion) it is legitimate to have
	    // multiple matches, e.g. in ./*. The command will process each
	    // match as it sees fit, here we're just expanding the path.

	    const auto& it=cwd->children.find(tok);
	    if (it==cwd->children.end())
		return nullptr;
	    const auto& [name, child]=*it;
	    cwd=child;
	}
	path+=pos;
    }

    return cwd;
}

int main(int argc, char * argv[])
{
    int opt=0;

    const char* ifile=nullptr;
    char delim='/';
    while((opt=getopt(argc,argv,"i:d:h"))!=-1){
	switch(opt){
	case 'i':
	    ifile=optarg;
	    break;
	case 'd':
	    delim=optarg[0];
	    break;
	case 'h':
	    usage(argv[0]);
	    break;
	}
    }

    try{

	CommandFactory<TreeInfo> cmdFactory;

	// Register create() functions for derived classes to api map.
	cmdFactory.add("ls", &List<TreeInfo>::create);
	cmdFactory.add("cd", &ChgDir<TreeInfo>::create);
	cmdFactory.add("pwd", &PWD<TreeInfo>::create);
	cmdFactory.add("tree", &TreeCmd<TreeInfo>::create);
	cmdFactory.add("find", &FindCmd<TreeInfo>::create);
	cmdFactory.add("q", &Quit<TreeInfo>::create);

	// The tree
	vector<string> paths;
	if (ifile) {
	    ifstream InStr(ifile);
	    string path;
	    while (getline(InStr, path))
		paths.emplace_back(path);
	}

	using Node=TreeNode<TreeInfo>;

	Node root = paths.empty() ? makeTree() : makeTree(paths, delim);

	// Try out
	// const Node* junk=rxfollow(root, "COMMON");
	// exit(0);

	const Node* current=&root;

	// Commands can exist outside the command factory.
	// They still go through makeArgs, because that's in the Command
	// constructor, but that's OK.
	PWD<TreeInfo> prompt("", root, current);
	string_view pcolor="\e[1;34m";   // bold face blue
	string_view ecolor="\e[0m";

	string cmdLine;
	cout << pcolor << prompt.get(current) << ecolor << "> ";
	while (current && getline(cin, cmdLine)) {

	    // If command line is empty, keep showing the prompt.
	    if (!cmdLine.empty()) {
		try {
		    auto cmd=cmdFactory.create(cmdLine, root, current);
		    if (cmd)
			current=cmd->exec();
		    else {
			if (!(cmdLine=="h" || cmdLine=="help"))
			    cerr << __FILE__ << ": " << cmdLine << ": "
				 << "command not found" << endl;
			cmdFactory.help(root);
		    }
		}
		catch (const string& err) {
		    cerr << WhereMacro << ": " << err << endl;
		}
	    }

	    if (current)
		cout << pcolor << prompt.get(current) << ecolor << "> ";
	}
    }
    catch(std::bad_alloc & err){
	cerr << WhereMacro << endl
	     << "\t" << err.what() << endl;
    }
    catch(const string & err){
	cerr << WhereMacro << endl
	     << "\t" << err << endl;
    }
    catch(std::exception & err){
	cerr << WhereMacro << endl
	     << "\t" << err.what() << endl;
    }

    return 0;
}
