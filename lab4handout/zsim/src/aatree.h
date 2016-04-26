/** $lic$
 * Copyright (C) 2012 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2012 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim, the zcache simulator. zsim was mainly
 * developed at MIT and Stanford by Daniel Sanchez <sanchez@csail.mit.edu>.
 *
 * If you use this software in your research, we request that you reference
 * the zcache paper ("The ZCache: Decoupling ways and Associativity", Sanchez
 * and Kozyrakis, MICRO-44, December 2010) as the source of the simulator in
 * any publications that use this software, and that you send us a citation of
 * your work.
 *
 * Until we release zsim, only MIT and Stanford students and faculty using
 * university equipment are allowed to use zsim. We will release zsim under
 * a GPLv2 license on 2013, upon publication of a paper about it, currently
 * under submission. Until then, you are required to keep this within MIT
 * and/or Stanford. 
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 */

/* An AA Tree implementation, taken from Mark Allen Weiss' implementation,
 * http://users.cis.fiu.edu/~weiss/dsaa_c++/code/
 *
 * Modified to:
 * 1. Keep track of current number of left and right children for each node,
 *    so that we can compute the position of a node in the tree by simply
 *    doing a walk. This is used for profiling how good a walk is.
 * 2. Change the memory allocation to use a fixed-size pool (given at
 *    initialization, and allocated from global memory), with LIFO reuse.
 *    This avoids having to implement a real malloc/free in the global allocator,
 *    and a LIFO pool works well with the way this class is used (remove one
 *    element, immediately insert a new one).
 *
 * -dsm
 * */

#ifndef __AATREE_H__
#define __AATREE_H__

#include <iostream>       // For NULL
#include "log.h"
#include "galloc.h"


template<class T>
class LIFOPool : public GlobAlloc {
    public:
        LIFOPool(int maxElems);
        ~LIFOPool();
        T* alloc();
        void free(T* ptr);
    private:
        int maxElems;
        int numElems;
        T** freeList;
        T*  memPtr;
};


// AATree class
//
// CONSTRUCTION: with ITEM_NOT_FOUND object used to signal failed finds
//
// ******************PUBLIC OPERATIONS*********************
// void insert( x )       --> Insert x
// void remove( x )       --> Remove x
// Comparable find( x )   --> Return item that matches x
// Comparable findMin( )  --> Return smallest item
// Comparable findMax( )  --> Return largest item
// boolean isEmpty( )     --> Return true if empty; else false
// void makeEmpty( )      --> Remove all items
// void printTree( )      --> Print tree in sorted order

// Node and forward declaration because g++ does
// not understand nested classes.
template <class Comparable>
class AATree;

/*template <class T>
class LIFOPool;
*/

template <class Comparable>
class AANode
{
    Comparable element;
    AANode    *left;
    AANode    *right;
    int        level;

    int leftChildren;
    int rightChildren;

    AANode( ) : left( NULL ), right( NULL ), level( 1 ), leftChildren(0), rightChildren(0) { }
    AANode( const Comparable & e, AANode *lt, AANode *rt, int lv = 1 )
        : element( e ), left( lt ), right( rt ), level( lv ), leftChildren(0), rightChildren(0) { } //TODO: If non-null node, set left/right children properly. Problem is, we don't have nullnode to compare to...

    void* operator new (size_t sz, LIFOPool< AANode<Comparable> >* pool) {
        assert(sz == sizeof(AANode<Comparable>));
        return pool->alloc();
    }

    //Kill normal new/delete operators; allocation must ALWAYS happen thru a pool
    void* operator new    (size_t sz) { assert(false); }
    void  operator delete (void* ptr) { assert(false); }

    friend class AATree<Comparable>;
};

template <class Comparable>
class AATree : public GlobAlloc
{
    public:
        explicit AATree(int maxElems);
        AATree( const AATree & rhs );
        ~AATree( );

        Comparable* findMin( ) const;
        Comparable* findMax( ) const;
        Comparable* find( const Comparable & x ) const;
        int getPosition( const Comparable & x ) const;
        bool isEmpty( ) const;
        void printTree( ) const;

        int size() const;

        void makeEmpty( );
        void insert( const Comparable & x );
        void remove( const Comparable & x );

        const AATree & operator=( const AATree & rhs );
    private:
        AANode<Comparable> *root;
        const int maxElems;
        AANode<Comparable> *nullNode;

        LIFOPool< AANode<Comparable> >* pool;

        // Recursive routines
        void insert( const Comparable & x, AANode<Comparable> * & t );
        void remove( const Comparable & x, AANode<Comparable> * & t );
        void makeEmpty( AANode<Comparable> * & t );
        void printTree( AANode<Comparable> *t ) const;

        // Rotations
        void skew( AANode<Comparable> * & t ) const;
        void split( AANode<Comparable> * & t ) const;
        void rotateWithLeftChild( AANode<Comparable> * & t ) const;
        void rotateWithRightChild( AANode<Comparable> * & t ) const;
        AANode<Comparable> * clone( AANode<Comparable> * t ) const;
};

template <class Key, class Value>
class AAMap : public GlobAlloc
{
    public:

        struct Pair {
            Key key;
            Value value;
            bool operator < (const Pair/*<K, V>*/& cmp) const { return key <  cmp.key; }
            bool operator > (const Pair/*<K, V>*/& cmp) const { return key >  cmp.key; }
            bool operator <=(const Pair/*<K, V>*/& cmp) const { return key <= cmp.key; }
            bool operator >=(const Pair/*<K, V>*/& cmp) const { return key >= cmp.key; }
            bool operator ==(const Pair/*<K, V>*/& cmp) const { return key == cmp.key; }
            bool operator !=(const Pair/*<K, V>*/& cmp) const { return key != cmp.key; }
        };

        explicit AAMap(int maxElems) : tree(maxElems) {}
        ~AAMap() {}

        Pair* find(const Key& x) const {
            Pair pair;
            pair.key = x;
            memset(&pair.value, 0, sizeof(Value)); //I don't want uninitialized warnings, even if this stupid...
            Pair* res = tree.find(pair);
            return res;
        }

        Pair* findMin() const {
            return tree.findMin();
        }


/*        Value* find(const Key& x) const {
            Pair pair;
            pair.key = x;
            memset(&pair.value, 0, sizeof(Value)); 
            Pair* res = tree.find(pair);
            return (res==NULL)? NULL : &(res->value);
        }*/

        int getPosition(const Key& x) const {
            Pair  pair;
            pair.key = x;
            memset(&pair.value, 0, sizeof(Value));
            return tree.getPosition(pair);
        }

        bool isEmpty() const {return tree.isEmpty();}
        int size() const {return tree.size();}

        void makeEmpty() {tree.makeEmpty();}

        void insert(const Key& key, const Value& val) {
            Pair/*<Key, Value>*/  pair;
            pair.key = key;
            pair.value = val;
            tree.insert(pair);
        }

        void remove(const Key& x) {
            Pair/*<Key, Value>*/  pair;
            pair.key = x;
            memset(&pair.value, 0, sizeof(Value));
            tree.remove(pair);
        }

    private:
        AATree< Pair/*<Key, Value>*/ > tree;

};

#include "aatree.cpp"
#endif /* __AATREE_H__ */


