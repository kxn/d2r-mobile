// Owner-thread exact interval index for GPU-owned NVN guest byte aliases.
// Values are metadata only. Membership, authority and retirement remain owned
// by the image alias list; the index must never extend any proof's lifetime.
#ifndef DM_NVN_VK_ALIAS_INTERVAL_H
#define DM_NVN_VK_ALIAS_INTERVAL_H
#include <stdint.h>

typedef struct alias_range_node {
  struct alias_range_node *left, *right;
  uint64_t start, end, max_end, serial;
  void *owner;
  unsigned height;
} alias_range_node;

static unsigned alias_range_height(const alias_range_node *n) {
  return n ? n->height : 0;
}
static void alias_range_update(alias_range_node *n) {
  unsigned a=alias_range_height(n->left),b=alias_range_height(n->right);
  n->height=1+(a>b?a:b);
  n->max_end=n->end;
  if(n->left && n->left->max_end>n->max_end)n->max_end=n->left->max_end;
  if(n->right && n->right->max_end>n->max_end)n->max_end=n->right->max_end;
}
static alias_range_node *alias_range_rotate_left(alias_range_node *n) {
  alias_range_node *r=n->right;n->right=r->left;r->left=n;
  alias_range_update(n);alias_range_update(r);return r;
}
static alias_range_node *alias_range_rotate_right(alias_range_node *n) {
  alias_range_node *l=n->left;n->left=l->right;l->right=n;
  alias_range_update(n);alias_range_update(l);return l;
}
static alias_range_node *alias_range_balance(alias_range_node *n) {
  if(!n)return n;
  alias_range_update(n);
  int balance=(int)alias_range_height(n->left)-(int)alias_range_height(n->right);
  if(balance>1) {
    if(alias_range_height(n->left->left)<alias_range_height(n->left->right))
      n->left=alias_range_rotate_left(n->left);
    return alias_range_rotate_right(n);
  }
  if(balance < -1) {
    if(alias_range_height(n->right->right)<alias_range_height(n->right->left))
      n->right=alias_range_rotate_right(n->right);
    return alias_range_rotate_left(n);
  }
  return n;
}
static int alias_range_compare(const alias_range_node *a,const alias_range_node *b) {
  if(a->start!=b->start)return a->start<b->start?-1:1;
  if(a->serial!=b->serial)return a->serial<b->serial?-1:1;
  uintptr_t x=(uintptr_t)a->owner,y=(uintptr_t)b->owner;
  return (x>y)-(x<y);
}
static alias_range_node *alias_range_insert(alias_range_node *root,alias_range_node *n,
                                           int *inserted) {
  if(!root) {*inserted=1;return n;}
  int order=alias_range_compare(n,root);
  if(order<0)root->left=alias_range_insert(root->left,n,inserted);
  else if(order>0)root->right=alias_range_insert(root->right,n,inserted);
  return alias_range_balance(root);
}
static alias_range_node *alias_range_take_min(alias_range_node *root,
                                             alias_range_node **minimum) {
  if(!root->left) {*minimum=root;return root->right;}
  root->left=alias_range_take_min(root->left,minimum);
  return alias_range_balance(root);
}
static alias_range_node *alias_range_remove(alias_range_node *root,
                                           const alias_range_node *n,int *removed) {
  if(!root)return root;
  int order=alias_range_compare(n,root);
  if(order<0)root->left=alias_range_remove(root->left,n,removed);
  else if(order>0)root->right=alias_range_remove(root->right,n,removed);
  else {
    *removed=1;
    if(!root->left)return root->right;
    if(!root->right)return root->left;
    alias_range_node *successor,*right=alias_range_take_min(root->right,&successor);
    successor->left=root->left;successor->right=right;root=successor;
  }
  return alias_range_balance(root);
}
// The caller checks zero length and overflow exactly as its old full-list
// oracle did. Stored end uses that oracle's unsigned start+size semantics,
// including wraparound; it is not silently saturated or reinterpreted.
static int alias_range_overlap(const alias_range_node *n,uint64_t start,uint64_t end,
    int (*eligible)(const alias_range_node *,int),int shared) {
  if(!n || n->max_end<=start)return 0;
  if(alias_range_overlap(n->left,start,end,eligible,shared))return 1;
  if(n->start>=end)return 0;
  if(n->end>start && eligible(n,shared))return 1;
  return alias_range_overlap(n->right,start,end,eligible,shared);
}
#endif
