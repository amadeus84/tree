#ifndef _TREEFUNCTORS_H_
#define _TREEFUNCTORS_H_

// Define the TreeInfoConcept.
//
// Define concrete tree node functors (functions that can be applied to a single
// tree node), templated on the TreeInfoConcept. These can be called from
// tree traversals.
//
// Define also a concrete TreeInfo class that is a TreeInfoConcept and with it
// test the node functors and the tree traversals. This need not be in this
// file, but we define it here for convenience.
//

#include "tree.H"

#include <iostream>
#include <fstream>
#include <string_view>
#include <utility>
#include <regex>
#include <cassert>

#include "../COMMON/where.H"

using namespace std;

//////////////////////////////////////////////////////////////////////
// Concept of tree info
//////////////////////////////////////////////////////////////////////

template <class T>
concept TreeInfoConcept=requires
{
    same_as<decltype(T::name), string>;
    same_as<decltype(T::parent), TreeNode<T>*>;
    same_as<decltype(T::level), size_t>;
    same_as<decltype(T::idx), size_t>;
    same_as<decltype(T::delim), char>;
    same_as<decltype(T::anyLevel), size_t>;
};

// equivalently,

// template <class T>
// concept TreeInfoConcept=requires(T a)
// {
//     { a.name } -> std::same_as<std::string&>;  // why the &?
//     { a.parent } -> same_as<TreeNode<T>*&>;
//     { a.level } -> same_as<size_t&>;
//     { a.idx } -> same_as<size_t&>;
//     { a.delim } -> same_as<char&>;
//     { a.anyLevel } -> same_as<size_t&>;
// };


//////////////////////////////////////////////////////////////////////
// Operations on TreeNode<TreeInfoConcept> (tree node functors)
//////////////////////////////////////////////////////////////////////

// We don't enforce TreeInfoConcept on print, so that it works with
// TreeNode<string>. For indentation, we use NodeFunction::level_ rather than
// TreeNode::data.level, if we want to allow for data to be a just string.
//
// Note that this simple PrintNode template is not adequate for a given
// DataType, we can specialize it to that particular DataType. For instance,
// DataType might contain a description string for the node (besides the name)
// so we might want to print that as part of the -l option (long).
template <class DataType>
class PrintNode : public NodeFunction<DataType>
{
public:

    PrintNode(const string& ofile="") : Out(&cout) {
	if (!ofile.empty())
	    Out=new ofstream(ofile.c_str(), ios::out);
    }
    ~PrintNode() { if (Out!=&cout) delete Out; }

    using Base=NodeFunction<DataType>;

    class Config
    {
    public:
	bool long_=false;
	char indentChar='\t';
	size_t minLevel=0;
	size_t maxLevel=-1ul;
	string_view nonLeafColor="\e[1;34m";   // bold face blue
	string_view endColor="\e[0m";
    };

    Config config;
    ostream* Out=&cout;

    bool operator()(TreeNode<DataType>& node) override {

	const auto& [long_, indentChar_,
		     minLevel_, maxLevel_, nonLeafColor, endColor]=config;

	// Do nothing if we're not within the level (depth) set by the user
	if ( ! (minLevel_ <= Base::level_ && Base::level_<maxLevel_) ) {
	    bool rval=Base::level_<minLevel_ ? true : false;
	    Base::level_++;      // we decrease this onExit()
	    return rval;
	}

	string tabs=string(Base::level_, indentChar_);
	if ( indentChar_=='*' ) tabs += " "; // org mode
	if (long_) {
	    if (node.children.empty())            // print leafs plain
		*Out << tabs << node.data << ": " << node.data.name << endl;
	    else {
		*Out << tabs << node.data << ": ";
		*Out << nonLeafColor << node.data.name << endColor << endl;
	    }
	}
	else {
	    if (node.children.empty())
		*Out << tabs << node.data.name << endl;
	    else {
		*Out << tabs << nonLeafColor << node.data.name << endColor
		     << endl;
	    }
	}
	Base::level_++;
	return true;
    }

    bool onExit(TreeNode<DataType>&) override { Base::level_--; return true; }
};

// Call from DFS to set the node level.
template <TreeInfoConcept DataType>
class Level : public NodeFunction<DataType>
{
public:

    using Base=NodeFunction<DataType>;

    bool operator()(TreeNode<DataType>& node) override {
	node.data.level=Base::level_;
	Base::level_++;
	return true;
    }

    bool onExit(TreeNode<DataType>& node) override { Base::level_--; return true;}
};

// Call BFS to set the linear index
template <TreeInfoConcept DataType>
class Index : public NodeFunction<DataType>
{
public:

    using Base=NodeFunction<DataType>;

