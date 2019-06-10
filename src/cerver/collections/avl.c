#include "collections/avl.h"

AVLTree *avl_init (CompPointer comparator, void (*destroy)(void *data)) {

    AVLTree *tree = (AVLTree *) malloc (sizeof (AVLTree));
    if (tree) {
        tree->root = NULL;
        tree->comparator = comparator;
        if (destroy) tree->destroy = destroy;
    }

    return tree;

}

// removes all nodes from an avl tree
void avl_clearTree (AVLNode **node, void (*destroy)(void *data)) {

    if (*node) {
        AVLNode *ptr = *node;
        avl_clearTree (&(ptr->right), destroy);
        avl_clearTree (&(ptr->left), destroy);

        if (destroy) destroy (ptr->id);
        else free (ptr->id);

        free (ptr);
        *node = NULL;
    }

}

bool avl_isEmpty (AVLTree *tree) {

    if (tree->root != NULL) return false;
    else return true;

}

// returns content of required node
void *avl_getNodeData (AVLTree *tree, void *id) {

    AVLNode *node = tree->root;

    while (node != NULL) {
        switch (tree->comparator (node->id, id)) {
            case 0: return node->id; 
            case 1: node = node->left; break;
            case -1: node = node->right; break;
            default: return NULL;
        }
    }

    return NULL;

}

void avl_insertNodeR (AVLNode **parent, CompPointer comparator, void *id, char *flag) ;

// user function for insertion
void avl_insertNode (AVLTree *tree, void *data) {

    char flag = 0;

    avl_insertNodeR (&(tree->root), tree->comparator, data, &flag);

}

void *avl_removeNodeR (AVLTree *tree, AVLNode **parent, CompPointer comparator, void *id, char *flag);

// user function to remove a node
void *avl_removeNode (AVLTree *tree, void *data) {

    char flag = 0;

    return avl_removeNodeR (tree, &tree->root, tree->comparator, data, &flag);

}

// returns if the node is in the tree
bool avl_nodeInTree (AVLTree *tree, void *id) {

    AVLNode *node = tree->root;
    while (node) {
        switch (tree->comparator (node->id, id)) {
            case 0: return true;
            case 1: node = node->left;
            case -1: node = node ->right;
            default: return false;
        }
    }

    return false;

}

#pragma regigion PRIVATE

// TODO: what happens to the data ptr?
AVLNode *avl_newNode (void *data) {

    AVLNode *node = (AVLNode *) malloc (sizeof (AVLNode));

    if (node) {
        node->id = data;
        // memcpy (node->id, data, sizeof (data));
        node->left = NULL;
        node->right = NULL;
        node->balance = 0;
    }

    return node;

}

void avl_rightRotation (AVLNode **parent) {

    AVLNode *aux = (*parent)->left;

    (*parent)->left = aux->right;
    aux->right = *parent;
    *parent = aux;

}

void avl_leftRotation (AVLNode **parent) {

    AVLNode *aux = (*parent)->right;

    (*parent)->right = aux->left;
    aux->left = *parent;
    *parent = aux;

}

// fix balance based on given information
void avl_balanceFix (AVLNode *parent, int dependency) {

    parent->balance = 0;

    switch (dependency) {
        case -1:
            parent->left->balance  = 0;
            parent->right->balance = 1;
            break;
        case 0:
            parent->left->balance  = 0;
            parent->right->balance = 0;
            break;
        case 1:
            parent->left->balance  = -1;
            parent->right->balance = 0;
            break;
        default: break;
    }

}

// verify and treat an insertion in right subtree
void avl_treatLeftInsertion (AVLNode **parent, char *flag) {

    (*parent)->balance--;

    switch ((*parent)->balance) {
        case 0: *flag = 0; break;
        case -2:
            if ((*parent)->left->balance == -1) {
                avl_rightRotation (parent);
                (*parent)->balance = 0;
                (*parent)->right->balance = 0;
            } 
            
            else {
                int balance = (*parent)->left->right->balance;
                avl_leftRotation (&(*parent)->left);
                avl_rightRotation (parent);
                avl_balanceFix (*parent, balance);
            }
            *flag = 0;
            break;
        default: break;
    }

}

// verify and treat a insertion in left subtree
void avl_treatRightInsertion (AVLNode **parent, char *flag) {

    (*parent)->balance++;

    switch ((*parent)->balance) {
        case 0: *flag = 0; break;
        case 2:
            if ((*parent)->right->balance == 1) {
                avl_leftRotation (parent);
                (*parent)->balance = 0;
                (*parent)->left->balance = 0;
            } 
            
            else {
                int balance = (*parent)->right->left->balance;
                avl_rightRotation (&(*parent)->right);
                avl_leftRotation (parent);
                avl_balanceFix (*parent, balance);
            }
            *flag = 0;
            break;
        default: break;
    }
}

