/*
  Copyright (c) 2007 Stefan Kurtz <kurtz@zbh.uni-hamburg.de>
  Copyright (c) 2007 Center for Bioinformatics, University of Hamburg

  Permission to use, copy, modify, and distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "libgtcore/arraydef.h"
#include "divmodmul.h"
#include "seqpos-def.h"
#include "spacedef.h"
#include "encseq-def.h"
#include "bltrie-ssort.h"

#undef SKDEBUG
#ifdef SKDEBUG
#include "sfx-cmpsuf.pr"
#endif

#define DEPTH(N)        ((N)->depth)
#define EITHER(N)       ((N)->either)
#define FIRSTCHILD(N)   (EITHER(N).firstchild)
#define STARTPOS(N)     (EITHER(N).startpos)
#define RIGHTSIBLING(N) ((N)->rightsibling)
#define ISLEAF(N)       ((N)->isleaf)
#define FIRSTCHAR(N)    ((N)->firstchar)
#define ROOTNODE        trierep->root
#define BlindtrieNULL   NULL

#define NODENUM(PTR)\
        ((PTR) == NULL ? 99UL\
                       : (unsigned long) ((PTR) - trierep->spaceBlindtrienode))

typedef struct Blindtrienode
{
  Seqpos depth;
  struct Blindtrienode *rightsibling;
  union
  {
    struct Blindtrienode *firstchild;
    Seqpos startpos;
  } either;
  bool isleaf;
  Uchar firstchar;
} Blindtrienode;

typedef Blindtrienode * Nodeptr;

DECLAREARRAYSTRUCT(Nodeptr);

struct Blindtrierep
{
  const Encodedsequence *encseq;
  Readmode readmode;
  Seqpos totallength;
  Blindtrienode *spaceBlindtrienode;
  Nodeptr root;
  unsigned long allocatedBlindtrienode,
                nextfreeBlindtrienode;
  ArrayNodeptr stack;
};

typedef struct
{
  Blindtrienode *previous,
                *current;
} Nodepair;

static Nodeptr newBlindtrienode(Blindtrierep *trierep)
{
  assert(trierep->nextfreeBlindtrienode < trierep->allocatedBlindtrienode);
  return trierep->spaceBlindtrienode + trierep->nextfreeBlindtrienode++;
}

static Blindtrienode *makenewleaf(Blindtrierep *trierep,
                                  Seqpos startpos,
                                  Uchar firstchar)
{
  Blindtrienode *newleaf;

  newleaf = newBlindtrienode(trierep);
  STARTPOS(newleaf) = startpos;
  DEPTH(newleaf) = 0;
  ISLEAF(newleaf) = true;
  FIRSTCHAR(newleaf) = firstchar;
  RIGHTSIBLING(newleaf) = NULL;
  return newleaf;
}

static Nodeptr makeroot(Blindtrierep *trierep,Seqpos startpos)
{
  Blindtrienode *root, *newleaf;
  Uchar firstchar;

  root = newBlindtrienode(trierep);
  DEPTH(root) = 0;
  FIRSTCHAR(root) = 0; /* undefined */
  RIGHTSIBLING(root) = BlindtrieNULL;
  ISLEAF(root) = false;
  if (startpos >= trierep->totallength)
  {
    firstchar = (Uchar) SEPARATOR;
  } else
  {
    firstchar = getencodedchar(trierep->encseq, /* Random access */
                               startpos,
                               trierep->readmode);
    if (firstchar == (Uchar) WILDCARD)
    {
      firstchar = (Uchar) SEPARATOR;
    }
  }
  newleaf = makenewleaf(trierep,startpos,firstchar);
  FIRSTCHILD(root) = newleaf;
  return root;
}

static Nodeptr extractleafnode(Nodeptr head)
{
  assert(!ISLEAF(head));
  do
  {
    head = FIRSTCHILD(head);
  } while (!ISLEAF(head));
  return head;
}

static int comparecharacters(Uchar oldchar,Uchar newchar)
{
  if (oldchar < newchar)
  {
    return -1;
  }
  if (oldchar > newchar)
  {
    return 1;
  }
  if (ISSPECIAL(oldchar))
  {
    return -1;
  }
  return 0;
}

static Nodeptr findsucc(Nodeptr node,Uchar newchar)
{
  int retval;
  for (;;)
  {
    retval = comparecharacters(FIRSTCHAR(node),newchar);
    if (retval == 0)
    {              /* found branch corresponding to newchar */
      return node;
    }
    if (retval == 1) /* found branch which is already greater than newchar */
    {
      return NULL;
    }
    node = RIGHTSIBLING(node);
    if (node == BlindtrieNULL) /* no other branches: mismatch */
    {
      return NULL;
    }
  }
}

