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

#include "aatree.h"
#include <iostream>
#include "log.h"
#include "galloc.h"

/**
 * Construct the tree.
 */
template <class Comparable>
AATree<Comparable>::AATree(int _maxElems) :
    maxElems(_maxElems)
{
    pool = new LIFOPool< AANode<Comparable> > (maxElems + 1 /*for nullNode*/ );
    nullNode = new(pool) AANode<Comparable>;
    nullNode->left = nullNode->right = nullNode;
    nullNode->level = 0;
    root = nullNode;
}

/**
 * Copy constructor.
 */
template <class Comparable>
AATree<Comparable>::AATree(const AATree<Comparable> & rhs) :
    maxElems(rhs.maxElems)
{
    pool = new LIFOPool< AANode<Comparable> > (rhs.maxElems + 1 /*for nullNode*/ );
    nullNode = new(pool) AANode<Comparable>;
    nullNode->left = nullNode->right = nullNode;
    nullNode->level = 0;
    root = clone( rhs.root );
}

/**
 * Destructor for the tree.
 */
template <class Comparable>
AATree<Comparable>::~AATree( )
{
    makeEmpty( );
    pool->free(nullNode);
    delete pool;
}

/**
 * Insert x into the tree; duplicates are ignored.
 */
template <class Comparable>
void AATree<Comparable>::insert( const Comparable & x )
{
    /*printf("INSERT %ld BEFORE:\n", x);
      printTree();*/
    insert( x, root );
    /*printf("AFTER:\n");
      printTree();*/
}

/**
 * Remove x from the tree. Nothing is done if x is not found.
 */
template <class Comparable>
void AATree<Comparable>::remove( const Comparable & x )
{
    /*printf("REMOVE %ld BEFORE:\n", x);
      printTree();*/
    remove( x, root );
    /*printf("AFTER:\n");
      printTree();*/
}

/**
 * Find the smallest item in the tree.
 * Return smallest item or ITEM_NOT_FOUND if empty.
 */
template <class Comparable>
Comparable* AATree<Comparable>::findMin( ) const
{
    if( isEmpty( ) )
        return NULL;

    AANode<Comparable> *ptr = root;
    while( ptr->left != nullNode )
        ptr = ptr->left;

    return &ptr->element;
}

/**
 * Find the largest item in the tree.
 * Return the largest item of ITEM_NOT_FOUND if empty.
 */
template <class Comparable>
Comparable* AATree<Comparable>::findMax( ) const
{
    if( isEmpty( ) )
        return NULL;

    AANode<Comparable> *ptr = root;
    while( ptr->right != nullNode )
        ptr = ptr->right;

    return &ptr->element;
}

/**
 * Find item x in the tree.
 * Returns a pointer to the matching item or NULL if not found.
 */
template <class Comparable>
Comparable* AATree<Comparable>::
find( const Comparable & x ) const
{
    AANode<Comparable> *current = root;
    nullNode->element = x;

    for( ; ; )
    {
        if( x < current->element )
            current = current->left;
        else if( current->element < x )
            current = current->right;
        else if( current != nullNode )
            return &(current->element);
        else
            return NULL;
    }
}




/**
 * Find item x's position in the tree.
 * Returns the position, or panics if the element is not found.
 */
template <class Comparable>
int AATree<Comparable>::getPosition( const Comparable & x ) const
{
    AANode<Comparable> *current = root;
    nullNode->element = x;

    int pos = 0;

    for( ; ; )
    {
        if( x < current->element ) {
            pos += current->rightChildren + 1;
            current = current->left;
        } else if( current->element < x )
        {
            //pos += current->leftChildren + 1;
            current = current->right;
        }
        else if( current != nullNode )
            return pos + current->rightChildren;
        else
            assert(false);
    }
}


/**
 * Make the tree logically empty.
 */
template <class Comparable>
void AATree<Comparable>::makeEmpty( )
{
    makeEmpty( root );
}

/**
 * Test if the tree is logically empty.
 * Return true if empty, false otherwise.
 */
template <class Comparable>
bool AATree<Comparable>::isEmpty( ) const
{
    return root == nullNode;
}


template <class Comparable>
int AATree<Comparable>::size( ) const
{
    return (root == nullNode)? 0 : root->leftChildren + root->rightChildren + 1;
}

/**
 * Print the tree contents in sorted order.
 */
template <class Comparable>
void AATree<Comparable>::printTree( ) const
{
    if( root == nullNode )
        cout << "Empty tree" << endl;
    else
        printTree( root );
}

/**
 * Deep copy.
 */
template <class Comparable>
const AATree<Comparable>& AATree<Comparable>::operator=( const AATree<Comparable> & rhs )
{
    if( this != &rhs )
    {
        makeEmpty( );
        root = clone( rhs.root );
    }

    return *this;
}

/**
 * Internal method to insert into a subtree.
 * x is the item to insert.
 * t is the node that roots the tree.
 * Set the new root.
 */
template <class Comparable>
void AATree<Comparable>::insert( const Comparable & x, AANode<Comparable> * & t )
{
    if( t == nullNode )
        t = new(pool) AANode<Comparable>( x, nullNode, nullNode );
    else if( x < t->element ) {
        insert( x, t->left );
        t->leftChildren++;
    } else if( t->element < x ) {
        insert( x, t->right );
        t->rightChildren++;
    } else {
        assert(false); //can't have duplicates
    }

    skew( t );
    split( t );
}

/**
 * Internal method to remove from a subtree.
 * x is the item to remove.
 * t is the node that roots the tree.
 * Set the new root.
 */
