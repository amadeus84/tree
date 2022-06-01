# tree

TreeNode class.

Depth First Search traversal (DFS).

Breadth First Search traversal (BFS).

Some functors that can be called during the tree traversals, to visit the individual tree nodes. 

Unix-like tree commands that parse user input, accepting short options and performing (path) expansion (e.g. ls -l foo*). 

A sample main (treeNav.C) that reads and executes user commands in a loop.

These commands are C++ classes derived from an abstract NodeFunction class. Internally, they may call on DFS or BFS, with a particular node visitor. Example commands: ls, pwd, cd, tree, find. Obviously, other node operations/functions can be derived as needed, such as compute something at a node, or plot data at a node. This tree library only provides the framework for projects where tree traversal might be useful.

Except for treeNav.C, everything is headers-only: 
- tree.H: TreeNode, NodeFunction, DFS and BFS
- treeFunctors.H: node visitors 
- treeCommands.H, all in C++20 (because we use concepts).

We separate the tree functionality (traversals and navigating the tree) from data. The TreeNode class is templated on the data type and the DFS and BFS tree traversal functions take as input the root node and the abstract visitor class. This makes everything scalable, without ever touching the traversal mechanism. 

Compilation

g++ -o treeNav treeNav.C