static Nodeptr findcompanion(Blindtrierep *trierep,Seqpos startpos)
{
  Uchar newchar;
  Nodeptr head, succ;

  trierep->stack.nextfreeNodeptr = 0;
  head = ROOTNODE;
  while (!ISLEAF(head))
  {
    STOREINARRAY (&trierep->stack, Nodeptr, 128, head);
    if (startpos + DEPTH(head) >= trierep->totallength)
    {
      newchar = (Uchar) SEPARATOR;
    } else
    {
      newchar = getencodedchar(trierep->encseq, /* Random access */
                               startpos + DEPTH(head),
                               trierep->readmode);
      if (newchar == (Uchar) WILDCARD)
      {
        newchar = (Uchar) SEPARATOR;
      }
    }
    if (ISSPECIAL(newchar))
    {
      return extractleafnode(head);
    }
    succ = findsucc(FIRSTCHILD(head),newchar);
    if (succ == NULL)
    {
      return extractleafnode(head);
    }
    head = succ;
  }
  STOREINARRAY (&trierep->stack, Nodeptr, 128, head);
  return head;
}

static void insertsuffixintoblindtrie(Blindtrierep *trierep,
                                      Nodeptr oldnode,
                                      Uchar mm_oldsuffix,
                                      Seqpos lcp,
                                      Uchar mm_newsuffix,
                                      Seqpos currentstartpos)
{
  Nodeptr p, *pp;
  int retval;

  assert(ISSPECIAL(mm_oldsuffix) || ISSPECIAL(mm_newsuffix) ||
         mm_oldsuffix != mm_newsuffix || ISLEAF(oldnode) ||
         DEPTH(oldnode) == lcp);

  /* insert a new node before node oldnode if necessary */
  if (DEPTH(oldnode) != lcp)
  {
    p = newBlindtrienode(trierep);
    FIRSTCHAR(p) = mm_oldsuffix;
    DEPTH(p) = DEPTH(oldnode); /* p inherits depth+children of oldnode*/
    ISLEAF(p) = ISLEAF(oldnode);
    EITHER(p) = EITHER(oldnode);
    RIGHTSIBLING(p) = BlindtrieNULL;
    /*
    printf("create split node (leaf=%s) with firstchar %u\n",
            ISLEAF(p) ? "true" : "false", (unsigned int) FIRSTCHAR(p));
    if (!ISLEAF(p))
    {
      printf("of depth %u\n",DEPTH(p));
    }
    */
    DEPTH(oldnode) = lcp;
    ISLEAF(oldnode) = false;
    FIRSTCHILD(oldnode) = p;        /* now *h has p as the only child */
  }
  assert(DEPTH(oldnode) == lcp);

  /* search the position of s[n] among *h offsprings */
  pp = &FIRSTCHILD(oldnode);
  while ((*pp) != BlindtrieNULL)
  {
    retval = comparecharacters(FIRSTCHAR(*pp),mm_newsuffix);
    if (retval >= 0)
    {
      break;
    }
    pp = &RIGHTSIBLING(*pp);
  }
  /* insert new node containing suf */
  p = newBlindtrienode(trierep);
  DEPTH(p) = 0;
  ISLEAF(p) = true;
  FIRSTCHAR(p) = mm_newsuffix;
  /* printf("create leaf with firstchar %u\n",(unsigned int) FIRSTCHAR(p)); */
  RIGHTSIBLING(p) = *pp;
  *pp = p;
  STARTPOS(p) = currentstartpos;
}

static Seqpos getlcp(Uchar *mm_oldsuffix,
                     Uchar *mm_newsuffix,
                     const Encodedsequence *encseq,
                     Readmode readmode,
                     Seqpos totallength,
                     Seqpos leafpos,
                     Seqpos currentstartpos)
{
  Seqpos idx1, idx2;
  Uchar cc1, cc2;

  for (idx1 = leafpos, idx2=currentstartpos;
       /* Nothing */;
       idx1++, idx2++)
  {
    if (idx1 < totallength)
    {
      cc1 = getencodedchar(/*XXX*/ encseq,idx1,readmode);
      if (cc1 == (Uchar) WILDCARD)
      {
        cc1 = (Uchar) SEPARATOR;
      }
    } else
    {
      cc1 = (Uchar) SEPARATOR;
    }
    if (idx2 < totallength)
    {
      cc2 = getencodedchar(/*XXX*/ encseq,idx2,readmode);
      if (cc2 == (Uchar) WILDCARD)
      {
        cc2 = (Uchar) SEPARATOR;
      }
    } else
    {
      cc2 = (Uchar) SEPARATOR;
    }
    if (cc1 != cc2 || ISSPECIAL(cc1))
    {
      *mm_oldsuffix = cc1;
      *mm_newsuffix = cc2;
      break;
    }
  }
  return idx1 - leafpos;
}