    bool operator()(TreeNode<DataType>& node) override {
	node.data.idx=Base::idx_;
	Base::idx_++;
	return true;
    }

    bool onExit(TreeNode<DataType>& node) override { Base::idx_--; return true;}
};

// Count nodes with minLevel <= level < maxLevel.
// Any DataType will work here, not just the TreeInfoConcept.
template <class DataType>
class NodeCounter : public NodeFunction<DataType>
{
public:

    using Base=NodeFunction<DataType>;

    NodeCounter(size_t l=0, size_t L=-1ul) : minLevel(l), maxLevel(L) {}

    bool operator()(TreeNode<DataType>& node) override {
	if (minLevel<=Base::level_ && Base::level_<maxLevel)
	    n++;
	Base::level_++;
	return true;
    }

    bool onExit(TreeNode<DataType>& node) override { Base::level_--; return true;}

    size_t numel() const { return n; }

private:
    size_t n=0;
    size_t minLevel=0, maxLevel=-1ul;
};

// Used to find, in a tree, a node tha contains "namestr" in its name.
// Perhaps we can do regex find?
// If isinf(level), then find all on any level.
// Otherwise, find only on the given level.
template <TreeInfoConcept DataType>
class Find : public NodeFunction<DataType>
{
public:

    using Base=NodeFunction<DataType>;
    using Node=TreeNode<DataType>;

    Find(const char* namestr, size_t l=DataType::anyLevel, bool exct=false)
	: re(namestr), name(namestr), level(l), exact(exct) {}

    bool operator()(Node& node) override {
	if ((level==DataType::anyLevel) || (node.data.level==level)) {
	    if (regex_match(node.data.name, re))
		retNodes.push_back(&node);
	}
	return true;
    }
    bool onExit(Node& node) override { return true; }

    // do not return reference, by design, use NRVO (copy elision)
    vector<Node*> nodes() const { return retNodes; }

private:
    regex re;
    const char* name=nullptr;
    size_t level=DataType::anyLevel;
    bool exact=false;
    vector<Node*> retNodes;
};

// Given a path, and a node at level l, check whether the name of the
// current node matches the l-th token in the path (which might be a regex).
// If it does, add the node name to the path so far and continue down the
// input path with children.
// When we've reached the end of the input path, store the path so far.
//
// Because of paths like ./../../foo/bar*, we can't do path expansion by
// exhaustively traversing the tree, trying to match token k in the path to
// some node on level k in the tree. No node will match "..".
//
// Rather, we have to actually follow the path and when encounter a token with
// wildcards, loop over all children to see which one matches.
//
// Remove this class in a later commit.
template <TreeInfoConcept DataType>
class PathExpander : public NodeFunction<DataType>
{
public:

    using Base=NodeFunction<DataType>;
    using Node=TreeNode<DataType>;

    PathExpander(const string& path) {

	char delim=DataType::delim;
	assert(path[0]!='"');

	const char* p=path.c_str(), *pend=p+path.size();
	while (p<pend) {

	    // foo* will match foo, fooo, foooo... i.e. will math 0 or more o's.
	    // But that's not what we want, we want * to match any char after o.
	    // Therefore, we must match foo.*, i.e. insert a dot before *.
	    // That's why we make a copy of [p, end) and insert a . before *.
	    vector<char> tok;
	    const char* end=p-1;
	    while (++end<pend) {
		// we are at a delmiter and the delimiter is not escaped
		if (*end==delim && (end>p && *(end-1)!='\\'))
		    break;
		if (*end=='*') tok.push_back('.');  // the . before *
		tok.push_back(*end);
	    }

	    cerr << WhereMacro << ": " << string_view(tok.begin(), tok.end()) << endl;
	    tokens.push_back(regex(tok.begin(), tok.end()));
	    p=end+1;
	}
    }

    bool operator()(TreeNode<DataType>& node) override {

	assert(!tokens.empty() && "Empty tokens");
	// assert(Base::level_<tokens.size() && "Level exceeds number of tokens.");
	assert(currToken<tokens.size() && "Level exceeds number of tokens.");


	// Get token for current level:
	const auto& pattern=tokens[Base::level_];

	// If we add node to pathSoFar, then onExit() should remove it,
	// but onExit() does not know whether we've matched or not.
	// So we unconditionally add node to pathSoFar here (and increase level)
	// and unconditionally undo that in onExit().
	pathSoFar.push_back(&node);
	Base::level_++;

	if (!regex_match(node.data.name, pattern))
	    return false;

	// We matched, see if we're done
	if (Base::level_>=tokens.size()) {

	    // If we've seen all tokens, form the path string and store it
	    auto nit=pathSoFar.begin();
	    string path=(*nit)->data.name;

	    while((++nit)!=pathSoFar.end())
		path+=(DataType::delim + (*nit)->data.name);
	    paths.emplace_back(path);

	    return false;    // don't go below us
	}

	cerr << WhereMacro << ": matched " << node.data.name << endl;

	// we've matched and we have more tokens, continue
	return true;
    }