// verify and treat a removal in left subtree
void avl_treatLeftReduction (AVLNode **parent, char *flag) {

    (*parent)->balance++;

    short shortCut;
    switch ((*parent)->balance) {
        case 1: *flag = 0; break;
        case 2:
            shortCut = (*parent)->right->balance;
            switch (shortCut) {
                case 1:
                    avl_leftRotation (parent);
                    (*parent)->balance = 0;
                    (*parent)->left->balance = 0;
                    *flag = 1;
                    break;
                case 0:
                    avl_leftRotation (parent);
                    (*parent)->balance = -1;
                    (*parent)->left->balance = 1;
                    *flag = 0;
                    break;
                case -1:
                    shortCut = (*parent)->right->left->balance;
                    avl_rightRotation (&(*parent)->right);
                    avl_leftRotation (parent);
                    avl_balanceFix (*parent, shortCut);
                    *flag = 1;
                    break;
            }
            break;
        default: break;
    }

}

// verify and treat a removal in right subtree
void avl_treatRightReduction (AVLNode **parent, char *flag) {

    (*parent)->balance--;

    short shortCut;
    switch ((*parent)->balance){
        case -1: *flag = 0; break;
        case -2:
            shortCut = (*parent)->left->balance;
            switch (shortCut) {
                case -1:
                    avl_rightRotation (parent);
                    (*parent)->balance = 0;
                    (*parent)->right->balance = 0;
                    *flag = 1;
                    break;
                case 0:
                    avl_rightRotation(parent);
                    (*parent)->balance = 1;
                    (*parent)->right->balance = -1;
                    *flag = 0;
                    break;
                case 1:
                    shortCut = (*parent)->left->right->balance;
                    avl_leftRotation (&(*parent)->left);
                    avl_rightRotation (parent);
                    avl_balanceFix (*parent, shortCut);
                    *flag  = 1;
                    break;
            }
            break;
        default: break;
    }

}

// recursive function to insert in the tree
void avl_insertNodeR (AVLNode **parent, CompPointer comparator, void *id, char *flag) {

    // we are on a leaf, create node and set flag
    if (*parent == NULL){
        *parent = avl_newNode (id);
        *flag = 1;
    } 
    
    else {
        switch (comparator ((*parent)->id, id)){
            // go left
            case 1:
                avl_insertNodeR (&(*parent)->left, comparator, id, flag);
                if (*flag == 1) avl_treatLeftInsertion (&(*parent), flag);
                break;

            // go right
            case -1:
            case  0:
                avl_insertNodeR (&(*parent)->right, comparator, id, flag);
                if (*flag == 1) avl_treatRightInsertion (&(*parent), flag);
                break;

            default: break;
        }
    }

}

// recursive function to remove a node from the tree
void *avl_removeNodeR (AVLTree *tree, AVLNode **parent, CompPointer comparator, void *id, char *flag) {

    //Wrong way
    if (*parent != NULL) {
        switch (comparator ((*parent)->id, id)) {
            case 1: {
                void *data = avl_removeNodeR (tree, &(*parent)->left, comparator, id, flag);
                if (*flag == 1) avl_treatLeftReduction (&(*parent), flag);
                return data;
            }   
            case -1: {
                void *data = avl_removeNodeR (tree, &(*parent)->right, comparator, id, flag);
                if (*flag == 1) avl_treatRightReduction (&(*parent), flag);
                return data;
            }
            case 0:
                if ((*parent)->right != NULL && (*parent)->left != NULL) {
                    AVLNode *ptr = (*parent)->right, copy = *(*parent);

                    while (ptr->left != NULL) ptr = ptr->left;

                    (*parent)->id = ptr->id;
                    ptr->id = copy.id;

                    return avl_removeNodeR (tree, &(*parent)->right, comparator, id, flag);
                }
                
                else {
                    void *data = NULL;

                    if ((*parent)->left != NULL) {
                        AVLNode *p = (*parent)->left;
                        *(*parent) = *(*parent)->left;
                        free (p);

                    } 
                    
                    else if ((*parent)->right != NULL) {
                        AVLNode* p = (*parent)->right;
                        *(*parent) = *(*parent)->right;
                        free (p);
                    } 
                    
                    else {
                        // if (tree->destroy) tree->destroy ((*parent)->id);
                        // else free ((*parent)->id);
                        data = (*parent)->id;
                        free (*parent);
                        *parent = NULL;
                    }
                    *flag = 1;
                    return data;
                }
                break;

            default: break;
        }
    }

}

#pragma endregion