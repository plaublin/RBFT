
# 1 "logger.T"
// -*-c++-*-

#include "sfs_logger.h"

//-----------------------------------------------------------------------

sfs::logger_t::logger_t (str n, int m, int t) 
  : _file (n), _mode (m), _lock (tame::lock_t::OPEN), _tries (t), _pid (-1),
    _destroyed (New refcounted<bool> (false)) {}

//-----------------------------------------------------------------------

sfs::logger_t::~logger_t ()
{
  warn << "killing sfs_logger[" << _pid << "]\n";
  *_destroyed = true;
  _pid = -1;
  _x = NULL;
  _cli = NULL;
}

//-----------------------------------------------------------------------

void
sfs::logger_t::eofcb (ptr<bool> df)
{
  if (*df) { return; }
  warn << "logger[" << _pid << "] died\n";
  _cli = NULL;
  _x = NULL;
  _pid = -1;
}

//-----------------------------------------------------------------------

# 36 "logger.T"
class sfs__logger_t__launch__closure_t : public closure_t { public:   sfs__logger_t__launch__closure_t (sfs::logger_t *_self,  evb_t ev,  bool do_lock) : closure_t ("logger.T", "sfs::logger_t::launch"), _self (_self),  _stack (_self, ev, do_lock), _args (ev, do_lock) {}   typedef void  (sfs::logger_t::*method_type_t) ( evb_t ,  bool , ptr<closure_t>);   void set_method_pointer (method_type_t m) { _method = m; }   void reenter ()   {     ((*_self).*_method)  (_args.ev, _args.do_lock, mkref (this));   } void v_reenter () { reenter (); }   struct stack_t {     stack_t (sfs::logger_t *_self,  evb_t ev,  bool do_lock) : t (0)  {}      str path;      int t;      vec< str > args;   };   struct args_t {     args_t ( evb_t ev,  bool do_lock) : ev (ev), do_lock (do_lock) {}      evb_t ev;      bool do_lock;   };   sfs::logger_t *_self;   stack_t _stack;   args_t _args;   method_type_t _method;   bool is_onstack (const void *p) const   {     return (static_cast<const void *> (&_stack) <= p &&             static_cast<const void *> (&_stack + 1) > p);   } }; 
# 36 "logger.T"
void 
sfs::logger_t::launch( evb_t __tame_ev,  bool __tame_do_lock, ptr<closure_t> __cls_g)
{
  
# 39 "logger.T"
  sfs__logger_t__launch__closure_t *__cls;   ptr<sfs__logger_t__launch__closure_t > __cls_r;   const char *__cls_type = "sfs__logger_t__launch__closure_t";   use_reference (__cls_type);   if (!__cls_g) {     if (tame_check_leaks ()) start_rendezvous_collection ();     __cls_r = New refcounted<sfs__logger_t__launch__closure_t> (this, __tame_ev, __tame_do_lock);     if (tame_check_leaks ()) __cls_r->collect_rendezvous ();     __cls = __cls_r;     __cls_g = __cls_r;     __cls->set_method_pointer (&sfs::logger_t::launch);   } else {     __cls =     reinterpret_cast<sfs__logger_t__launch__closure_t *> (static_cast<closure_t *> (__cls_g));     __cls_r = mkref (__cls);   }    str &path = __cls->_stack.path;    int &t = __cls->_stack.t;    vec< str > &args = __cls->_stack.args;    evb_t &ev = __cls->_args.ev;    bool &do_lock = __cls->_args.do_lock;    use_reference (ev);     use_reference (do_lock);    switch (__cls->jumpto ()) {   case 0: break;   case 1:     goto sfs__logger_t__launch__label_1;     break;   case 2:     goto sfs__logger_t__launch__label_2;     break;   default:     panic ("unexpected case.\n");     break;   }
# 43 "logger.T"


  if (do_lock) {
    
# 46 "logger.T"
  do {     do {       closure_wrapper<sfs__logger_t__launch__closure_t > __cls_g (__cls_r);     __cls->init_block (1, 46);     __cls->set_jumpto (1); 
# 46 "logger.T"
 _lock.acquire (tame::lock_t::EXCLUSIVE, mkevent ()); 
# 46 "logger.T"
      if (!__cls->block_dec_count (__FL__))       return;     } while (0);  sfs__logger_t__launch__label_1:       ;   } while (0);
# 46 "logger.T"

  }

  path = fix_exec_path ("sfs_logger");
  args.push_back (path);
  args.push_back ("-m");
  args.push_back (strbuf ("0%o", _mode));
  args.push_back (_file);

  do {
    if ((_x = axprt_unix_spawnv (path, args, 0x100000))) {
      _pid = axprt_unix_spawn_pid;
      _cli = aclnt::alloc (_x, logger_prog_1);
      _cli->seteofcb (wrap (this, &sfs::logger_t::eofcb, _destroyed));
    } else {
      
# 61 "logger.T"
  do {     do {       closure_wrapper<sfs__logger_t__launch__closure_t > __cls_g (__cls_r);     __cls->init_block (2, 61);     __cls->set_jumpto (2); 
# 61 "logger.T"
 delaycb (1, 0, mkevent ()); 
# 61 "logger.T"
      if (!__cls->block_dec_count (__FL__))       return;     } while (0);  sfs__logger_t__launch__label_2:       ;   } while (0);
# 61 "logger.T"

    }
  } while (t < _tries && !_x);

  if (do_lock) {
    _lock.release ();
  }
  ev->trigger (_x);
# 69 "logger.T"
  do {   __cls->end_of_scope_checks (69);   return;   } while (0);
# 69 "logger.T"
}