#define SETCURRENT(VAL)\
        currentnode = VAL;\
        currentnodeisleaf = ISLEAF(VAL) ? true : false

static unsigned long enumeratetrieleaves (Seqpos *suffixtable,
                                          Seqpos *lcpsubtab,
                                          Blindtrierep *trierep,
                                          Seqpos offset)
{
  bool readyforpop = false, currentnodeisleaf;
  Nodeptr currentnode, siblval, child, lcpnode = ROOTNODE;
  unsigned long nextfree = 0;

  trierep->stack.nextfreeNodeptr = 0;
  STOREINARRAY (&trierep->stack, Nodeptr, 128, ROOTNODE);
  SETCURRENT(FIRSTCHILD(ROOTNODE));
  for (;;)
  {
    if (currentnodeisleaf)
    {
      assert (STARTPOS(currentnode) >= offset);
      if (lcpsubtab != NULL && nextfree > 0)
      {
        lcpsubtab[nextfree] = DEPTH(lcpnode) + offset;
      }
      suffixtable[nextfree++] = STARTPOS(currentnode) - offset;
      siblval = RIGHTSIBLING(currentnode);
      if (siblval == BlindtrieNULL)
      {
        readyforpop = true;
        currentnodeisleaf = false;
      } else
      {
        SETCURRENT (siblval);  /* current comes from brother */
        lcpnode = trierep->stack.spaceNodeptr[trierep->stack.nextfreeNodeptr-1];
      }
    } else
    {
      if (readyforpop)
      {
        if (trierep->stack.nextfreeNodeptr == 1UL)
        {
          break;
        }
        trierep->stack.nextfreeNodeptr--;
        siblval = RIGHTSIBLING(trierep->stack.spaceNodeptr[
                               trierep->stack.nextfreeNodeptr]);
        if (siblval != BlindtrieNULL)
        {
          SETCURRENT (siblval);        /* current comes from brother */
          lcpnode = trierep->stack.spaceNodeptr[
                             trierep->stack.nextfreeNodeptr - 1];
          readyforpop = false;
        }
      } else
      {
        STOREINARRAY (&trierep->stack, Nodeptr, 128, currentnode);
        child = FIRSTCHILD(currentnode);
        SETCURRENT (child);            /* current comes from child */
      }
    }
  }
  return nextfree;
}

#ifdef SKDEBUG
static void checkcurrentblindtrie(Blindtrierep *trierep,
                                  Seqpos offset)
{
  Seqpos suffixtable[6];
  unsigned long idx, numofsuffixes;
  Seqpos maxcommon;
  int retval;

  numofsuffixes = enumeratetrieleaves (&suffixtable[0], NULL, trierep, offset);
  for (idx=1UL; idx < numofsuffixes; idx++)
  {
    maxcommon = 0;
    retval = comparetwostringsgeneric(trierep->encseq,
                                      ISDIRREVERSE(trierep->readmode)
                                        ? false : true,
                                      ISDIRCOMPLEMENT(trierep->readmode)
                                        ? true : false,
                                      &maxcommon,
                                      suffixtable[idx-1],
                                      suffixtable[idx],
                                      0);
    if (retval >= 0)
    {
      fprintf(stderr,"retval = %d, maxcommon = %u for idx = %lu\n",
              retval,maxcommon,idx);
      assert(retval < 0);
    }
  }
}
#endif

Blindtrierep *newBlindtrierep(unsigned long numofsuffixes,
                              const Encodedsequence *encseq,
                              Readmode readmode)
{
  Blindtrierep *trierep;

  ALLOCASSIGNSPACE(trierep,NULL,Blindtrierep,1);
  trierep->allocatedBlindtrienode = MULT2(numofsuffixes + 1) + 1;
  ALLOCASSIGNSPACE(trierep->spaceBlindtrienode,NULL,Blindtrienode,
                   trierep->allocatedBlindtrienode);
  trierep->nextfreeBlindtrienode = 0;
  trierep->encseq = encseq;
  trierep->readmode = readmode;
  trierep->root = NULL;
  trierep->totallength = getencseqtotallength(encseq);
  INITARRAY (&trierep->stack, Nodeptr);
  return trierep;
}

