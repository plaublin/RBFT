// -*-c++-*-
/* $Id: opnew.h 4052 2009-02-12 13:22:01Z max $ */

/*
 *
 * Copyright (C) 1998 David Mazieres (dm@uun.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#ifndef _NEW_H_INCLUDED_
#define _NEW_H_INCLUDED_ 1

#include <new>
#include "sysconf.h"

using std::nothrow;

#ifdef DMALLOC
using std::nothrow_t;
static class dmalloc_init {
  static bool initialized;
  static void init ();
public:
  bool ok () { return initialized; }
  dmalloc_init () { if (!initialized) init (); }
} __dmalloc_init_obj;
struct dmalloc_t {};
extern struct dmalloc_t dmalloc;
void *operator new (size_t, dmalloc_t, const char *, int);
void *operator new[] (size_t, dmalloc_t, const char *, int);
void *operator new (size_t, nothrow_t, const char *, int) throw ();
void *operator new[] (size_t, nothrow_t, const char *, int) throw ();
#define ntNew new (nothrow, __FILE__, __LINE__)
#define New new (dmalloc, __FILE__, __LINE__)
#define opnew(size) operator new (size, dmalloc, __FILE__, __LINE__)
#if __GNUC__ >= 2
#define DSPRINTF_DEBUG 1
extern int nodelete_ignore_count;
struct nodelete_ignore {
  nodelete_ignore () { nodelete_ignore_count++; }
  ~nodelete_ignore () { nodelete_ignore_count--; }
  operator bool () const { return true; }
};
void nodelete_addptr (const void *obj, const char *fl, int *fp);
void nodelete_remptr (const void *obj, const char *fl, int *fp);
#endif /* GCC2 */

#else
# ifdef SIMPLE_LEAK_CHECKER

using std::nothrow_t;
struct simple_leak_checker_t {};
extern struct simple_leak_checker_t simple_leak_checker;
void *operator new (size_t, simple_leak_checker_t, const char *, int);
void *operator new[] (size_t, simple_leak_checker_t, const char *, int);
void *operator new (size_t, nothrow_t, const char *, int) throw ();
void *operator new[] (size_t, nothrow_t, const char *, int) throw ();
#define ntNew new (nothrow, __FILE__, __LINE__)
#define New new (simple_leak_checker, __FILE__, __LINE__)
#define opnew(size) operator new (size, simple_leak_checker, __FILE__, __LINE__)

# else /* !DMALLOC && !SIMPLE_LEAK_CHECKER */

#define ntNew new (nothrow)
#define New new
#define opnew(size) operator new(size)
# endif /* !SIMPLE_LEAK_CHECKER */
#endif /* !DMALLOC */

#define vNew (void) New

template<class T> inline T
destroy_return (T &t)
{
  T ret = t;
  t.~T ();
  return ret;
}

// XXX - work around egcs 1.1.2 bug:
template<class T> inline T *
destroy_return (T *&tp)
{
  return tp;
}

#endif /* !_NEW_H_INCLUDED_ */