    bool onExit(TreeNode<DataType>& node) override {

	// We should always be the last in pathSoFar
	assert(pathSoFar.back()==&node);

	pathSoFar.pop_back();
	Base::level_--;

	return true;
    }

    const vector<string>& get() const { return paths; }

private:

    size_t currToken=0;
    vector<Node*> pathSoFar;
    vector<regex> tokens;
    vector<string> paths;
};


//////////////////////////////////////////////////////////////////////
// Traversals using the above derived NodeFunction(s).
// These are just self-contained convenience wrappers around DFS and BFS.
//////////////////////////////////////////////////////////////////////

template <TreeInfoConcept DataType>
void setLevel(TreeNode<DataType>& root)
{
    DFS<DataType> dfs;
    Level<DataType> L;
    dfs(root, L);
};

template <TreeInfoConcept DataType>
void setIndex(TreeNode<DataType>& root)
{
    BFS<DataType> bfs;
    Index<DataType> Idx;
    bfs(root, Idx);
}

template <class DataType>
size_t countNodes(TreeNode<DataType>& root,
		  size_t minLevel=0, size_t maxLevel=-1ul)
{
    DFS<DataType> dfs;
    NodeCounter<DataType> C(minLevel, maxLevel);
    dfs(root, C);
    return C.numel();
}

// TODO: make this templated on TreeTraversal, defaulting on dfs
template <class DataType>
void printTree(const TreeNode<DataType>& root, const string& ofile="",
	       const typename PrintNode<DataType>::Config& config={})
{
    DFS<DataType> dfs;
    PrintNode<DataType> printNode(ofile);
    printNode.config=config;
    TreeNode<DataType>* r=const_cast<TreeNode<DataType>*>(&root);
    dfs(*r, printNode);
};

// Find a node by name in the tree
template <TreeInfoConcept DataType>
auto find(const TreeNode<DataType>& root, const char* name,
	  bool exact=false, size_t level=DataType::anyLevel)
{
    // FIXME: level BFS
    BFS<DataType> bfs;           // search no deeper than level l, inclusive
    Find<DataType> f(name, level, exact);  // find on specific level
    bfs(const_cast<TreeNode<DataType>&>(root), f);
    return f.nodes();
}


// Most useful
// If path is absolute, follow it from the root.
// If path is relative,
//    if node!= nullptr, follow path from node
//    else follow path from root
template <TreeInfoConcept DataType>
const TreeNode<DataType>* follow(const TreeNode<DataType>& root,
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

// Find the leftmost child at a given level below root.
template <TreeInfoConcept DataType>
auto findLeftmost(const TreeNode<DataType>& root, size_t level)
{
    // base case
    if (level==0) return &root;

    // level > node.level, call on children, from left to right
    const TreeNode<DataType>* node=nullptr;
    for (auto it=root.children.begin(); it!=root.children.end(); it++) {
	const auto [key, child]=*it;
	node=findLeftmost(*child, level-1);
	if (node!=nullptr) return node;
    }

    return node;
}

template <TreeInfoConcept DataType>
auto findRightmost(const TreeNode<DataType>& root, size_t level)
{
    // base case
    if (level==0) return &root;

    // level > node.level, call on children, right to left
    const TreeNode<DataType>* node=nullptr;
    for (auto it=root.children.rbegin(); it!=root.children.rend(); it++) {
	const auto [key, child]=*it;
	node=findRightmost(*child, level-1);
	if (node!=nullptr) return node;
    }

    return node;
}



// Concrete TreeInfo class that exactly matches TreeInfoConcept.
//
// With it, we can define a TreeNode<TreeInfo> in main, on which we can
// perform all tree node operations (functors) defined above.
//
// More complex node data can be derived from this class.
class TreeInfo
{
public:

    using TreeNode_t=TreeNode<TreeInfo>;

    // No constructor, use list initialization.

    string name;
    TreeNode<TreeInfo>* parent=nullptr;
    size_t level=0;
    size_t idx=0;             // linear index, set this with BFS

    static const char delim='/';
    static const size_t anyLevel=-1ul;

    // Unfortunately, we can't use the virtual operator<<() trick because
    // virtual functions prevent initializer list construction.
    // But we shouldn't have to print much more than the info anyway.
    // If we do, just redefine the << operator in the derived class.
    friend ostream& operator<<(ostream& Out, const TreeInfo& info) {
	Out << info.level << ": " << info.idx;
	return Out;
    }
};

#endif
