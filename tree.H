#ifndef _TREE_H_
#define _TREE_H_

// TreeNode, NodeFunction and DFS and BFS tree traversals.
//
// Making the node data a separate entity in the node, allows for generic
// tree traversals, i.e. traversals that
//    - are agnostic the node internals (other than that it has children)
//    - are agnostic to what operation needs to be done when a node is visited.
//
// When a node is actually visited during a traversals, we call on a node
// functor (visitor) to perform the operation on that node. The NodeFunctor
// class is the abstract interface to the node operations.
//
// Concrete tree functors are defined elsewhere, as they are application
// dependent.

#include <map>
#include <queue>
#include <string>

using namespace std;

template<class DataType, class KeyType = string>
class TreeNode
{
public:
    // Do not provide a constructor, so we can use list initialization.

    ~TreeNode()
    {
        // This will clean the whole tree recursively, using the system stack.
        // It entails that the child nodes were allocated on the heap (new).
        for(auto& [key, child] : children)
	    delete child;
    }

    DataType data;
    map<KeyType, TreeNode*> children;
};

// Tree node functor interface (operation that can be applied to a single
// TreeNode, e.g. from a tree traversal like DFS or BFS).
template<class DataType>
class NodeFunction
{
public:
    using Node = TreeNode<DataType>;

    // return:
    //     - true: continue to visit children
    //     - false: do not visit children
    virtual bool operator()(Node&) { return true; }
    virtual bool onExit(Node&) { return true; }

    size_t level() const { return level_; }
    size_t index() const { return idx_; }

    static constexpr size_t none = -1ul;
protected:
    size_t level_ = 0;
    size_t idx_ = 0;
};

//////////////////////////////////////////////////////////////////////
// DFS and BFS tree traversals
//////////////////////////////////////////////////////////////////////

// DFS need not be a class, but make it a class rather than a function,
// so that we can include it in multiple files without the need of a .C file.
template<class DataType>
class DFS
{
public:
    using Node = TreeNode<DataType>;
    using Func = NodeFunction<DataType>;

    // Depth-first search, up to maxLevel
    void operator()(Node& node, Func& func, size_t maxLevel = -1ul) const
    {
	bool cont=true;
        if(func.level() < maxLevel) {
            const DFS& dfs = *this;
	    cont=func(node);
	    if (cont) {
		for(auto& [key, child] : node.children)
		    dfs(*child, func, maxLevel);
	    }
            func.onExit(node);
        }
    }
};

// Breadth-first search
template<class DataType>
class BFS
{
public:
    using Node = TreeNode<DataType>;
    using Func = NodeFunction<DataType>;

    void operator()(Node& root, Func& func)
    {
	bool cont=true;
        q.push(&root);
        while(!q.empty()) {
            auto* node = q.front();
            q.pop();
            cont=func(*node);
	    if (cont) {
		for(auto& [k, child] : node->children)
		    q.push(child);
	    }
        }
    }
private:
    queue<Node*> q;
};

#endif