//-----------------------------------------------------------------------

# 73 "logger.T"
class sfs__logger_t__turn__closure_t : public closure_t { public:   sfs__logger_t__turn__closure_t (sfs::logger_t *_self,  evb_t ev) : closure_t ("logger.T", "sfs::logger_t::turn"), _self (_self),  _stack (_self, ev), _args (ev) {}   typedef void  (sfs::logger_t::*method_type_t) ( evb_t , ptr<closure_t>);   void set_method_pointer (method_type_t m) { _method = m; }   void reenter ()   {     ((*_self).*_method)  (_args.ev, mkref (this));   } void v_reenter () { reenter (); }   struct stack_t {     stack_t (sfs::logger_t *_self,  evb_t ev) : ret (false)  {}      bool ret;      clnt_stat err;   };   struct args_t {     args_t ( evb_t ev) : ev (ev) {}      evb_t ev;   };   sfs::logger_t *_self;   stack_t _stack;   args_t _args;   method_type_t _method;   bool is_onstack (const void *p) const   {     return (static_cast<const void *> (&_stack) <= p &&             static_cast<const void *> (&_stack + 1) > p);   } }; 
# 73 "logger.T"
void 
sfs::logger_t::turn( evb_t __tame_ev, ptr<closure_t> __cls_g)
{
  
# 76 "logger.T"
  sfs__logger_t__turn__closure_t *__cls;   ptr<sfs__logger_t__turn__closure_t > __cls_r;   const char *__cls_type = "sfs__logger_t__turn__closure_t";   use_reference (__cls_type);   if (!__cls_g) {     if (tame_check_leaks ()) start_rendezvous_collection ();     __cls_r = New refcounted<sfs__logger_t__turn__closure_t> (this, __tame_ev);     if (tame_check_leaks ()) __cls_r->collect_rendezvous ();     __cls = __cls_r;     __cls_g = __cls_r;     __cls->set_method_pointer (&sfs::logger_t::turn);   } else {     __cls =     reinterpret_cast<sfs__logger_t__turn__closure_t *> (static_cast<closure_t *> (__cls_g));     __cls_r = mkref (__cls);   }    bool &ret = __cls->_stack.ret;    clnt_stat &err = __cls->_stack.err;    evb_t &ev = __cls->_args.ev;    use_reference (ev);    switch (__cls->jumpto ()) {   case 0: break;   case 1:     goto sfs__logger_t__turn__label_1;     break;   case 2:     goto sfs__logger_t__turn__label_2;     break;   default:     panic ("unexpected case.\n");     break;   }
# 79 "logger.T"


  
# 81 "logger.T"
  do {     do {       closure_wrapper<sfs__logger_t__turn__closure_t > __cls_g (__cls_r);     __cls->init_block (1, 81);     __cls->set_jumpto (1); 
# 81 "logger.T"
 _lock.acquire (tame::lock_t::EXCLUSIVE, mkevent ()); 
# 81 "logger.T"
      if (!__cls->block_dec_count (__FL__))       return;     } while (0);  sfs__logger_t__turn__label_1:       ;   } while (0);
# 81 "logger.T"

  if (_cli) {
    
# 83 "logger.T"
  do {     do {       closure_wrapper<sfs__logger_t__turn__closure_t > __cls_g (__cls_r);     __cls->init_block (2, 83);     __cls->set_jumpto (2); 
# 83 "logger.T"
 RPC::logger_prog_1::logger_turn (_cli, &ret, mkevent (err)); 
# 83 "logger.T"
      if (!__cls->block_dec_count (__FL__))       return;     } while (0);  sfs__logger_t__turn__label_2:       ;   } while (0);
# 83 "logger.T"

    if (err) {
      warn << "Error in logger::turn RPC: " << err << "\n";
    }
  }
  _lock.release ();
  ev->trigger (ret);
# 90 "logger.T"
  do {   __cls->end_of_scope_checks (90);   return;   } while (0);
# 90 "logger.T"
}