template <class Comparable>
void AATree<Comparable>::remove( const Comparable & x, AANode<Comparable> * & t )
{
    static AANode<Comparable> *lastNode, *deletedNode = nullNode;

    if( t != nullNode )
    {
        // Step 1: Search down the tree and set lastNode and deletedNode
        lastNode = t;
        if( x < t->element )
        {
            if (t->left != nullNode) t->leftChildren--;
            remove( x, t->left );
        }
        /*else if (x == t->element)
          {
          deletedNode = t;
          remove( x, t->right );
          }*/
        else
        {
            if (t->right != nullNode) t->rightChildren--;
            deletedNode = t;
            remove( x, t->right );
        }

        //assert(deletedNode != nullNode); //no delete-on-empty allowed

        // Step 2: If at the bottom of the tree and
        //         x is present, we remove it
        if( t == lastNode )
        {
            if( deletedNode == nullNode || x != deletedNode->element ) {
                //return;   // Item not found; do nothing
                assert(deletedNode != nullNode); //deleting non-existent elements is not allowed
            }
            deletedNode->element = t->element;
            deletedNode = nullNode;
            t = t->right;
            pool->free(lastNode);
        }

        // Step 3: Otherwise, we are not at the bottom; rebalance
        else
            if( t->left->level < t->level - 1 ||
                    t->right->level < t->level - 1 )
            {
                if( t->right->level > --t->level )
                    t->right->level = t->level;
                skew( t );
                skew( t->right );
                skew( t->right->right );
                split( t );
                split( t->right );
            }
    }
}

/**
 * Internal method to make subtree empty.
 */
template <class Comparable>
void AATree<Comparable>::makeEmpty( AANode<Comparable> * & t )
{
    if( t != nullNode )
    {
        makeEmpty( t->left );
        makeEmpty( t->right );
        pool->free(t);
    }
    t = nullNode;
}

/**
 * Internal method to print a subtree in sorted order.
 * @param t the node that roots the tree.
 */
template <class Comparable>
void AATree<Comparable>::printTree( AANode<Comparable> *t ) const
{
    if( t != nullNode )
    {
        printTree( t->left );
        for (int i = 1; i < t->level; i++) cout << "  ";
        cout << t->element << " nl: " <<  t->leftChildren << " nr: " <<  t->rightChildren << " cl: " << t->left->element << " cr: " << t->right->element << " | l: " << t->level << endl;
        printTree( t->right );
    }
}


/**
 * Rotate binary tree node with left child.
 */
template <class Comparable>
void AATree<Comparable>::rotateWithLeftChild( AANode<Comparable> * & k2 ) const
{
    if (k2 == nullNode) return;

    AANode<Comparable> *k1 = k2->left;
    assert(k1 != nullNode);
    assert(k2 != nullNode);
    k2->left = k1->right;
    k2->leftChildren = k1->rightChildren;
    k1->right = k2;
    k1->rightChildren = k2->leftChildren + k2->rightChildren + 1;
    //if (k2->leftChildren + k2->rightChildren == 0) k1->rightChildren -= 1;
    k2 = k1;
}

/**
 * Rotate binary tree node with right child.
 */
template <class Comparable>
void AATree<Comparable>::rotateWithRightChild( AANode<Comparable> * & k1 ) const
{
    if (k1 == nullNode) return;

    AANode<Comparable> *k2 = k1->right;
    assert(k1 != nullNode);
    assert(k2 != nullNode);
    k1->right = k2->left;
    k1->rightChildren = k2->leftChildren;
    k2->left = k1;
    k2->leftChildren = k1->leftChildren + k1->rightChildren + 1;
    //if (k1->leftChildren + k1->rightChildren == 0) k2->leftChildren -= 1;
    k1 = k2;
}

/**
 * Skew primitive for AA-trees.
 * t is the node that roots the tree.
 */
template <class Comparable>
void AATree<Comparable>::skew( AANode<Comparable> * & t ) const
{
    if( t->left->level == t->level )
        rotateWithLeftChild( t );
}

/**
 * Split primitive for AA-trees.
 * t is the node that roots the tree.
 */
template <class Comparable>
void AATree<Comparable>::split( AANode<Comparable> * & t ) const
{
    if( t->right->right->level == t->level )
    {
        rotateWithRightChild( t );
        t->level++;
    }
}

/**
 * Internal method to clone subtree.
 */
template <class Comparable>
AANode<Comparable> *
AATree<Comparable>::clone( AANode<Comparable> * t ) const
{
    panic("AATree is not copyable"); //NOTE (dsm): due to how the copy constructor is implemented, clone will not maintain the left/right children counts
    if( t == t->left )  // Cannot test against nullNode!!!
        return nullNode;
    else
        return new(pool) AANode<Comparable>( t->element, clone( t->left ),
                clone( t->right ), t->level );
}


/* The memory pool */
template <class T>
LIFOPool<T>::LIFOPool(int _maxElems) :
    maxElems(_maxElems), numElems(0)
{
    memPtr = (T*) gm_calloc(maxElems, sizeof(T));
    freeList = (T**) gm_calloc(maxElems, sizeof(T*));
    for (int ii = 0; ii < maxElems; ii++) {
        freeList[ii] = &(memPtr[ii]);
    }
}

template <class T>
LIFOPool<T>::~LIFOPool() {
    //TODO: Free
}

template <class T>
T* LIFOPool<T>::alloc() {
    //warn("alloc, numElems %d maxElems %d", numElems, maxElems);
    assert(numElems < maxElems);
    return freeList[numElems++];
}

template <class T>
void LIFOPool<T>::free(T* ptr) {
    assert(numElems > 0);
    //warn("free, numElems %d maxElems %d", numElems, maxElems);
    memset(ptr, 0, sizeof(T)); //clear eagerly (could do lazily on alloc)
    freeList[--numElems] = ptr;
}