void freeBlindtrierep(Blindtrierep **trierep)
{
  FREESPACE((*trierep)->spaceBlindtrienode);
  FREEARRAY (&(*trierep)->stack, Nodeptr);
  FREESPACE(*trierep);
}

#ifdef SKDEBUG
static void showleaf(const Blindtrierep *trierep,unsigned int level,
                     Nodeptr current)
{
  printf("%*.*s",(int) (6 * level),(int) (6 * level)," ");
  assert(current != NULL);
  printf("Leaf(add=%lu,firstchar=%u,startpos=" FormatSeqpos
         ",rightsibling=%lu)\n",
         NODENUM(current),
         (unsigned int) FIRSTCHAR(current),
         PRINTSeqposcast(STARTPOS(current)),
         NODENUM(RIGHTSIBLING(current)));
}

static void showintern(const Blindtrierep *trierep,unsigned int level,
                       Nodeptr current)
{
  printf("%*.*s",(int) (6 * level),(int) (6 * level)," ");
  assert(current != NULL);
  printf("Intern(add=%lu,firstchar=%u,depth=" FormatSeqpos
         ",firstchild=%lu,rightsibling=%lu)\n",
          NODENUM(current),
          (unsigned int) FIRSTCHAR(current),
          PRINTSeqposcast(DEPTH(current)),
          NODENUM(FIRSTCHILD(current)),
          NODENUM(RIGHTSIBLING(current)));
}

static void showblindtrie2(const Blindtrierep *trierep,
                           unsigned int level,
                           Nodeptr node)
{
  Nodeptr current;

  for (current = FIRSTCHILD(node);
       current != BlindtrieNULL;
       current = RIGHTSIBLING(current))
  {
    if (ISLEAF(current))
    {
      showleaf(trierep,level,current);
    } else
    {
      showintern(trierep,level,current);
      showblindtrie2(trierep,level+1,current);
    }
  }
}

static void showblindtrie(const Blindtrierep *trierep)
{
  showblindtrie2(trierep,0,ROOTNODE);
}
#endif

static int suffixcompare(const void *a, const void *b)
{
  assert(*((Seqpos *) a) != *((Seqpos *) b));
  if (*((Seqpos *) a) < *((Seqpos *) b))
  {
    return -1;
  }
  return 1;
}

void blindtriesuffixsort(Blindtrierep *trierep,
                         Seqpos *suffixtable,
                         Seqpos *lcpsubtab,
                         unsigned long numberofsuffixes,
                         Seqpos offset)
{
  unsigned long i, t;
  Nodeptr leafinsubtree, currentnode;
  Seqpos lcp;
  Uchar mm_oldsuffix, mm_newsuffix;

  qsort(suffixtable,(size_t) numberofsuffixes, sizeof (Seqpos), suffixcompare);
  trierep->nextfreeBlindtrienode = 0;
  trierep->root = makeroot(trierep,suffixtable[0] + offset);
#ifdef SKDEBUG
  printf("insert suffixes at offset " FormatSeqpos ":\n",
          PRINTSeqposcast(offset));
  for (i=0; i < numberofsuffixes; i++)
  {
    printf(FormatSeqpos " ",PRINTSeqposcast(suffixtable[i] + offset));
  }
  printf("\nstep 0\n");
  showblindtrie(trierep);
#endif
  for (i=1UL; i < numberofsuffixes; i++)
  {
    leafinsubtree = findcompanion(trierep, suffixtable[i] + offset);
    assert(ISLEAF(leafinsubtree));
    lcp = getlcp(&mm_oldsuffix,&mm_newsuffix,
                 trierep->encseq,trierep->readmode,
                 trierep->totallength,
                 STARTPOS(leafinsubtree),
                 suffixtable[i] + offset);
    currentnode = ROOTNODE;
    for (t=0;t<trierep->stack.nextfreeNodeptr;t++)
    {
      currentnode = trierep->stack.spaceNodeptr[t];
      if (ISLEAF(currentnode) || DEPTH(currentnode) >= lcp)
      {
        break;
      }
    }
    insertsuffixintoblindtrie(trierep,
                              currentnode,
                              mm_oldsuffix,
                              lcp,
                              mm_newsuffix,
                              suffixtable[i] + offset);
#ifdef SKDEBUG
    printf("step %lu\n",i);
    showblindtrie(trierep);
    checkcurrentblindtrie(trierep,offset);
#endif
  }
  (void) enumeratetrieleaves (suffixtable, lcpsubtab, trierep, offset);
}