//-----------------------------------------------------------------------

# 94 "logger.T"
class sfs__logger_t__log__closure_t : public closure_t { public:   sfs__logger_t__log__closure_t (sfs::logger_t *_self,  str s,  evb_t ev) : closure_t ("logger.T", "sfs::logger_t::log"), _self (_self),  _stack (_self, s, ev), _args (s, ev) {}   typedef void  (sfs::logger_t::*method_type_t) ( str ,  evb_t , ptr<closure_t>);   void set_method_pointer (method_type_t m) { _method = m; }   void reenter ()   {     ((*_self).*_method)  (_args.s, _args.ev, mkref (this));   } void v_reenter () { reenter (); }   struct stack_t {     stack_t (sfs::logger_t *_self,  str s,  evb_t ev) : ret (false)  {}      bool ret;      clnt_stat err;   };   struct args_t {     args_t ( str s,  evb_t ev) : s (s), ev (ev) {}      str s;      evb_t ev;   };   sfs::logger_t *_self;   stack_t _stack;   args_t _args;   method_type_t _method;   bool is_onstack (const void *p) const   {     return (static_cast<const void *> (&_stack) <= p &&             static_cast<const void *> (&_stack + 1) > p);   } }; 
# 94 "logger.T"
void 
sfs::logger_t::log( str __tame_s,  evb_t __tame_ev, ptr<closure_t> __cls_g)
{
  
# 97 "logger.T"
  sfs__logger_t__log__closure_t *__cls;   ptr<sfs__logger_t__log__closure_t > __cls_r;   const char *__cls_type = "sfs__logger_t__log__closure_t";   use_reference (__cls_type);   if (!__cls_g) {     if (tame_check_leaks ()) start_rendezvous_collection ();     __cls_r = New refcounted<sfs__logger_t__log__closure_t> (this, __tame_s, __tame_ev);     if (tame_check_leaks ()) __cls_r->collect_rendezvous ();     __cls = __cls_r;     __cls_g = __cls_r;     __cls->set_method_pointer (&sfs::logger_t::log);   } else {     __cls =     reinterpret_cast<sfs__logger_t__log__closure_t *> (static_cast<closure_t *> (__cls_g));     __cls_r = mkref (__cls);   }    bool &ret = __cls->_stack.ret;    clnt_stat &err = __cls->_stack.err;    str &s = __cls->_args.s;    evb_t &ev = __cls->_args.ev;    use_reference (s);     use_reference (ev);    switch (__cls->jumpto ()) {   case 0: break;   case 1:     goto sfs__logger_t__log__label_1;     break;   case 2:     goto sfs__logger_t__log__label_2;     break;   case 3:     goto sfs__logger_t__log__label_3;     break;   default:     panic ("unexpected case.\n");     break;   }
# 100 "logger.T"


  
# 102 "logger.T"
  do {     do {       closure_wrapper<sfs__logger_t__log__closure_t > __cls_g (__cls_r);     __cls->init_block (1, 102);     __cls->set_jumpto (1); 
# 102 "logger.T"
 _lock.acquire (tame::lock_t::EXCLUSIVE, mkevent ()); 
# 102 "logger.T"
      if (!__cls->block_dec_count (__FL__))       return;     } while (0);  sfs__logger_t__log__label_1:       ;   } while (0);
# 102 "logger.T"


  if (!_cli) {
    
# 105 "logger.T"
  do {     do {       closure_wrapper<sfs__logger_t__log__closure_t > __cls_g (__cls_r);     __cls->init_block (2, 105);     __cls->set_jumpto (2); 
# 105 "logger.T"
 launch (mkevent (ret), false); 
# 105 "logger.T"
      if (!__cls->block_dec_count (__FL__))       return;     } while (0);  sfs__logger_t__log__label_2:       ;   } while (0);
# 105 "logger.T"

  }

  if (_cli) {
    
# 109 "logger.T"
  do {     do {       closure_wrapper<sfs__logger_t__log__closure_t > __cls_g (__cls_r);     __cls->init_block (3, 109);     __cls->set_jumpto (3); 
# 109 "logger.T"
 RPC::logger_prog_1::logger_log (_cli, s, &ret, mkevent (err)); 
# 109 "logger.T"
      if (!__cls->block_dec_count (__FL__))       return;     } while (0);  sfs__logger_t__log__label_3:       ;   } while (0);
# 109 "logger.T"

    if (err) {
      warn << "Error in logger::log RPC: " << err << "\n";
    }
  }
  _lock.release ();
  ev->trigger (ret);
# 116 "logger.T"
  do {   __cls->end_of_scope_checks (116);   return;   } while (0);
# 116 "logger.T"
}

//-----------------------------------------------------------------------

